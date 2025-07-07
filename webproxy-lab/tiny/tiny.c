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
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

int main(int argc, char **argv)
{
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE]; // C에서는 문자열의 길이를 미리 정해진 크기의 배열에 저장해야 하니까, 안전하게 사용할 수 있도록 MAXLINE 처럼 상수를 만들어둔다.
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);
  // Tiny 웹 서버가 멈추지 않고 클라이언트의 요청을 기다리기 위해 필요한 구조다.
  // 웹 서버는 일반적으로 한 번 실행되면 계속 대기하다 요청이 오면 처리를 반복한다.
  while (1)
  {
    clientlen = sizeof(clientaddr);
    // 클라이언트가 접속하면 연결을 수락하고 통신할 소켓을 만든다.
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); // 여기서 생성된 소켓은 1:1 통신에만 사용된다.
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    // 그 요청을 처리한다.
    doit(connfd);
    // 요청 처리 후 소켓을 닫고 처음으로 돌아가서 다음 요청을 기다린다.
    Close(connfd); // Tiny 서버 자체는 종료되는 게 아니다. 닫는 것은 해당 클라이언트와의 연결만 종료하는 것이다.
  }
}

void doit(int fd) {
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;

  Rio_readinitb(&rio, fd); // 버퍼링된 구조체에 fd를 연결해서 버퍼링된 읽기 기능을 초기화함
  Rio_readlineb(&rio, buf, MAXLINE); // 한 줄 읽어서 buf에 저장함, 이 줄은 보통 HTTP 요청 첫 줄 GET /index.html HTTP/1.1이 됨
  printf("Request headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);
  // GET이 아니면 에러 발생 시킴
  if (strcasecmp(method, "GET")) {
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement thie method");
    return;
  }
  read_requesthdrs(&rio); // 이후에 오는 HTTP 헤더들을 줄 단위로 읽고 출력만 함

  is_static = parse_uri(uri, filename, cgiargs);
  // stat은 파일에 대한 정보를 얻기 위함 함수임
  // filename 경로의 파일 정보가 sbuf에 채워짐
  // 실패하면 파일이 존재하지 않거나 접근 불가능하다는 뜻
  if (stat(filename, &sbuf) < 0) {
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
    return;
  }

  if (is_static) {
    // S_ISREG는 이 파일이 일반 파일(regular file)인지 확인하는 매크로
    // 텍스트, html 같은 파일은 regular file임
    
    // S_ISUSR은 파일 소유자(read)의 권한이 있는지를 비트 연산으로 확인함
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
      return;
    }

    serve_static(fd, filename, sbuf.st_size);
  } else {
    // S_IXUSR은 실행 권한이 있는지를 비트 연산으로 확인하는 거임
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
      return;
    }

    serve_dynamic(fd, filename, cgiargs);
  }
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
  char buf[MAXLINE], body[MAXBUF];

  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

// 클라이언트가 보낸 요청에서 "요청 라인" "헤더"를 읽기 위한 함수임
void read_requesthdrs(rio_t *rp) {
  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE);
  while (strcmp(buf, "\r\n")) {
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  
  return;
}

int parse_uri(char *uri, char *filename, char *cgiargs) {
  char *ptr;

  // strstr은 문자열 안에 특정 문자열이 있는지 확인하는 함수다.
  // cgi-bin이 있으면 CGI 실행 파일 요청 즉, 동적 컨텐츠다.
  // CGI란? 실제로 그 파일을 실행시키고 그 실행 결과(출력)을 클라이언트에게 보내는 방식이다.
  if (!strstr(uri, "cgi-bin")) {
    // 함수들을 거치면 uri가 /index.html이라면
    // filename은 ./index.html이 됨
    strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri);

    // 만약 /로 끝나는 uri라면 default 파일을 지정해주는 거임
    if (uri[strlen(uri) - 1] == '/') {
      strcat(filename, "home.html");
    }

    return 1;
  } else {
    ptr = index(uri, '?');
    if (ptr) {
      strcpy(cgiargs, ptr + 1);
      *ptr = '\0';
    } else {
      strcpy(cgiargs, "");
    }
    strcpy(filename, ".");
    strcat(filename, uri);
    return 0;
  }
}

// 정적 콘텐츠(HTML, 텍스트, 이미지)를 클라이언트에게 HTTP 응답 형식으로 전송하는 함수임
void serve_static(int fd, char *filename, int filesize) {
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  get_filetype(filename, filetype);
  // HTTP 응답 헤더를 만들기
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  Rio_writen(fd, buf, strlen(buf)); // 응답 헤더를 문자열로 작성된걸 클라이언트에게 전송함

  printf("Response headers: \n");
  printf("%s", buf);

  srcfd = Open(filename, O_RDONLY, 0); // Open으로 해당 파일을 읽기 전용으로 염
  srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); // Mmap로 파일 전체를 가상 메모리(주소 공간)에 매핑함, 즉 파일 내용을 읽지 않고도 포인터처럼 접근 가능함, 더 빠르고 효율적임
  Close(srcfd);
  Rio_writen(fd, srcp, filesize); // 매핑된 메모리 내용을 클라이언트에게 전송
  Munmap(srcp, filesize); // 가상 메모리 해제
}

// 여기서 말하는 동적 요청은 Tiny 웹 서버가 특정 프로그램(CGI 스크립트)을 실행해서 그 결과를 클라이언트에 보내는 요청이다.
// 예를 들어 adder.cgi?1&2 요청이 오면 adder.cgi 프로그램을 실행하고 그 안에서 1, 2를 받아서 처리한 뒤 결과를 HTML 형식으로 출력한다.
/*
→ Tiny는 adder.cgi라는 프로그램을 실행하고
→ QUERY_STRING=1&2를 환경변수로 넣어줌
→ 프로그램 안에서 getenv("QUERY_STRING") 해서 값 파싱 후 계산
→ 결과를 printf() 하면 그대로 웹 브라우저에 출력됨
*/
void serve_dynamic(int fd, char *filename, char *cgiargs) {
  char buf[MAXBUF], *emptylist[] = { NULL };

  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  // 자식 프로세스에서 CGI 프로그램을 실행한다.
  // 자식 프로세스 생성
  if (Fork() == 0) {
    // 환경 변수 설정
    // CGI 프로그램이 getenv("QUERY_STING")으로 GET 파라미터를 읽을 수 있게 해줌
    // 예: adder.cgi?1&2 -> cgiargs = "1&2" -> 환경 변수 QUERY_STRING=1&2
    setenv("QUERY_STRING", cgiargs, 1);

    // 자식 프로세스의 표준 출력(STDOUT)을 소켓(fd)으로 리디렉션
    // 즉 CGI 프로그램이 printf() 하면 그 출력이 브라우저로 바로 전송됨
    Dup2(fd, STDOUT_FILENO);

    // CGI 프로그램 실행 (예: adder.cgi)
    // 해당 실행 파일을 그대로 실행함(emptylist는 argv 없음)
    Execve(filename, emptylist, environ);
  }
  
  // 부모 프로세스는 자식 프로세스가 끝날 때까지 기다림
  Wait(NULL);
}

void get_filetype(char *filename, char *filetype) {
  if (strstr(filename, ".html")) {
    strcpy(filetype, "text/html");
  } else if (strstr(filename, ".gif")) {
    strcpy(filetype, "image/gif");
  } else if (strstr(filename, ".png")) {
    strcpy(filetype, "image/png");
  } else if (strstr(filename, ".jpg")) {
    strcpy(filetype, "image/jpeg");
  } else {
    strcpy(filetype, "text/plain");
  }
}