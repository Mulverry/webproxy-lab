#include "csapp.h"
int cache_cnt = 0;

unsigned hash(char *s);
struct nlist *find(char *s);
char *hashstrdup(char *s);  // strdup 라는 이름은 사용할 수 없음(string.h에 이미 정의됨)
struct nlist *insert(char *name, char *defn);


/* 해시테이블  https://stackoverflow.com/questions/21850356/error-conflicting-types-for-strdup */
struct nlist { /* table entry: */
    struct nlist *next; /* 다음 항목을 가리킴.next entry in chain */
    char *name; /* defined name */
    char *defn; /* 대체텍스트 replacement text */
};

#define HASHSIZE 1049000
static struct nlist *hashtab[HASHSIZE]; /* pointer table */

/* hash: form hash value for string s
입력받은 문자열 s를 해시값으로 변환 */
unsigned hash(char *s)
{
    unsigned hashval;
    for (hashval = 0; *s != '\0'; s++)
      hashval = *s + 31 * hashval; //해시 값을 갱신. 31은 임의로 선택한 함수.
    return hashval % HASHSIZE;
}

/* find: look for s in hashtab
주어진 문자열 s를 해시 함수를 사용하여 해시 값을 계산한 후, 해당 해시 값에 저장된 연결 리스트를 검색 */
struct nlist *find(char *s)
{
    printf("looking for %s\n", s);
    struct nlist *np;
    for (np = hashtab[hash(s)]; np != NULL; np = np->next) //해시 테이블의 해당 버킷에서 시작하여 연결 리스트를 따라가며 문자열 s와 이름이 일치하는 항목을 찾음
        if (strcmp(s, np->name) == 0)
        {
          printf("[find func]cache hit!!!!!");
          return np; /* found 일치하는 항목을 찾으면 해당 항목을 반환 */
        }
    return NULL; /* not found */
}

/* insert: put (name, defn) in hashtab */
struct nlist *insert(char *name, char *defn) //defn = 삽입할 정의 문자열의 주소 가리키는 포인터
{
    printf("inserting name=%s defn=%s\n", name, defn);
    struct nlist *np;
    unsigned hashval;
    if ((np = find(name)) == NULL) { /* 동일한 이름이 없는 경우 */
        np = (struct nlist *) malloc(sizeof(*np)); //struct nlist의 새로운 인스턴스인 np를 할당
        if (np == NULL || (np->name = hashstrdup(name)) == NULL) //np의 name 필드를 hashstrdup(name)으로 할당하여 복제
          return NULL; 
        hashval = hash(name); //hash(name)을 사용하여 해시값을 계산
        np->next = hashtab[hashval]; //np의 next 필드를 hashtab[hashval]로 설정
        hashtab[hashval] = np; //hashtab[hashval]을 np로 업데이트하여 체인에 np를 추가
        cache_cnt++; 
    } else /* 동일한 이름이 있는 경우 */
        free((void *) np->defn); /*이전 정의를 해제 */

    if ((np->defn = hashstrdup(defn)) == NULL) //np의 defn 필드를 hashstrdup(defn)으로 할당하여 복제
       return NULL;
    return np;
}

char *hashstrdup(char *s) /* make a duplicate of s  문자열 s의 복제본을 만들어 반환하는 함수. */
{
    char *p;
    p = (char *) malloc(strlen(s)+1); /* strlen(s)를 사용하여 s의 길이를 계산하고, strlen(s)+1만큼의 메모리를 동적으로 할당. +1은 문자열의 끝에 널 문자('\0')를 저장하기 위해 추가 */
    if (p != NULL)
       strcpy(p, s); //s를 새로 할당된 메모리 p에 복사
    return p; //s 문자열의 복제본인 새로 할당된 메모리 블록의 주소를 반환
}