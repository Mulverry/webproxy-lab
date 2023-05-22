/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */

/*주석 출처 : https://velog.io/@fredkeemhaus/%EB%84%A4%ED%8A%B8%EC%9B%8C%ED%81%AC-%ED%94%84%EB%A1%9C%EA%B7%B8%EB%9E%98%EB%B0%8D-Tiny-%EC%9B%B9-%EC%84%9C%EB%B2%84-%EC%BD%94%EB%93%9C-%EB%B6%84%EC%84%9D*/

#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

// 입력 ./tiny 8000 / argc = 2, argv[0] = tiny, argv[1] = 8000
int main(int argc, char **argv) { //첫 번째 매개변수 argc는 옵션의 개수이며, argv는 옵션 문자열의 배열이다.
  int listenfd, connfd; //소켓 파일 디스크립터: 듣기 식별자, 연결 식별자
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen; //클라이언트 주소 구조체의 크기 저장
  struct sockaddr_storage clientaddr; //클라이언트 소켓 주소정보 저장

  /* Check command line args
  명령행 인수의 개수를 검사하여 올바른 개수가 아니면 오류 메시지를 출력하고 프로그램을 종료*/
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]); //stderr스트림에 usage:프로그램이름<port> 형식의 표준에러 메시지 출력
    exit(1);
  }

  /* Open_listenfd 함수를 호출해서 듣기 소켓을 오픈한다. 인자로 포트번호를 넘겨준다. */
    // Open_listenfd는 요청받을 준비가된 듣기 식별자를 리턴한다 = listenfd
  listenfd = Open_listenfd(argv[1]); // 듣기소켓 오픈. 인자로 포트번호 넘겨줌

//무한서버 루트 실행
  while (1) { //while true임.
    clientlen = sizeof(clientaddr); // accept 함수 인자에 넣기 위한 주소 길이를 계산
    
    // line:netp:tiny:accept 반복적으로 연결요청 접수
    // accept 함수는 1. 듣기 식별자, 2. 소켓주소구조체의 주소, 3. 주소(소켓구조체)의 길이를 인자로 받아서 새로 연결된 소켓 파일 디스크립터 리턴
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);  
    
    // Getaddrinfo는 호스트 이름: 호스트 주소, 서비스 이름: 포트 번호의 스트링 표시를 소켓 주소 구조체로 변환
    // Getnameinfo는 위를 반대로 소켓 주소 구조체에서 스트링 표시로 변환.
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    
    doit(connfd);   // line:netp:tiny:doit 트랜잭션 수행
    Close(connfd);  // line:netp:tiny:close 자신쪽의 연결 소켓을 닫음
  }
}

void doit(int fd)
{
  int is_static; //1이면 정적컨텐츠, 0이면 동적컨텐츠.
  struct stat sbuf; //파일 정보를 저장하기 위한 구조체 변수. stat함수를 사용하여 파일 정보 가져올 때 사용됨.
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE]; //buf는 요청라인 및 헤더를 저장하기 위한 버퍼. method는 http메소드 version은 http버전.
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;  // rio_readlineb를 위해 rio_t 타입(구조체)의 읽기 버퍼를 선언. 읽읽기버퍼와 관련된 정보 저장.

  /*Read request line and headers*/
  /* Rio = Robust I/O */
  // rio_t 구조체를 초기화 해준다.
  Rio_readinitb(&rio, fd); // rio구조체 초기화. &rio 주소를 가지는 읽기 버퍼와 식별자 connfd를 연결하여 읽기작업을 수행할 준비함.
  Rio_readlineb(&rio, buf, MAXLINE);// 버퍼에서 읽은 것이 담겨있다. rio 구조체를 사용하여 파일 디스크립터로부터 한 줄 데이터를 읽어와서 버퍼 buf에 저장.
  printf("Request headers: \n");
  printf("%s", buf);  // "GET 버퍼에서 읽은 것. ex) GET /cat.mp4 /HTTP/1.1
  sscanf(buf, "%s %s %s", method, uri, version);// 버퍼에서 자료형을 읽는다, 분석한다.

