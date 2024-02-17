#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
  int t = 0;  // buffer
  int p[2];
  if (pipe(p) < 0) {
    printf("pipe error");
    exit(-1);
  }
  int fret = fork();
  if (fret == 0) {
    // sub process
    // read from ping
    read(p[0], &t, 1);  // read only for blocking
    printf("%d: received ping\n", getpid());
    write(p[1], &t, 1);  // write to pong
  } else if (fret > 0) {
    // parent process
    write(p[1], &t, 1);  // write to ping
    sleep(1);
    read(p[0], &t, 1);   // read from pong
    printf("%d: received pong\n", getpid());
  } else {
    printf("fork error");
    exit(-1);
  }
  // pipes will be automatically closed when process exits
  exit(0);
}