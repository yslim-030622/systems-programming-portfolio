/*
 * Optional scheduler instrumentation hook.
 *
 * The default implementation is intentionally empty so the kernel can build
 * without tracing enabled. Replacing this function makes it easy to record
 * scheduler decisions during experiments.
 */

#include "param.h"
#include "types.h"
#include "defs.h"
#include "mmu.h"
#include "proc.h"

void log_sched(struct proc* p) {}
