#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

void doit(int fd);

int main(int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1)
  {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);
    Close(connfd);
  }
}

void doit(int connfd) {
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char hostname[MAXLINE], pathname[MAXLINE], port[MAXLINE];
  rio_t client_rio, server_rio;
  int serverfd;

  Rio_readinitb(&client_rio, connfd);

  if (!Rio_readlineb(&client_rio, buf, MAXLINE)) {
    return;
  }

  sscanf(buf, "%s %s %s", method, uri, version);
  if (strcasecmp(method, "GET")) {
    printf("Only GET supported.\n");
    return;
  }

  if (sscanf(uri, "http://%[^:/]:%[^/]%s", hostname, port, pathname) != 3) {
    if (sscanf(uri, "http://%[^/]%s", hostname, pathname) != 2) {
      printf("Invalid URI\n");
      return;
    }

    strcpy(port, "80");
  }

  serverfd = Open_clientfd(hostname, port);
  if (serverfd < 0) {
    printf("Connection to server failed\n");
    return;
  }
  Rio_readinitb(&server_rio, serverfd);

  sprintf(buf, "GET %s HTTP/1.0\r\n", pathname);
  Rio_writen(serverfd, buf, strlen(buf));

  while (Rio_readlineb(&client_rio, buf, MAXLINE) > 0) {
    if (strcmp(buf, "\r\n") == 0) {
      break;
    }

    if (strncasecmp(buf, "Connection:", 11) == 0) continue;
    if (strncasecmp(buf, "Proxy-Connection:", 17) == 0) continue;
    if (strncasecmp(buf, "User-Agent:", 11) == 0) continue;
    Rio_writen(serverfd, buf, strlen(buf));
  }

  sprintf(buf, "Connection: close\r\n");
  Rio_writen(serverfd, buf, strlen(buf));
  sprintf(buf, "Proxy-Connection: close\r\n");
  Rio_writen(serverfd, buf, strlen(buf));
  sprintf(buf, "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3)\r\n\r\n");
  Rio_writen(serverfd, buf, strlen(buf));

  ssize_t n;
  while ((n = Rio_readlineb(&server_rio, buf, MAXLINE)) > 0) {
    Rio_writen(connfd, buf, n);
  }

  Close(serverfd);
}