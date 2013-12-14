
// using epoll. i hope.

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <signal.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/epoll.h>
#include <stdarg.h>
#include <time.h>

#define MAX_EVENTS 10

int socketpool[100];

struct epoll_event ev, events[MAX_EVENTS];
int listen_sock = 0;
int epollfd = 0;

char buffer[1024];


int run_loop = 1;

#define ASSERT( x, f ) \
  if( (x) == -1 ) { \
    perror(f); \
    exit(-1); \
  }

void signal_handler(int sig) {

  printf("got SIGINT\n");
  run_loop = 0; 
}

void cleanup() {
  int i;

  printf("cleanup\n");

  for(i = 0; i < 100; i++) {
    if(socketpool[i]) close(socketpool[i]);
  }

  if(listen_sock) close(listen_sock);
  if(epollfd) close(epollfd);
}

void do_socket_read( int fd ) {

  int i;
  int ret;

  ret = read(fd, buffer, sizeof(buffer));
  ASSERT( ret, "read()" );

  if( ret == 0 ) {

    printf("socket %d went away\n", fd );

    for(i = 0; i < 100; i++) {
      if( socketpool[i] == 0 || socketpool[i] != fd ) continue;

      socketpool[i] = 0;
      break;
    }

    return;
  }

  for(i = 0; i < 100; i++) {
    if(socketpool[i] == 0 || socketpool[i] == fd) continue;

    write( socketpool[i], buffer, ret );

  }

  memset( buffer, 0, sizeof(buffer) );
}

void do_new_connection() {

  struct sockaddr_in local;
  socklen_t addrlen;
  int i;
  int conn_sock;
  int ret;

  addrlen = sizeof(local);
  conn_sock = accept(listen_sock, (struct sockaddr *) &local, &addrlen);
  ASSERT( conn_sock, "accept()" );

  //setnonblocking(conn_sock);

  ev.events  = EPOLLIN | EPOLLET;
  ev.data.fd = conn_sock;

  ret = epoll_ctl(epollfd, EPOLL_CTL_ADD, conn_sock, &ev);
  ASSERT( ret, "epoll_ctl()");

  for(i = 0; i < 100; i++) {
    if(socketpool[i]) continue;
    printf("socket added %d\n", conn_sock);
    socketpool[i] = conn_sock;
    break;
  }

}

int main(int argc, char** asgv) {

  int ret;
  int nfds, n;
  struct sigaction sa; 
  struct sockaddr_in addr;

  memset( socketpool, 0, sizeof(socketpool) );
  memset( buffer, 0, sizeof(buffer) );

  atexit(cleanup);

  memset( &sa, 0, sizeof(sa) );
  sa.sa_handler = signal_handler;

  ret = sigaction(SIGINT, &sa, NULL);
  ASSERT( ret, "sigaction()" );

  listen_sock = socket( AF_INET,  SOCK_STREAM, 0 );
  ASSERT( listen_sock, "socket()" );

  memset( &addr, 0, sizeof(addr) );
  addr.sin_family = AF_INET;
  addr.sin_port   = htons( 6753 );
  addr.sin_addr.s_addr = inet_addr( "127.0.0.1" );
  
  ret = bind( listen_sock, (struct sockaddr *)&addr, sizeof(addr) );
  ASSERT( ret, "bind()" );

  ret = listen( listen_sock, 5 );
  ASSERT( ret, "isten()" );

  epollfd = epoll_create(MAX_EVENTS);
  ASSERT( epollfd, "epoll_create()" );

  ev.events  = EPOLLIN;
  ev.data.fd = listen_sock;

  ret = epoll_ctl(epollfd, EPOLL_CTL_ADD, listen_sock, &ev);
  ASSERT( ret, "epoll_ctl()" );

  printf("entering main loop\n");

  while(run_loop) {

    nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
    ASSERT( nfds, "epoll_wait()" );

    for (n = 0; n < nfds; ++n) {

      if (events[n].data.fd == listen_sock) {

        do_new_connection();

      } else {

        do_socket_read( events[n].data.fd ); 
      }
    } 

  }

  printf("main loop stopped\n");


  return 0;

}

#if 0

void main_loop() {

  char buffer[1024];
  int ret;
  int newsock;
  int i;
  int j;
  int max_socket;
  fd_set rfds;
  struct timeval tv;
  struct sockaddr_in addr;

  ret = listen( sockfd, 5 );
  if(ret == -1) {
    perror("listen");
    exit(-1);
  }

  max_socket = sockfd;

  FD_ZERO(&rfds);
  FD_SET(sockfd, &rfds);

  memset(&tv, 0, sizeof(tv));
  tv.tv_usec = 100 * 1000;

  while(run_loop) {

    ret = select(max_socket, &rfds, NULL, NULL, &tv);
    if(!ret) continue;

    if(FD_ISSET(sockfd, &rfds)) {

      j = sizeof(addr);
      newsock = accept(sockfd, (struct sockaddr *)&addr, &j);
      if(newsock == -1) {
        perror("accepting");
      }

      FD_SET( newsock, &rfds );
      if(newsock > max_socket) max_socket = newsock;

      printf("new client %d\n", newsock);

      continue;
    }

    for(i = 0; i < max_socket; i++) {
      if(!FD_ISSET(i, &rfds)) continue;

      ret = read( i, buffer, sizeof(buffer));
      if(ret == 0) {

        printf("client went away\n");

        FD_CLR(i, &rfds);
        max_socket = sockfd;
        for(j = sockfd; j < FD_SETSIZE; j++) {
          if(!FD_ISSET( j, &rfds )) continue;
          if(j > max_socket) max_socket = j;
        }

        continue; 
      }

      printf("Just read (%d): '%s'\n", i, buffer);
    }
  }

  printf("main loop stopped\n");
}

void signal_handler( int t ) {
  printf("sigint\n");
  run_loop = 0;
}

void cleanup() {
  printf("cleanup\n");
  if(sockfd) close(sockfd);
}

void setup() {

  int ret;

  struct sigaction act;
  struct sockaddr_in addr;

  act.sa_handler = signal_handler;
  sigaction( SIGINT, &act, NULL );

  sockfd = socket( AF_INET, SOCK_STREAM, 0);
  if(sockfd == -1) {
    perror("creating socket");
    exit(-1);
  }

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(1729);
  addr.sin_addr.s_addr = inet_addr("127.0.0.1");

  ret = bind( sockfd, (struct sockaddr *)&addr, sizeof(addr));
  if(ret == -1) {
    perror("bind");
    exit(-1);
  }
}

int main(int argc, char ** argv) {

  atexit(cleanup);

  setup();

  main_loop(); 

  return 0;
}

#endif


