#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

const int nonblock = 1;


void yieldtest(void) {
    printf("yield test\n");

    for (int i = 0; i < 3; i++) {
        int pid = fork();
        if (pid == 0) {
            char name[16];
            memset(name, 0, 16);
            char *base_name = "yield_child_";
            memcpy(name, base_name, strlen(base_name));
            char idx = '0' + i;
            memcpy(name + strlen(base_name), &idx, 1);
            rename(name);
            int sum = 0;
            // printf("switch to parent\n");
            for (int i = 0; i < 1000; i++) {
                sum++;
            }
            // printf("parent finished, sum %d\n", sum);
            exit(0);
        } else {
            // printf("switch to child\n");
            // yield();
            // printf("child finished\n");
        }
    }
    printf("switch to child\n");
    yield();
    printf("child finished\n");
    for (int i = 0; i < 3; i++) {
        wait(0, 0);
    }
    printf("yield test OK\n");
}

int main(int argc, char *argv[]) {
    yieldtest();
    exit(0);
}