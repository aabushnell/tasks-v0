// executor.cpp — per-core worker: pop desc, resolve borrows, run kernel,
// post timestamped completion. Spins; eventfd wakeup comes later.
#include "executor.h"

#include <algorithm>
#include <sched.h>
#include <unistd.h>

#include "common.h"
#include "kernels.h"

[[noreturn]] void executor_main(Arena *A, int idx) {
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(idx % std::max(1L, sysconf(_SC_NPROCESSORS_ONLN)), &set);
  sched_setaffinity(0, sizeof(set), &set); // best effort

  auto &ring = A->task_ring[idx];
  for (;;) {
    if (A->shutdown.load(std::memory_order_acquire))
      _exit(0);
    TaskDesc d;
    if (!ring.pop(d)) {
      sched_yield();
      continue;
    }

    CompletionRec rec{
        .id = d.id, .enqueue_ts = d.t_enqueue, .pop_ts = now_ns()};
    rec.start_ts = now_ns();
    Ctx c{d, *A};
    KERNELS[d.kernel](c);
    rec.end_ts = now_ns();
    rec.status = 0;
    A->cmpl.push(rec);
  }
}
