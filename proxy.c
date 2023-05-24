#include <stdio.h>
#include "csapp.h"
#include "hash.c"


/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define LRU_MAGIC_NUMBER 9999
#define CACHE_OBJS_COUNT 10
#define VERBOSE         1
#define CONCURRENCY     1 // 0: 시퀀셜, 1: 멀티스레드, 2: 멀티프로세스

/*cache function*/
void cache_init();
int cache_find(char *url);
int cache_eviction();
void cache_LRU(int index);
void cache_uri(char *uri,char *buf);
void readerPre(int i);
void readerAfter(int i);


typedef struct { // cache_block 구조체
    char cache_obj[MAX_OBJECT_SIZE];
    char cache_url[MAXLINE];
    int LRU; // Least recently used. 캐시의 최근 사용여부
    int isEmpty;

    int readCnt;            /*count of readers*/
    sem_t wmutex;           /*캐시 접근을 위한 세마포어*/
    sem_t rdcntmutex;       /*readcnt변수에 대한 접근을 보호하기 위한 세마포어*/

} cache_block;

typedef struct { //캐시구조체
    cache_block cacheobjs[CACHE_OBJS_COUNT];  /*cacheobjs cache_block 구조체의 배열. 실제 캐시 블록(10)을 저장함.*/
    int cache_num; //현재 캐시된 블록의 수
} Cache;

Cache cache;


/* You won't lose style points for including this long line in your code */
// https://developer.mozilla.org/ko/docs/Glossary/Request_header
static const char *request_hdr_format = "%s %s HTTP/1.0\r\n";
static const char *host_hdr_format = "Host: %s\r\n";
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_connection_hdr = "Proxy-Connection: close\r\n";
static const char *Accept_hdr = "    Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";
static const char *EOL = "\r\n";


void doit(int connfd);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void parse_uri(char *uri,char *hostname,char *path,int *port);
int make_request(rio_t* client_rio, char *hostname, char *path, int port, char *hdr, char *method);
#if CONCURRENCY == 1 
void *thread(void *vargp);  // Pthread_create 에 루틴 반환형이 정의되어있음
#endif



/*프록시 필요기능 : 소켓통신, HTTP 프로토콜 해석, 요청 중개, 캐싱, 에러처리, 보안*/

int main(int argc, char **argv) {
  int listenfd, *clientfd; //소켓 파일 디스크립터: 듣기 식별자, 클라이언트 식별자
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen; //클라이언트 주소 구조체의 크기 저장
  struct sockaddr_storage clientaddr; //클라이언트 소켓 주소정보 저장
  char client_hostname[MAXLINE], client_port[MAXLINE];  // 프록시가 요청을 받고 응답해줄 클라이언트의 IP, Port
  pthread_t tid;  // 스레드에 부여할 tid 번호 (unsigned long)

  /* Check command line args
  명령행 인수의 개수를 검사하여 올바른 개수가 아니면 오류 메시지를 출력하고 프로그램을 종료*/
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]); //stderr스트림에 usage:프로그램이름<port> 형식의 표준에러 메시지 출력
    exit(1);
  }

  cache_init();

  listenfd = Open_listenfd(argv[1]);  // e듣기소켓 오픈. client.c에서 인자로 포트번호 넘겨줌
