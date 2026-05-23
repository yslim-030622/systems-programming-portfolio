#include "types.h"
#include "stat.h"
#include "user.h"


int
main(int argc, char *argv[]){
    if (argc != 2){
        printf(1, "usage : work <10 milliseconds>\n");
        return -1;
    }
    int pid = getpid();
    int to_spin = atoi(argv[1]);
    printf(1, "XV6_TEST: pid %d started\n", pid);
    spinwait(to_spin);
    printf(1, "XV6_TEST: pid %d finished\n", pid);
    exit();
}