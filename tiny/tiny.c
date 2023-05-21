/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

int main(int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]); // 듣기소켓 오픈

  while (1) { //무한서버 루트 실행
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);  // line:netp:tiny:accept 반복적으로 연결요청 접수
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);   // line:netp:tiny:doit 트랜잭션 수행
    Close(connfd);  // line:netp:tiny:close 자신쪽의 연결 소켓을 닫음
  }
}

void doit(int fd)
{
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;

  /*Read request line and headers*/
  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE); //요청라인을 읽음
  printf("Request headers: \n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);
  if (strcasecmp(method, "GET")){//Tiny에 다른 메소드 요청하면 에러 메시지 보내고 main 루틴으로 돌아옴.
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }
  read_requesthdrs(&rio); //Tiny에 GET요청 들어오면 읽어들이고 다른 요청헤더 ㅁ시

  /*Parse URI from GET request*/
  is_static = parse_uri(uri, filename, cgiargs); //요청이 정적/동적 컨텐츠를 위한 것인지 나타내는 플래그 설정
  if (stat(filename, &sbuf) < 0){ // 파일이 디스크 상에 없으면 에러메시지 보내고 main루틴으로 리턴
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
    return;
  }

  if (is_static){
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) { //이 파일이 보통파일인지 읽기 권한을 갖고 있는지 검증
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
      return;
    }
    serve_static(fd, filename, sbuf.st_size); //정적 컨텐츠 제공
  } else {
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)){ // 이 파일이 실행가능한지 검증
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
      return;
    }
    serve_dynamic(fd, filename, cgiargs); // 동적컨텐츠 제공
  }
}


/*Tiny read_requesthdrs 요청헤더를 읽고 무시함. Tiny는 요청헤더 내의 어떤 정보도 사용하지 않음*/
void read_requesthdrs(rio_t *rp){
  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE);
  while (strcmp(buf, "\r\n")) {
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}


/*Tiny parse_uri  HTTP URI를 분석함. tiny는 스트링 cgi-bin을 포함하는 모든 uri가 동적컨텐츠를 요청한다고 가정*/
int parse_uri(char *uri, char *filename, char *cgiargs){
  char *ptr;

  if (!strstr(uri, "cgi-bin")) {//요청이 정적 컨텐츠를 위한 것이라면
    strcpy(cgiargs, ""); //cgi인자 스트링을 지움
    strcpy(filename, "."); //uri를 상대 리눅스 경로 이름(ex. ./index.html 등)으로 바꿈
    strcat(filename, uri); 
    if (uri[strlen(uri)-1] == '/') //만약 uri가 /로 끝난다면 기본 파일이름 추가
      strcat(filename, "home.html");
    return 1;
  }
  else{ //요청이 동적컨텐츠를 위한 것이라면 
    ptr = index(uri, '?');
    if (ptr){ //모든 cgi인자를 추출하고
      strcpy(cgiargs, ptr+1);
      *ptr = '\0';
    } else {
      strcpy(cgiargs, "");
    }
    strcpy(filename, "."); // 나머지 uri부분을 상대 리눅스 파일이름으로 변환
    strcat(filename, uri);
    return 0;
  }
}



void serve_static(int fd, char*filename, int filesize){
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  /* 클라이언트에게 응답해더 보내기*/
  get_filetype(filename, filetype);       //line:netp:servestatic:getfiletype
  sprintf(buf, "HTTP/1.0 200 OK\r\n");    //line:netp:servestatic:beginserve
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  Rio_writen(fd, buf, strlen(buf));       //line:netp:servestatic:endserve
  printf("Response headers:\n");
  printf("%s", buf);
  /*클라이언트에게 응답 본체 보내기*/
  srcfd = Open(filename, O_RDONLY, 0);
  srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
  Close(srcfd);
  Rio_writen(fd, srcp, filesize);
  Munmap(srcp, filesize);
}


/**/
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
    else
	strcpy(filetype, "text/plain");
}  


void serve_dynamic(int fd, char *filename, char *cgiargs) 
{
    char buf[MAXLINE], *emptylist[] = { NULL };

    /* Return first part of HTTP response */
    sprintf(buf, "HTTP/1.0 200 OK\r\n"); 
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Server: Tiny Web Server\r\n");
    Rio_writen(fd, buf, strlen(buf));
  
    if (Fork() == 0) { /* Child */ //line:netp:servedynamic:fork
	/* Real server would set all CGI vars here */
	setenv("QUERY_STRING", cgiargs, 1); //line:netp:servedynamic:setenv
	Dup2(fd, STDOUT_FILENO);         /* Redirect stdout to client */ //line:netp:servedynamic:dup2
	Execve(filename, emptylist, environ); /* Run CGI program */ //line:netp:servedynamic:execve
    }
    Wait(NULL); /* Parent waits for and reaps child */ //line:netp:servedynamic:wait
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