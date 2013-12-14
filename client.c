

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <ncurses.h>
#include <pthread.h>
#include <time.h>
#include <stdarg.h>
#include <fcntl.h>


#define ASSERT( x, f ) \
  if( (x) == -1 ) { \
    /*perror(f);*/ \
    wlog( "error(%d): %s %s\n", errno, f, strerror(errno) ); \
    exit(-1); \
  }

static char screen_buffer[1024];
static char input_buffer[1024];
static char sock_buffer[1024];

static int input_pos  = 0;
static pthread_t listen_thread_h = 0;
static int run_loop   = 1;
static FILE *log_file = NULL;
static int net_sock   = 0;

struct _S_HISTORY {
  char *line;
  int u;
  struct _S_HISTORY *next;
};

struct _S_HISTORY *history = NULL;
int history_size = 0;

void wlog( char* fmt, ... ) {

  va_list al;

  struct tm ts;

  time_t now = time(NULL);

  gmtime_r( (const time_t *)&now, &ts );

  fprintf( 
    log_file, 
    "%02d-%02d-%d@%02d-%02d-%02d: ", 
    ts.tm_year + 1900, 
    ts.tm_mon, 
    ts.tm_mday, 
    ts.tm_hour,
    ts.tm_min,
    ts.tm_sec
  );

  //TODO: timestamp
  va_start( al, fmt ); 
  vfprintf( log_file, fmt, al ); 
  va_end( al );

  fflush( log_file );
}

void cleanup() {
  
  int ret;

  if(listen_thread_h) {
    ret = pthread_join( listen_thread_h, NULL );
    ASSERT( ret, "pthread_join()" );
  }

  if(net_sock) {
    close(net_sock);
  }

  endwin(); 

  if(log_file) {
    wlog("end of log\n");
    fflush(log_file);
    fclose(log_file);
  }
}


void redraw() {

  struct _S_HISTORY *hp = history;
  int i;

  clear();

  curs_set(0);
  move( 0, 0 );

  memset( screen_buffer, 0, sizeof(screen_buffer));
  
  attrset( A_REVERSE );

  for(i = 0; i < COLS; i++)
    addch( ' ');

  snprintf( screen_buffer, 1024, "state: connected" );
  mvaddstr( 0, 0, screen_buffer );
  attroff( A_REVERSE );

  i = LINES-2;
  while(hp) {

    mvaddstr( i, 0, hp->line );

    hp = hp->next;
    i--;
  }

  mvaddstr( LINES-1, 0, "> " );
  addstr( input_buffer );


  curs_set(1);

  refresh();
}

void push_history( char* s, int len, int u ) {

  struct _S_HISTORY *hp;
  struct _S_HISTORY *prev = history;
  //int i;

  //TODO: cut off, stop massive memory leak
   

  hp = (struct _S_HISTORY *)calloc( 1, sizeof(struct _S_HISTORY));
  if(!hp) return;

  hp->line = (char*)calloc( len + 1, sizeof(char));
  if(!hp->line) {
    free(hp);
    return;
  }

  memcpy( hp->line, s, len+1 );

  if(prev)
    hp->next = prev;
  
  history_size++;

  history = hp;
}

void* listen_thread( void* d ) {

  struct timespec ts;
  int ret;

  ts.tv_sec  = 0;
  ts.tv_nsec = 500 * 1000 * 1000;

  wlog("listen_thread: started\n");

  while(run_loop) {

    memset( sock_buffer, 0, sizeof(sock_buffer) );

    ret = read( net_sock, sock_buffer, 1024 );
    
    if( ret == -1 && (EAGAIN == errno || EWOULDBLOCK == errno )) {
      nanosleep( &ts, NULL );
      continue;
    }
    ASSERT( ret, "read()" );

    if( ret == 0 ) {
      wlog( "socket went away\n");
      exit(0);
    }

    push_history( sock_buffer, ret, 1 );
    redraw();
  }

  wlog("listen_thread: has stopped\n");

  return NULL;
}

void send_input() {
  int ret;

  ret = send( net_sock, input_buffer, input_pos + 1, 0);
  ASSERT( ret, "send()" ); 
}

void do_user_input() {

  int i;
  int ch = getch();

  switch(ch) {
    case 0x03:
    case 0x04:
      wlog( "got ^C, signalling stop\n"); run_loop = 0;
      break;

    case 0x0d:
      if(!input_pos) break;

      move( LINES -1, 2 );
      for(i = 0; i < input_pos; i++)
        addch(' ');

      send_input();

      push_history( input_buffer, input_pos, 0 );
        
      memset( input_buffer, 0, sizeof(input_buffer) );
      input_pos = 0; 
      break;

    case KEY_BACKSPACE:
    case 127:
      if(input_pos > 0) {
        mvaddch( LINES -1, input_pos+1, ' ');
        input_buffer[--input_pos] = 0;
      }
      break;

    default:
      if(input_pos < 200) {
        input_buffer[input_pos++] = ch;
      }
      break;

  }
}

void handle_sigwinch(int i) {

  redraw();
}

void init_socket(char *host, unsigned short port) {

  struct sockaddr_in addr;
  int ret;
  int flags;

  wlog( "connecting to %s:%d", host, port );

  net_sock = socket( AF_INET, SOCK_STREAM, 0 );
  ASSERT( net_sock, "socket()" );

  memset( &addr, 0, sizeof(addr) );
  addr.sin_family = AF_INET;
  addr.sin_port   = htons(port);
  addr.sin_addr.s_addr = inet_addr(host);

  ret = connect( net_sock, (struct sockaddr *)&addr, sizeof(addr) );
  ASSERT( ret, "connect()" ); 

  ret = pthread_create( &listen_thread_h, NULL, listen_thread, NULL );
  ASSERT( ret, "pthread_create()" );

  ret = fcntl(net_sock, F_GETFL);
  ASSERT( ret, "fcntl()" );

  flags = ret | O_NONBLOCK;

  ret = fcntl(net_sock, F_SETFL, flags);
  ASSERT( ret, "fcntl()" );

}

void init_curses() {

  initscr();

  keypad(stdscr, TRUE);
  nonl();
  //cbreak();
  raw();
  noecho();

  redraw();
}


int main(int argc, char** argv) {

  //int ret;
  //struct sigaction sa;

/*   memset( &sa, 0, sizeof(sa) );
 *   sa.sa_handler = handle_sigint;
 *   ret = sigaction( SIGINT, &sa, NULL);
 *   ASSERT( ret, "sigaction()" );
 */

  memset( input_buffer, 0, sizeof(input_buffer) );

  atexit(cleanup);

  log_file = fopen( "output.log", "a+" );
  if( !log_file ) {
    perror("fopen()");
    return -1;
  }

  wlog("starting up\n");

  init_curses();
  init_socket( "127.0.0.1", 6753 );


  wlog("entering main loop\n");
  while(run_loop) {
    do_user_input();
    redraw();
  }
  wlog("main loop stopped\n");


  return 0;

}