//숙제 11.11. HTTP HEAD 메소드 지원
  // if (strcasecmp(method, "GET")){//Tiny에 다른 메소드 요청하면 에러 메시지 보내고 main 루틴으로 돌아옴. strcasecmp(method, "GET")은 문자열 method와 문자열 "GET"을 대소문자 구분 없이 비교하는 함수
  if (strcasecmp(method, "GET") != 0 || strcasecmp(method, "HEAD") != 0){
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }
  read_requesthdrs(&rio); //Tiny에 GET요청 들어오면 읽어들이고 다른 요청헤더 무시

  /*Parse URI from GET request*/
  /* URI 를 파일 이름과 비어 있을 수도 있는 CGI 인자 스트링으로 분석하고, 요청이 정적/ 동적 컨텐츠를 위한 것인지 나타내는 플래그를 설정한다.  */
  is_static = parse_uri(uri, filename, cgiargs);

  //파일이 디스크 상에 없으면 에러메시지 보내고 main루틴으로 리턴
  if (stat(filename, &sbuf) < 0){ //stat는 파일 정보를 불러오고 sbuf에 내용을 적어준다. ok 0, errer -1
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
    return;
  }

  /* Serve static content */
  if (is_static){
    // 파일 읽기 권한이 있는지 확인하기. is_static이 0이 아닌 경우(정적컨텐츠)에 실행됨.
    // S_ISREG : 일반 파일인가? , S_IRUSR: 읽기 권한이 있는지? S_IXUSR 실행권한이 있는가?
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {//sbuf.st_mode : sbuf구조체의 st_mode가 일반파일인지 확인 
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file"); // 권한이 없다면 클라이언트에게 에러를 전달
      return;
    }
    serve_static(fd, filename, sbuf.st_size, method); //권한이 있다면 정적 컨텐츠 제공
  } else {/* Serve dynamic content */
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)){ // 이 파일이 실행가능한지 검증
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program"); // 실행이 불가능하다면 에러를 전달
      return;
    }
    serve_dynamic(fd, filename, cgiargs, method); // 실행가능하면 동적컨텐츠 제공
  }
}


/*Tiny read_requesthdrs 요청헤더를 읽고 무시함. Tiny는 요청헤더 내의 어떤 정보도 사용하지 않음*/
void read_requesthdrs(rio_t *rp){
  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE);

  /* strcmp 두 문자열을 비교하는 함수 */
  /* 헤더의 마지막 줄은 비어있기에 \r\n 만 buf에 담겨있다면 while문을 탈출한다.  */
  while (strcmp(buf, "\r\n")) {
    Rio_readlineb(rp, buf, MAXLINE); //rio 설명에 나와있다시피 rio_readlineb는 \n를 만날때 멈춘다.
    printf("%s", buf); // 멈춘 지점 까지 출력하고 다시 while
  }
  return;
}


/*Tiny parse_uri  HTTP URI를 분석함. tiny는 스트링 cgi-bin을 포함하는 모든 uri가 동적컨텐츠를 요청한다고 가정*/
int parse_uri(char *uri, char *filename, char *cgiargs){
  char *ptr;

   /* strstr 으로 cgi-bin이 들어있는지 확인하고 양수값을 리턴하면 dynamic content를 요구하는 것이기에 조건문을 탈출 */
  if (!strstr(uri, "cgi-bin")) {//요청이 정적 컨텐츠를 위한 것이라면: strstr(대상문자열, 검색할 문자열) 문자열 안에서 문자열로 검색하는 기능.
    strcpy(cgiargs, ""); //cgi인자 스트링을 지움. strcpy(대상문자열, 원본문자열) 문자열 복사하기
    strcpy(filename, "."); //uri를 상대 리눅스 경로 이름(ex. ./index.html 등)으로 바꿈
    strcat(filename, uri); //strcat(최종 문자열, 붙일 문자열)

    //결과 cgiargs = "" 공백 문자열, filename = "./~~ or ./home.html
	  // uri 문자열 끝이 / 일 경우 home.html을 filename에 붙혀준다.
    if (uri[strlen(uri)-1] == '/')
      strcat(filename, "home.html");
    return 1;
  }
  else{ //요청이 동적컨텐츠를 위한 것이라면. 
    ptr = index(uri, '?'); // uri 예시: dynamic: /cgi-bin/adder?first=1213&second
    // index 함수는 문자열에서 특정 문자의 위치를 반환한다
    // CGI인자 추출
    if (ptr){
      // 물음표 뒤에 있는 인자 다 갖다 붙인다.
      // 인자로 주어진 값들을 cgiargs 변수에 넣는다.
      strcpy(cgiargs, ptr+1); // 포인터는 문자열 마지막으로 바꾼다.
      *ptr = '\0'; // uri물음표 뒤 다 없애기
    } else {
      strcpy(cgiargs, "");// 물음표 뒤 인자들 전부 넣기
    }
    strcpy(filename, "."); // 나머지 uri부분을 상대 uri(리눅스 파일이름)으로 변환
    strcat(filename, uri); // ./uri 가 된다.
    return 0;
  }
}



