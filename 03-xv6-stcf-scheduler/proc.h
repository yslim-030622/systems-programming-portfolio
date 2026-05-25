#ifndef PROC
#define PROC

#define MAXPROCNAMELEN 16
#include "spinlock.h"

// STCF: initial remaining time for the very first process.
// We use a signed 32-bit max so normal subtraction will not overflow easily.
#define STCF_INIT_REMAIN 0x7fffffff

// Per-CPU state
struct cpu {
  uchar apicid;                // Local APIC ID
  struct context *scheduler;   // swtch() here to enter scheduler
  struct taskstate ts;         // Used by x86 to find stack for interrupt
  struct segdesc gdt[NSEGS];   // x86 global descriptor table
  volatile uint started;       // Has the CPU started?
  int ncli;                    // Depth of pushcli nesting.
  int intena;                  // Were interrupts enabled before pushcli?
  struct proc *proc;           // The process running on this cpu or null
};

extern struct cpu cpus[NCPU];
extern int ncpu;

//PAGEBREAK: 17
// Saved registers for kernel context switches.
struct context {
  uint edi;
  uint esi;
  uint ebx;
  uint ebp;
  uint eip;
};

enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// Per-process state
struct proc {
  uint sz;                     // Size of process memory (bytes)
  pde_t* pgdir;                // Page table
  char *kstack;                // Bottom of kernel stack for this process
  enum procstate state;        // Process state
  int pid;                     // Process ID
  struct proc *parent;         // Parent process
  struct trapframe *tf;        // Trap frame for current syscall
  struct context *context;     // swtch() here to run process
  void *chan;                  // If non-zero, sleeping on chan
  int killed;                  // If non-zero, have been killed
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[MAXPROCNAMELEN];   // Process name (debugging)
  int nclone;                  // Number of clone calls on this proc
  int sleepticks;              // Number of ticks left the process should sleep for

  // ---- STCF fields (added) ----
  // Remaining (expected) time to completion for this process.
  // This is signed and may go negative due to coarse tick accounting; that is OK.
  int t_remain;

  // The global tick when this process last started running.
  // We subtract elapsed ticks from t_remain when the process yields/sleeps/exits.
  int t_last_scheduled;

  // One-shot flag for give_cpu(): if set, scheduler skips this proc once.
  // If no other runnable proc exists, it can still be chosen.
  int skip_once;
};

typedef struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} Ptable;

// Process memory is laid out contiguously, low addresses first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap

#endif
