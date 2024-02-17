#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
  int p_ping[2], p_pong[2];
  int t = 0;  // buffer
  int iret = pipe(p_ping), oret = pipe(p_pong);
  if (iret < 0 || oret < 0) {
    printf("pipe error");
    exit(-1);
  }
  int fret = fork();
  if (fret == 0) {
    // sub process
    close(p_ping[1]);
    close(p_pong[0]);
    // read from ping
    read(p_ping[0], &t, 1);  // read only for blocking
    printf("%d: received ping\n", getpid());
    write(p_pong[1], &t, 1);  // write to pong
  } else if (fret > 0) {
    // parent process
    close(p_ping[0]);
    close(p_pong[1]);
    write(p_ping[1], &t, 1);  // write to ping
    read(p_pong[0], &t, 1);   // read from pong
    printf("%d: received pong\n", getpid());
  } else {
    printf("fork error");
    exit(-1);
  }
  // pipes will be automatically closed when process exits
  exit(0);
}