void serve_static(int fd, char*filename, int filesize, char* method){
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  /* 클라이언트에게 응답해더 보내기*/
  get_filetype(filename, filetype);       //line:netp:servestatic:getfiletype
  sprintf(buf, "HTTP/1.0 200 OK\r\n");    //line:netp:servestatic:beginserve
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf); //sprintf: 문자열을 buf 버퍼에 형식화하여 저장하는 함수. 
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  Rio_writen(fd, buf, strlen(buf));       //line:netp:servestatic:endserve
  printf("Response headers:\n");
  printf("%s", buf);

  if (strcasecmp(method, "HEAD") = 0)
    return;

  /*클라이언트에게 응답 본체 보내기*/
  srcfd = Open(filename, O_RDONLY, 0);
  srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
  Close(srcfd);
  Rio_writen(fd, srcp, filesize);
  Munmap(srcp, filesize);
}


/*HTML 서버가 처리할 수 있는 파일 타입을 이 함수를 통해 제공*/
void get_filetype(char *filename, char *filetype) 
{
    if (strstr(filename, ".html"))
	strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif"))
	strcpy(filetype, "image/gif");
    else if (strstr(filename, ".png"))
	strcpy(filetype, "image/png");
    else if (strstr(filename, ".jpg"))
	strcpy(filetype, "image/jpeg");
  /* 11.7 숙제 문제 - Tiny 가 MPG  비디오 파일을 처리하도록 하기.  */
    else if (strstr(filename, ".mp4"))
  strcpy(filetype, "video/mp4");
    else
	strcpy(filetype, "text/plain");
}  


void serve_dynamic(int fd, char *filename, char *cgiargs, char *method) 
{
    char buf[MAXLINE], *emptylist[] = { NULL };

    /* Return first part of HTTP response */
    sprintf(buf, "HTTP/1.0 200 OK\r\n"); 
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Server: Tiny Web Server\r\n");
    Rio_writen(fd, buf, strlen(buf));
  
    if (Fork() == 0) { /* Child 자식 프로세스인 경우*/ //line:netp:servedynamic:fork
	  /* Real server would set all CGI vars here */
	    setenv("QUERY_STRING", cgiargs, 1); //line:netp:servedynamic:setenv cgi프로그램에 환경변수 설정. query_string 환경변수에 cgiargs값 설정.
      setenv("REQUEST_METHOD", method, 1);
	    Dup2(fd, STDOUT_FILENO);         /* Redirect stdout to client */ //line:netp:servedynamic:dup2 표준출력을 클라이언트소켓에 연결.
	    Execve(filename, emptylist, environ); /* Run CGI program */ //line:netp:servedynamic:execve cgi프로그램 실행하고 실행결과 반환. execve로 filename을 실행하고 인자로 빈 리스트와 현재 환경변수 전달.
    }
    Wait(NULL); /* 부모 프로세스는 자식 프로세스가 종료될 때까지 기다림. NULL을 전달하면 자식 프로세스의 종료 상태를 받지 않고 대기. */ //line:netp:servedynamic:wait
}


/*Tiny clienterror: 에러 메시지를 클라이언트에게 보냄*/
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}

// //p930 숙제 11.6 - a문제. tiny수정하여 모든 요청라인과 요청헤더 echo하기>
// void echo(int connfd) 
// {
//     size_t n; 
//     char buf[MAXLINE]; 
//     rio_t rio;

//     Rio_readinitb(&rio, connfd);
//     while((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0) { //line:netp:echo:eof
// 	printf("server received %d bytes\n", (int)n);
// 	Rio_writen(connfd, buf, n);
//     }
// }