; 
  while (1) { //while true임.
    clientlen = sizeof(clientaddr); // accept 함수 인자에 넣기 위한 주소 길이를 계산
    clientfd = (int *)Malloc(sizeof(int));  // 여러개의 디스크립터를 만들 것이므로 덮어쓰지 못하도록 고유메모리에 할당
    *clientfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);  // 클라이언트와 연결수락. 프록시가 서버로서 클라이언트와 맺는 파일 디스크립터(소켓 디스크립터) : 고유 식별되는 회선이자 메모리 그 자체

    //클라이언트의 IP 주소와 포트 번호를 얻어 client_hostname과 client_port에 저장
    Getnameinfo((SA *)&clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);

    if (VERBOSE){// VERBOSE가 참(1)인 경우 printf 함수를 사용하여 클라이언트의 연결 정보를 출력
      printf("Connected to (%s, %s)\n", client_hostname, client_port); 
    }

    //CONCURRENCY 옵션에 따라 다른 동작을 수행
    #if CONCURRENCY == 0 //단일스레드모드 : doit 함수를 호출하여 클라이언트 요청을 처리한 후, clientfd를 닫음.
      doit(*clientfd);
      Close(*clientfd);
    
    #elif CONCURRENCY == 1 //스레드모드 : 스레드 모드. thread 함수를 새로운 스레드로 실행하고, doit 함수를 호출하여 클라이언트 요청을 처리한 후, clientfd를 닫습니다.
      Pthread_create(&tid, NULL, thread, (void *)clientfd);

    #elif CONCURRENCY == 2  //프로세스모드 : 새로운 자식을 생성하여 클라이언트 요청을 처리한 다음, 
      if (Fork() == 0) {  // clientfd, listenfd 닫고 자식 프로세스 종료
        Close(listenfd);
        doit(*clientfd);
        Close(*clientfd);
        exit(0);
      }
    Close(*clientfd);
  #endif
  }

  return 0;
}

#if CONCURRENCY == 1 
  void *thread(void *argptr) {
    int clientfd = *((int *)argptr);
    Pthread_detach((pthread_self()));
    doit(clientfd);
    Close(clientfd);
  }
#endif


/*프록시 doit. 클라이언트와의 통신 처리. 클라이언트의 요청을 서버로 전달. 서버의 응답을 클라이언트로 전달.*/
void doit(int client_fd){
  char hostname[MAXLINE], path[MAXLINE]; //프록시가 요청을 보낼 서버의 hostname, 파일경로, 포트번호
  int port;

  char buf[MAXLINE], hdr[MAXLINE]; //문자열을 저장
  char method[MAXLINE], uri[MAXLINE], version[MAXLINE]; //클라이언트 요청의 메소드, uri, 버전

  int server_fd;

  rio_t client_rio;
  rio_t server_rio;

  Rio_readinitb(&client_rio, client_fd); //클라이언트와 연결 시작
  rio_readlineb(&client_rio, buf, MAXLINE); //클라이언트의 요청을 한줄씩 읽어옴
  sscanf(buf, "%s %s %s", method, uri, version); // 클라이언트에서 받은 요청 파싱

  if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD")){//strcasecamp()는 괄호 안이 맞다면 0(FALSE) 반환. -> GET 또는 HEAD가 아닐 경우
    if (VERBOSE) { //VERBOSE가 참(1)인 경우 printf 함수를 사용하여 상세한 로그 메시지를 출력
      printf("[PROXY]501 ERROR\n");
    }
    clienterror(client_fd, method, "501", "잘못된 요청", "501 에러. 올바른 요청이 아닙니다.");
    return;
  } 


  char url_store[100];
  strcpy(url_store,uri);  /*original url을 url_store에 저장 */
  if(strcasecmp(method,"GET")){  // method 문자열이 GET과 다르다면
    printf("Proxy does not implement the method");
    return;
  }

  /*the uri is cached ? */
  int cache_index;/*in cache then return the cache content*/
  if((cache_index=cache_find(url_store))!=-1){//cache_find함수를 이용하여 uri_store에 해당하는 캐시 블록의 인덱스를 찾음. cahce_index는 반환된 인덱스.
      readerPre(cache_index); //cache_index에 해당하는 캐시 블록을 읽는 동안 다른 스레드들이 쓰기 작업을 방지하기 위해 reader 프로세스 락을 설정함. 이는 동시에 여러 스레드가 동일한 캐시 블록을 동시에 읽을 수 있도록 함.
      Rio_writen(client_fd,cache.cacheobjs[cache_index].cache_obj,strlen(cache.cacheobjs[cache_index].cache_obj));  //cleint_fd를 통해 캐시 블록의 콘텐츠를 클라이언트에게 전송.
      readerAfter(cache_index); //캐시블록의 읽기 작업이 완료되었으므로 reader프로세스락 해제.
      return;
  }

  parse_uri(uri, hostname, path, &port); // 클라이언트가 요청한 uri 파싱하여 hostname, path, port(포인터) 변수에 할당
    
  char port_value[100];
  sprintf(port_value,"%d",port); //port 변수값을 문자열(%d)로 변환하여 port_value에 저장.
  server_fd = Open_clientfd(hostname, port_value); // 서버와의 소켓 디스크립터 생성

  if (!make_request(&client_rio, hostname, path, port, hdr, method)) {//클라이언트 요청 생성 실패한 경우
    if (VERBOSE) {
      printf("[PROXY]501 ERROR\n");
    }
    clienterror(client_fd, method, "501", "잘못된 요청", "501 에러. 올바른 요청이 아닙니다.");
  }
  
  Rio_readinitb(&server_rio, server_fd);  // 서버 소켓과 연결
  Rio_writen(server_fd, hdr, strlen(hdr)); // 서버에 req 보냄
  
  char cachebuf[MAX_OBJECT_SIZE]; //캐시에 저장할 응답을 임시로 저장하는 cachebuf선언.
  int sizebuf = 0; //현재까지 버퍼에 정답된 응답의 크기
  size_t n; //Rio_readlineb함수가 읽어온 응답의 길이를 저장
  while ((n=Rio_readlineb(&server_rio, buf, MAXLINE)) > 0) { //서버로부터 한줄씩 읽어온 응답의 길이를 n에 저장. n이 0보다 큰 경우(더 읽어올 응답이 있는 경우)
    sizebuf+=n; //sizebuf + n으로 현재까지 읽어온 응답의 크기 갱신 
    if(sizebuf < MAX_OBJECT_SIZE)
      strcat(cachebuf,buf); //buf에 저장된 응답을 cachebuf에 이어붙임. 응답을 임시로 cahcebuf에 저장.
      Rio_writen(client_fd, buf, n);   // 클라이언트에게 buf에 저장된 응답 전달. n은 읽어온 응답의 길이.
  } 
  Close(server_fd); //

  /*store it*/
  if(sizebuf < MAX_OBJECT_SIZE){
    cache_uri(url_store,cachebuf); //url_sotre에 해당하는 url의 캐시 블록에 cachebuf에 저장된 응답을 저장.
  }
}


