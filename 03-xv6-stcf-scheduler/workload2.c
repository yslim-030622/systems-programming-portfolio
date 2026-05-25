#include "types.h"
#include "stat.h"
#include "user.h"


int main(){
    printf(1, "workload 2 started\n");
    spinwait(50);
    printf(1, "workload 2 fin\n");
    exit();
}