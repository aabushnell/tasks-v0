// kernels.hpp — executor-side kernel ABI (stand-in for dlopen'd envs).
// Later this becomes the header an environment dylib compiles against:
// Ctx + view() are the borrow-resolution interface, KERNELS becomes the
// per-env registered table instead of one global static array.
#pragma once

#include "common.h"

struct Ctx {
  const TaskSpec &d;
  ArenaHeader &A;
};
using KernelFn = void (*)(Ctx &);

// Resolve the i-th borrow to a pointer. Works in any process because every
// process shares the arena's virtual mapping.
inline int64_t *view(Ctx &c, int i) {
  const Depend &b = c.d.dependencies[i];
  return reinterpret_cast<int64_t *>(reinterpret_cast<char *>(&c.A) +
                                     c.A.blocks[b.id].offset + b.offset);
}

// Kernels derive their extent from borrow.len — no compile-time block size.
extern KernelFn KERNELS[];

enum DemoKernel : KernelID { K_INIT = 0, K_STEP = 1, K_SUM = 2 };