/*
 * clienterror - returns an error message to the client
 */
/* $begin clienterror */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Tiny Error</title>"); //body문자열에 HTML태그 포함한 오류페이지의 시작부분 저장.
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg); //buf문자열에 응답헤더의 상태코드와 상태 메시지 저장.
    Rio_writen(fd, buf, strlen(buf)); //buf에 저장된 HTTP응답헤더를 클라이언트에 전달.
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body)); //body에 저장된 오류페이지 내용을 클라이언트에 전달.
}
/* $end clienterror */


void parse_uri(char *uri,char *hostname, char *path, int *port) {
  /*
   uri가  
   / , /cgi-bin/adder 이렇게 들어올 수도 있고,
   http://11.22.33.44:5001/home.html 이렇게 들어올 수도 있다.
   알맞게 파싱해서 hostname, port로, path 나누어주어야 한다!
   주어진 URI를 파싱하여 호스트 이름, 경로, 포트 번호를 추출하고 해당 변수들에 저장하는 역할.
  */

  *port = 80;
  if (VERBOSE) {
    printf("uri=%s\n", uri);
  }
  
  char *parsed;
  parsed = strstr(uri, "//"); //uri문자열에서 //찾기
  
  //만약 "//"문자열이 존재하지 않는다면 uri에 호스트이름과 포트번호가 포함되어있지 않은 경우.
  //(= URI가 "/path" 형식이거나 "/cgi-bin/adder" 형식인 경우)
  if (parsed == NULL) {
    parsed = uri;
  }
  else { // "//" 문자열이 존재하여, URI에 호스트 이름과 포트 번호가 포함된 경우
    parsed = parsed + 2;  // "//"이후로 포인터 두칸 이동 . parsed는 이제 호스트이름과 포트번호가 시작되는 위치 가리킴.
  }
  char *parsed2 = strstr(parsed, ":"); //parsed에서 ":" 문자열을 찾아서 호스트 이름과 포트 번호를 구분.

  if (parsed2 == NULL) {// ':' 이후가 없다면, port가 없음
    parsed2 = strstr(parsed, "/");
    if (parsed2 == NULL) {
      sscanf(parsed,"%s",hostname); // "/" 문자열이 존재하지 않는다면, URI에 호스트 이름만 포함된 것이므로 parsed에서 호스트 이름을 추출하여 hostname에 저장
    } 
    else { // "/" 문자열이 존재한다면, parsed에서 호스트 이름과 경로를 추출하여 각각 hostname과 path에 저장.
        *parsed2 = '\0';
        sscanf(parsed,"%s",hostname);
        *parsed2 = '/';
        sscanf(parsed2,"%s",path);
    }
  } else {// ':' 이후가 있으므로 port가 있음
      *parsed2 = '\0'; // ":" 문자열을 NULL 문자로 대체하여 호스트 이름을 추출
      sscanf(parsed, "%s", hostname);
      sscanf(parsed2+1, "%d%s", port, path); //":" 이후에 오는 문자열에서 포트 번호를 추출하여 port에 저장하고, 경로를 추출하여 path에 저장
  }
  if (VERBOSE) {
    printf("hostname=%s port=%d path=%s\n", hostname, *port, path);
  }
}


