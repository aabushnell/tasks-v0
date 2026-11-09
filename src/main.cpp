#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "common.h"
#include "executor.h"
#include "supervisor.h"

// ---- demo workload params ----

constexpr int NB = 16;            // blocks per array
constexpr size_t BL_LEN = 4096;   // int64s per block (32 KiB)

constexpr KernelID K_INIT = 0;
constexpr KernelID K_STEP = 1;
constexpr KernelID K_SUM = 2;

static TaskSpec mk(
    EnvID env, KernelID k, std::vector<Depend> deps) {
  if (deps.size() > INLINE_DEPS) {
    fprintf(stderr, "mk: %zu borrows > inline cap\n", deps.size());
    _exit(2);
  }

  TaskSpec spec{};
  spec.env = env;
  spec.kernel = k;
  spec.n_depends = (uint16_t)deps.size();
  memcpy(spec.dependencies, deps.data(), deps.size() * sizeof(Depend));

  return spec;
}

static std::vector<Depend> tri(
    const std::vector<BlockID> &arr, int i, BlockID w) {
  return {
      {arr[std::max(0, i - 1)], 0, BL_LEN * 8, Read},
      {arr[i], 0, BL_LEN * 8, Read},
      {arr[std::min(NB - 1, i + 1)], 0, BL_LEN * 8, Read},
      {w, 0, BL_LEN * 8, Write},
  };
}

int main(int argc, char **argv) {
  const std::string kind = argc > 1 ? argv[1] : "local";
  const std::string ref  = argc > 2 ? argv[2]
        : (kind == "nix" ? "./result" : "./env_stencil.so");

  ArenaHeader *AH = (ArenaHeader *)mmap(
      nullptr, ARENA_SIZE, PROT_READ | PROT_WRITE,
      MAP_SHARED | MAP_ANONYMOUS, -1, 0
  );
  if (AH == MAP_FAILED) {
    perror("mmap");
    return EXIT_FAILURE;
  }
  new (AH) ArenaHeader();

  long hw = sysconf(_SC_NPROCESSORS_ONLN);
  int n_exec = (int)std::min<long>(std::max(1L, hw), MAX_EXEC);
  AH->n_exec = n_exec;

  Supervisor sup(AH, mk_env_source(kind));

  EnvID stencil = sup.declare_env(ref);
  if (stencil == INVALID_ENV) return EXIT_FAILURE;

  printf("env source: %s\n", AH->envs[stencil].path);

  // blocks:
  //    X[0..NB),
  //    Y[NB..2NB),
  //    SCALAR,
  //    SEEDS
  std::vector<BlockID> xr(NB), yr(NB);
  for (int i = 0; i < NB; i++)
    xr[i] = sup.alloc_block(BL_LEN * 8);
  for (int i = 0; i < NB; i++)
    yr[i] = sup.alloc_block(BL_LEN * 8);
  const BlockID SCALAR = sup.alloc_block(8);
  const BlockID SEEDS = sup.alloc_block(NB * 8);

  auto* seeds = (int64_t *)((uint8_t *)AH + AH->blocks[SEEDS].offset);
  for (int i = 0; i < NB; i++) {
    seeds[i] = i;
  }

  for (int e = 0; e < n_exec; e++) {
    if (fork() == 0)
      executor_main(AH, e);
  }

  // ---- demo DAG:
  //    init X;
  //    step X->Y;
  //    step Y->X (WAR/WAW wave);
  //    sum X
  // ----
  for (int i = 0; i < NB; i++)
    sup.submit_task(mk(stencil, K_INIT, {
      { xr[i], 0, BL_LEN * 8, Write },
      { SEEDS, (uint64_t)i * 8, 8, Read } }));
  for (int i = 0; i < NB; i++)
    sup.submit_task(mk(stencil, K_STEP, tri(xr, i, yr[i])));
  for (int i = 0; i < NB; i++)
    sup.submit_task(mk(stencil, K_STEP, tri(yr, i, xr[i])));

  {
    std::vector<Depend> deps;
    for (int i = 0; i < NB; i++)
      deps.push_back({ xr[i], 0, BL_LEN * 8, Read });
    deps.push_back({ SCALAR, 0, 8, Write });
    sup.submit_task(mk(stencil, K_SUM, deps));
  }

  std::vector<std::vector<int64_t>> rx(NB, std::vector<int64_t>(BL_LEN)),
                                    ry(NB, std::vector<int64_t>(BL_LEN));
  for (int i = 0; i < NB; i++) {
    for (size_t j = 0; j < BL_LEN; j++) {
      rx[i][j] = (int64_t)((i * 2654435761u + j) % 1000);
    }
  }

  auto step = [&](std::vector<std::vector<int64_t>> &src,
                  std::vector<std::vector<int64_t>> &dst) {
    for (int i = 0; i < NB; i++)
      for (size_t j = 0; j < BL_LEN; j++) {
        int64_t l = src[std::max(0, i - 1)][j], m = src[i][j],
                r = src[std::min(NB - 1, i + 1)][j];
        dst[i][j] = (l + 2 * m + r) / 4;
      }
  };

  step(rx, ry);
  step(ry, rx);

  int64_t ref_sum = 0;
  for (int i = 0; i < NB; i++) {
    for (size_t j = 0; j < BL_LEN; j++)
      ref_sum += rx[i][j];
  }

  uint64_t t0 = now_ns();
  sup.run();
  uint64_t duration = now_ns() - t0;

  AH->shutdown.store(1, std::memory_order_release);
  while (wait(nullptr) > 0) {
    /* spin */
  }

  // result analysis (from traces)
  std::vector<uint64_t> lat, run;
  size_t errors = 0;
  for (auto &trace : sup.completions()) {
    lat.push_back(trace.pop_ts - trace.enqueue_ts);
    run.push_back(trace.end_ts - trace.start_ts);
    if (trace.status != 0) errors++;
  }
  std::sort(lat.begin(), lat.end());
  std::sort(run.begin(), run.end());
  auto pct = [](std::vector<uint64_t> &v, double p) {
    return v[(size_t)(p * (v.size() - 1))];
  };
  int64_t got = *(int64_t *)((char *)AH + AH->blocks[SCALAR].offset);

  printf("executors            : %d\n", n_exec);
  printf("tasks completed      : %zu / %zu\n", sup.completions().size(),
         sup.n_tasks());
  printf("makespan             : %.3f ms\n", duration / 1e6);
  printf("dispatch p50 / p99   : %.1f / %.1f us\n", pct(lat, .5) / 1e3,
         pct(lat, .99) / 1e3);
  printf("kernel run p50 / p99 : %.1f / %.1f us\n", pct(run, .5) / 1e3,
         pct(run, .99) / 1e3);
  printf("checksum             : %lld (ref %lld) %s\n", (long long)got,
         (long long)ref_sum, got == ref_sum ? "OK" : "MISMATCH");
  return got == ref_sum && errors == 0 ? 0 : 1;
}
