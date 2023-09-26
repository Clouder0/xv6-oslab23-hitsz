#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

const int nonblock = 1;

void exittest(void) {
  printf("exit test\n");

  for (int i = 0; i < 3; i++) {
    int pid = fork();
    if (pid == 0) {
      char name[16] = "child";
      char idx = '0' + i;
      int len = strlen(name);
      memmove(name + len, &idx, 1);
      name[len + 1] = '\0';
      rename(name);
      sleep(10);
      exit(0);
    }
  }

  sleep(1);
}

int main(int argc, char *argv[]) {
  exittest();
  exit(0);
}