int make_request(rio_t* client_rio, char *hostname, char *path, int port, char *hdr, char *method) {
  // 프록시서버로 들어온 요청을 서버에 전달하기 위해 HTTP 헤더 생성
  char req_hdr[MAXLINE], additional_hdr[MAXLINE], host_hdr[MAXLINE]; //요청헤더, 추가적인 헤더정보, 호스트헤더정보 저장하기 위한 변수.
  char buf[MAXLINE];
  char *HOST = "Host";
  char *CONN = "Connection";
  char *UA = "User-Agent";
  char *P_CONN = "Proxy-Connection";
  sprintf(req_hdr, request_hdr_format, method, path); // req_hdr에 request_hdr_format 문자열, method, path를 형식화하여 저장.

  while (1) { //클라이언트로부터 받은 헤더 정보를 처리. 반복문은 클라이언트로부터 빈 줄(EOL)이나 EOF를 받을 때까지 실행
    if (Rio_readlineb(client_rio, buf, MAXLINE) == 0) break; //읽은 결과가 0이면 EOF므로 반복문 종료.
    if (!strcmp(buf,EOL)) break;  // buf와 EOL을 비교하여 같다면 buf == EOL => EOF

    if (!strncasecmp(buf, HOST, strlen(HOST))) {// 호스트 헤더 지정.  buf가 "HOST"헤더를 나타내는 경우.
      strcpy(host_hdr, buf); //host_hdr에 buf복사
      continue;
    }

    if (strncasecmp(buf, CONN, strlen(CONN)) && strncasecmp(buf, UA, strlen(UA)) && strncasecmp(buf, P_CONN, strlen(P_CONN))) {
       // 미리 준비된 헤더(HOST, CONNECTION, USER-AGENT, PROXY-CONNECTION)가 아니면 추가 헤더에 추가 
      strcat(additional_hdr, buf);  
      strcat(additional_hdr, "\r\n");  
    }
  }

  if (!strlen(host_hdr)) {//호스트헤더가 없는 경우. 클라이언트로부터 받은 헤더에 호스트헤더가 포함되어있지 않은 경우.
    sprintf(host_hdr, host_hdr_format, hostname); //host_hdr_format형식 문자열을 사용하여 호스트 헤더를 생성하여 host_hdr에 저장.
  }

  sprintf(hdr, "%s%s%s%s%s%s", 
    req_hdr,   // METHOD URL VERSION
    host_hdr,   // Host header
    user_agent_hdr,
    connection_hdr,
    proxy_connection_hdr,
    EOL
  );

  if (strlen(hdr)) // 생성된 헤더 정보의 길이가 0보다 큰 경우 1을 반환
    return 1;
  return 0;
}



