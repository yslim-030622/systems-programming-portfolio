#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

// Remember who just called give_cpu(); defined in proc.c
extern struct proc *skip_target;

int
sys_fork(void)
{
  return fork();
}

int
sys_clone(void)
{
  int fn, stack, arg;
  argint(0, &fn);
  argint(1, &stack);
  argint(2, &arg);
  return clone((void (*)(void*))fn, (void*)stack, (void*)arg);
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;
  if (argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if (argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if (growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if (argint(0, &n) < 0)
    return -1;
  if (n == 0) {
    // Sleeping for 0 is just a yield.
    yield();
    return 0;
  }
  acquire(&tickslock);
  ticks0 = ticks;
  myproc()->sleepticks = n;
  while (ticks - ticks0 < (uint)n) {
    if (myproc()->killed) {
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  myproc()->sleepticks = -1;
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred since start.
int
sys_uptime(void)
{
  uint xticks;
  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

/* -------------------------
 * STCF project syscalls
 * ------------------------- */

// remain(int new_time)
// If new_time <= 0: return -1 (do nothing).
// Otherwise set t_remain. If it *increases*, yield so a shorter job can run.
int
sys_remain(void)
{
  int new_time;
  struct proc *p = myproc();

  if (argint(0, &new_time) < 0)
    return -1;

  if (new_time <= 0)
    return -1;

  int old = p->t_remain;
  p->t_remain = new_time;

  if (new_time > old)
    yield();

  return 0;
}

// exec2(int time_to_complete, char *path, char **argv)
// Validate time_to_complete > 0, copy argv like xv6 exec,
// set t_remain, and if it *increases*, yield before exec.
int
sys_exec2(void)
{
  int time_to_complete;
  char *path;
  uint uargv;     // user pointer to argv[]
  char *argv[MAXARG];
  int i;
  uint uarg;

  if (argint(0, &time_to_complete) < 0)
    return -1;
  if (time_to_complete <= 0)
    return -1;

  // xv6 style parsing: path at arg #1, argv user pointer at arg #2
  if (argstr(1, &path) < 0)
    return -1;
  if (argint(2, (int*)&uargv) < 0)
    return -1;

  // Build kernel-side argv[] from user pointers
  memset(argv, 0, sizeof(argv));
  for (i = 0; ; i++) {
    if (i >= MAXARG)
      return -1;
    if (fetchint(uargv + 4*i, (int*)&uarg) < 0)
      return -1;
    if (uarg == 0) {
      argv[i] = 0;
      break;
    }
    if (fetchstr(uarg, &argv[i]) < 0)
      return -1;
  }

  struct proc *p = myproc();
  int old = p->t_remain;
  p->t_remain = time_to_complete;

  if (time_to_complete > old)
    yield();

  return exec(path, argv);
}

// give_cpu(): ask the scheduler to skip this process once, then yield.
// If any other runnable process exists, it will run next.
int
sys_give_cpu(void)
{
  struct proc *p = myproc();
  p->skip_once = 1;      // one-shot skip in the picker
  skip_target = p;       // remember who asked, so we avoid it if possible
  yield();
  return 0;
}