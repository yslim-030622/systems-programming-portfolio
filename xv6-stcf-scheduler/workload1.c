#include "types.h"
#include "stat.h"
#include "user.h"


int main(){
    printf(1, "workload 1 started\n");
    int pid = fork();

    if (pid == 0){
        char *argv[2]={"workload2", 0};
        exec("/workload2", argv);
    }else{
        spinwait(100);
    }
    printf(1, "workload 1 fin\n");
    wait();
    exit();
}