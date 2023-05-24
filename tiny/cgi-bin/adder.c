/*
 * adder.c - a minimal CGI program that adds two numb  ers together
 */
/* $begin adder */
#include "csapp.h"

int main(void) {
    char *buf, *p, *method;
    char arg1[MAXLINE], arg2[MAXLINE], content[MAXLINE];
    int n1=0, n2=0;

    /* Extract the two arguments
    ex) ip주소:포트주소/cgi-bin/adder?arg1=5000&arg2=2 */
    if ((buf = getenv("QUERY_STRING")) != NULL) {
	p = strchr(buf, '&'); //buf 쿼리스트링(5000&2)에서 &문자를 찾음. &은 두 개의 변수를 구분하는 구분자로 사용. p는 &문자를 가리킴. 
	*p = '\0'; //&문자를 \0으로 하면 문자열 종료.

    /*원래는 strcpy(arg1, buf); 원래는 strcpy(arg2, p+1);였음.*/
	strcpy(arg1, buf+5); //buf에서 arg1으로 첫번째 변수값 복사. buf는 "arg1=5000&arg2=2" 전체문자열 가리킴. 시작점은 a. arg1=5000 이므로 숫자는 buf+5포인터에서 시작.
	strcpy(arg2, p+6); //p+6에서 arg2로 두번째 변수값 복사. p는 &위치 가리킴. & arg2=2이므로 p+6은 숫자 2를 가리킴.
	n1 = atoi(arg1); //arg1을 정수로 변환하여 n1에 저장. "5000"을 정수로 변환.
	n2 = atoi(arg2);
    }

    method = getenv("REQUEST_METHOD");

    /* Make the response body */
    // content인자에 html바디를 담음.
    sprintf(content, "Welcome to add.com: ");
    sprintf(content, "%sTHE Internet addition portal.\r\n<p>", content);
    sprintf(content, "%sThe answer is: %d + %d = %d\r\n<p>", content, n1, n2, n1 + n2);
    sprintf(content, "%sThanks for visiting!\r\n", content);
  
    /* Generate the HTTP response */
    // html head 출력
    printf("Connection: close\r\n");
    printf("Content-length: %d\r\n", (int)strlen(content));
    printf("Content-type: text/html\r\n\r\n");

    //html body 출력
    printf("%s", content);
    fflush(stdout);

    exit(0);
}
/* $end adder */
