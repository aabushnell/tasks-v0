// kernels.cpp — demo kernels. Proxy for code that will eventually live in a
// nix-store-path environment and be dlopen'd per executor.
#include "kernels.h"

static void k_init(Ctx &c) {
  int64_t *o = view(c, 0);
  size_t n = c.d.borrows[0].len / sizeof(int64_t);
  for (size_t i = 0; i < n; i++)
    o[i] = (int64_t)(((uint32_t)c.d.arg0 * 2654435761u + i) % 1000);
}

static void k_step(Ctx &c) { // out = (l + 2*m + r) / 4 over the borrow extent
  const int64_t *l = view(c, 0), *m = view(c, 1), *r = view(c, 2);
  int64_t *o = view(c, 3);
  size_t n = c.d.borrows[3].len / sizeof(int64_t);
  for (size_t i = 0; i < n; i++)
    o[i] = (l[i] + 2 * m[i] + r[i]) / 4;
}

static void k_sum(Ctx &c) { // sum all read borrows -> single scalar write
  int64_t s = 0;
  for (int b = 0; b + 1 < c.d.n_borrows; b++) {
    const int64_t *p = view(c, b);
    size_t n = c.d.borrows[b].len / sizeof(int64_t);
    for (size_t i = 0; i < n; i++)
      s += p[i];
  }
  *view(c, c.d.n_borrows - 1) = s;
}

KernelFn KERNELS[] = {k_init, k_step, k_sum};