/**************************************
 * Cache Function
 * https://github.com/yeonwooz/CSAPP-Labs
 * 
 * 
 * P: 스레드가 진입시 임계영역 잠금 
 * V: 스레드가 퇴장히 임계영역 열어줌
 **************************************/

void cache_init(){ //캐시 객체와 관련된 변수들을 초기화. 캐시 초기화 함수
    cache.cache_num = 0; //캐시에 저장된 객체의 수 
    int i;
    for(i=0;i<CACHE_OBJS_COUNT;i++){ //for 루프를 사용하여 CACHE_OBJS_COUNT(10)만큼 반복
        cache.cacheobjs[i].LRU = 0;// 캐시의 i번째 캐시블록배열의 LRU(Least Recently Used)변수를 0으로 설정. 
        cache.cacheobjs[i].isEmpty = 1;
        Sem_init(&cache.cacheobjs[i].wmutex,0,1); //쓰기 연산을 보호하기 위해 wmutex 세마포어를 1로 초기화.
        Sem_init(&cache.cacheobjs[i].rdcntmutex,0,1);//읽기 연산의 동시성을 보호하기 위해 rdcntmutex 세마포어를 1로 초기화
        cache.cacheobjs[i].readCnt = 0;//해당 객체를 읽고 있는 쓰레드의 수
    }
}

void readerPre(int i){//캐시 객체의 읽기 연산을 수행하기 전에 호출되는 함수
    P(&cache.cacheobjs[i].rdcntmutex); // cache.cacheobjs[i].rdcntmutex 세마포어를 잠근다.(P연산)-> 현재 읽기 연산의 동시성 보호 및 다른 스레드가 동시에 readCnt 수정 못함.
    cache.cacheobjs[i].readCnt++; 
    if(cache.cacheobjs[i].readCnt==1) P(&cache.cacheobjs[i].wmutex); // 처음으로 읽기 연산을 수행하는 스레드라면: cache.cacheobjs[i].wmutex 세마포어를 잠근다 (P 연산).
    V(&cache.cacheobjs[i].rdcntmutex); // cache.cacheobjs[i].rdcntmutex 세마포어를 풀어준다 (V 연산).
}

void readerAfter(int i){//캐시 객체의 읽기 연산이 완료된 후에 호출되는 함수
    P(&cache.cacheobjs[i].rdcntmutex);
    cache.cacheobjs[i].readCnt--;
    if(cache.cacheobjs[i].readCnt==0) V(&cache.cacheobjs[i].wmutex); // cache.cacheobjs[i].readCnt 값이 0이 되었다면 (더 이상 읽는 스레드가 없는 경우):wmutex세마포어를 푼다 -> 쓰기 연산이 대기 중인 경우 이제 쓰기 연산이 수행될 수 있음.
    V(&cache.cacheobjs[i].rdcntmutex);

}

void writePre(int i){
    P(&cache.cacheobjs[i].wmutex); //wmutex 세마포어를 잠근다.(P연산)
}

void writeAfter(int i){
    V(&cache.cacheobjs[i].wmutex);//wmutex 세마포어를 연다.(V연산)
}

/*find url is in the cache or not
cache_find()함수는 주어진 URL을 캐시에서 검색하고, 검색 결과에 따라 해당 URL이 캐시에 존재하는지를 판단 */
int cache_find(char *url){
    int i;
    for(i=0;i<CACHE_OBJS_COUNT;i++){
        readerPre(i);
        if((cache.cacheobjs[i].isEmpty==0) && (strcmp(url,cache.cacheobjs[i].cache_url)==0)) break; // 해당 캐시 객체가 비어있지 않은 상태(isEmpty = 0)이고 url이 일치하면 반복문 탈출. 해당 url은 캐시에 존재.
        readerAfter(i);
    }
    if(i>=CACHE_OBJS_COUNT) return -1; /*can not find url in the cache*/
    return i; //  URL이 캐시에서 찾아진 캐시 객체의 인덱스 리턴.
}

