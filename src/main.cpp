// main.cpp — demo driver: arena setup, executor fork, demo DAG, serial
// reference check, telemetry. Stands in for the future TCP client: tasks
// are built in-process but flow through the real Supervisor submit path.
//
// Build: g++ -std=c++20 -O2 -Wall -o minimal_runtime \
//          main.cpp supervisor.cpp executor.cpp kernels.cpp
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "common.h"
#include "executor.h"
#include "kernels.h"
#include "supervisor.h"

// ---- demo workload params ----
constexpr int NB = 16;         // blocks per array
constexpr size_t BLOCK = 4096; // int64s per block (32 KiB)

static TaskSpec mk(KernelID k, std::vector<Depend> b, uint64_t a0 = 0) {
  if (b.size() > INLINE_DEPS) {
    fprintf(stderr, "mk: %zu borrows > inline cap\n", b.size());
    _exit(2);
  }
  TaskSpec d{};
  d.kernel = k;
  d.arg0 = a0;
  d.n_borrows = (uint16_t)b.size();
  memcpy(d.dependencies, b.data(), b.size() * sizeof(Depend));
  return d;
}

// stencil borrows: read {left, mid, right} (clamped), write w
static std::vector<Depend> tri(const std::vector<BlockID> &arr, int i,
                               BlockID w) {
  return {
      {arr[std::max(0, i - 1)], 0, BLOCK * 8, Read},
      {arr[i], 0, BLOCK * 8, Read},
      {arr[std::min(NB - 1, i + 1)], 0, BLOCK * 8, Read},
      {w, 0, BLOCK * 8, Write},
  };
}

int main() {
  ArenaHeader *A = (ArenaHeader *)mmap(nullptr, ARENA_SIZE, PROT_READ | PROT_WRITE,
                           MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  if (A == MAP_FAILED) {
    perror("mmap");
    return 1;
  }
  new (A) ArenaHeader();

  long hw = sysconf(_SC_NPROCESSORS_ONLN);
  int nexec = (int)std::min<long>(std::max(1L, hw), MAX_EXEC);
  A->n_exec = nexec;

  Supervisor sup(A, nexec);

  // regions: X[0..NB), Y[NB..2NB), scalar — before fork (fixed table)
  std::vector<BlockID> xr(NB), yr(NB);
  for (int i = 0; i < NB; i++)
    xr[i] = sup.add_region(BLOCK * 8);
  for (int i = 0; i < NB; i++)
    yr[i] = sup.add_region(BLOCK * 8);
  const BlockID SCALAR = sup.add_region(8);

  for (int e = 0; e < nexec; e++)
    if (fork() == 0)
      executor_main(A, e);

  // ---- demo DAG: init X; step X->Y; step Y->X (WAR/WAW wave); sum X ----
  for (int i = 0; i < NB; i++)
    sup.submit_task(mk(K_INIT, {{xr[i], 0, BLOCK * 8, Write}}, i));
  for (int i = 0; i < NB; i++)
    sup.submit_task(mk(K_STEP, tri(xr, i, yr[i])));
  for (int i = 0; i < NB; i++)
    sup.submit_task(mk(K_STEP, tri(yr, i, xr[i])));
  {
    std::vector<Depend> b;
    for (int i = 0; i < NB; i++)
      b.push_back({xr[i], 0, BLOCK * 8, Read});
    b.push_back({SCALAR, 0, 8, Write});
    sup.submit_task(mk(K_SUM, b));
  }

  // serial reference
  std::vector<std::vector<int64_t>> rx(NB, std::vector<int64_t>(BLOCK)),
      ry(NB, std::vector<int64_t>(BLOCK));
  for (int i = 0; i < NB; i++)
    for (size_t j = 0; j < BLOCK; j++)
      rx[i][j] = (int64_t)((i * 2654435761u + j) % 1000);
  auto step = [&](std::vector<std::vector<int64_t>> &src,
                  std::vector<std::vector<int64_t>> &dst) {
    for (int i = 0; i < NB; i++)
      for (size_t j = 0; j < BLOCK; j++) {
        int64_t l = src[std::max(0, i - 1)][j], m = src[i][j],
                r = src[std::min(NB - 1, i + 1)][j];
        dst[i][j] = (l + 2 * m + r) / 4;
      }
  };
  step(rx, ry);
  step(ry, rx);
  int64_t ref = 0;
  for (int i = 0; i < NB; i++)
    for (size_t j = 0; j < BLOCK; j++)
      ref += rx[i][j];

  uint64_t t0 = now_ns();
  sup.run_until_idle();
  uint64_t makespan = now_ns() - t0;

  A->shutdown.store(1, std::memory_order_release);
  while (wait(nullptr) > 0) {
  }

  // telemetry
  std::vector<uint64_t> lat, run;
  for (auto &r : sup.completions()) {
    lat.push_back(r.pop_ts - r.enqueue_ts); // NOTE: includes queue wait
    run.push_back(r.end_ts - r.start_ts);
  }
  std::sort(lat.begin(), lat.end());
  std::sort(run.begin(), run.end());
  auto pct = [](std::vector<uint64_t> &v, double p) {
    return v[(size_t)(p * (v.size() - 1))];
  };
  int64_t got = *(int64_t *)((char *)A + A->blocks[SCALAR].offset);

  printf("executors            : %d\n", nexec);
  printf("tasks completed      : %zu / %zu\n", sup.completions().size(),
         sup.n_tasks());
  printf("makespan             : %.3f ms\n", makespan / 1e6);
  printf("dispatch p50 / p99   : %.1f / %.1f us\n", pct(lat, .5) / 1e3,
         pct(lat, .99) / 1e3);
  printf("kernel run p50 / p99 : %.1f / %.1f us\n", pct(run, .5) / 1e3,
         pct(run, .99) / 1e3);
  printf("checksum             : %lld (ref %lld) %s\n", (long long)got,
         (long long)ref, got == ref ? "OK" : "MISMATCH");
  return got == ref ? 0 : 1;
}
