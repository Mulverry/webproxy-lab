#include "csapp.h"



int main(int argc, char **argv) {
    int clientfd;
    char *host, *port, buf[MAXLINE];
    rio_t rio;

    if (argc != 3) { //인자 개수가 3이 아니면 오류 메시지 출력
        fprintf(stderr, "usage: %s <host> <port>\n", argv[0]);
        exit(0);
    }

    host = argv[1];
    port = argv[2];

    clientfd = Open_clientfd(host, port);
    Rio_readinitb(&rio, clientfd);

    while (Fgets(buf, MAXLINE, stdin) != NULL) { //표준 입력에서 한 줄씩 입력을 받아옴. 만약 입력이 더 이상 없다면 루프를 종료: Fgets:지정된 파일stdin에서 MAXLINE만큼 문자열을 읽어와 buf에 저장.
        printf("1. [I'm client] client -> proxy\n");
        Rio_writen(clientfd, buf, strlen(buf));         // 서버에 req 보냄: 클라이언트 소켓 clientfd를 통해 버퍼 buf에 있는 데이터를 프록시 서버에 전송

        printf("6.[I'm client] proxy -> client\n");
        Rio_readlineb(&rio, buf, MAXLINE);              // 서버의 res 받음: 프록시서버로부터 소켓 rio를 통해 버퍼에 있는 데이터를 읽어옴
        printf("srcp=%s\n", buf);

        Fputs(buf, stdout); //buf에 있는 데이터를 표준 출력에 출력: Fputs: buf의 문자열을 지정한 파일stdout에 출력]
    }
    Close(clientfd);
    exit(0);
}