/*find the empty cacheObj or which cacheObj should be evictioned
가장 적절한 캐시 객체를 선택하여 캐시에서 제거할 객체의 인덱스를 반환하는 함수.
 LRU (Least Recently Used) 알고리즘에 따라 가장 오랫동안 사용되지 않은 객체를 선택하여 제거하는 것을 의미.
*/
int cache_eviction(){
    int min = LRU_MAGIC_NUMBER; // min 변수를 LRU_MAGIC_NUMBER로 초기화.
    int minindex = 0; // minindex 변수를 0으로 초기화. 현재 최소 LRU 값을 갖는 캐시 객체의 인덱스를 저장
    int i;
    for(i=0; i<CACHE_OBJS_COUNT; i++)
    {
        readerPre(i);
        if(cache.cacheobjs[i].isEmpty == 1){/*choose if cache block empty 해당 캐시객체가 비어있는 경우 */
            minindex = i;
            readerAfter(i);
            break; //반복문 탈출 : 비어있는 캐시 객체가 발견되었을 때 해당 객체를 선택하여 제거할 것을 의미.
        }
        if(cache.cacheobjs[i].LRU< min){    /*캐시객체가 비어있지 않을 때, 현재 캐시 객체의 LRU 값이 더 작은 경우*/
            minindex = i;
            readerAfter(i);
            continue;
        }
        readerAfter(i);
    }

    return minindex;
}

/*update the LRU number except the new cache one
 특정 인덱스 이전과 이후에 있는 캐시 객체의 LRU 값을 감소시켜 LRU 알고리즘을 업데이트하는 함수
 해당 인덱스를 최신 사용으로 표시하고 다른 객체들의 LRU 값을 조정하는 역할*/
void cache_LRU(int index){
    int i;
    for(i=0; i<index; i++)    { //i가 index보다 작을 때까지 반복.
        writePre(i);
        if(cache.cacheobjs[i].isEmpty==0){ // 해당 캐시 객체가 비어있지 않은 경우
            cache.cacheobjs[i].LRU--;//해당 캐시 객체의 LRU 값을 업데이트하여 최신 사용으로 표시
        }
        writeAfter(i);
    }
    i++;
    for(i; i<CACHE_OBJS_COUNT; i++)    {//i가 CACHE_OBJS_COUNT(캐시 개체의 총 개수)보다 작을 때까지 반복
        writePre(i);
        if(cache.cacheobjs[i].isEmpty==0){
            cache.cacheobjs[i].LRU--;
        }
        writeAfter(i);
    }
}
/*cache the uri and content in cache
캐시에 URI와 해당 URI에 대한 응답 데이터를 저장하는 기능*/
void cache_uri(char *uri,char *buf) {
    int i = cache_eviction();// 캐시에서 대체할 인덱스(i)를 얻음

    writePre(i);/*writer P쓰기 연산 준비*/

    strcpy(cache.cacheobjs[i].cache_obj,buf); //buf의 내용을 cache.cacheobjs[i].cache_obj에 복사->해당 인덱스에 응답 데이터를 저장
    strcpy(cache.cacheobjs[i].cache_url,uri);
    cache.cacheobjs[i].isEmpty = 0; //해당인덱스의 캐시객체 사용 중(0. False.)으로 표시
    cache.cacheobjs[i].LRU = LRU_MAGIC_NUMBER; //해당 캐시 객체를 최신 사용으로 표시하는 LRU 값을 설정
    cache_LRU(i);  //LRU 알고리즘을 업데이트. 해당 인덱스를 최신 사용으로 표시하고 다른 객체들의 LRU 값을 조정

    writeAfter(i);/*writer V 쓰기 연산 완료*/
}