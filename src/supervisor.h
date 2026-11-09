// supervisor.hpp — parent-side runtime: region frontiers, dependency
// inference, dispatch, completion handling. All state here is
// supervisor-private; executors only ever see the arena.
#pragma once

#include <random>
#include <vector>
#include <cstring>

#include "common.h"
#include "env_source.h"

// Heap-side region entry: arena offset/size plus the graph frontier used
// by dependency inference. (Executors read offsets from Arena::regions;
// the frontier never leaves the supervisor.)
struct FrontierBlock {
  uint64_t offset;
  uint64_t size;
  TaskID last_writer = 0;
  std::vector<TaskID> readers;
};

struct TaskState {
  TaskSpec spec;
  uint32_t n_preds = 0;
  std::vector<TaskID> succ;
};

class Supervisor {
public:
  Supervisor(ArenaHeader *header, int nexec,
             std::unique_ptr<EnvSource> src = std::make_unique<LocalEnvSource>())
      : AH(header), nexec_(nexec) {}

  // Carve a region out of the arena and publish it in Arena::regions.
  // Must be called BEFORE forking executors (shared table is fixed at fork).
  BlockID alloc_block(uint64_t size);

  EnvID declare_env(const std::string& ref);

  // Assign an id, run frontier inference, enqueue if immediately ready.
  void submit_task(const TaskSpec &task);

  // Dispatch ready tasks and drain completions until every submitted task
  // has finished. Single user, power-of-two ring choice, no stealing.
  void run_until_idle();

  size_t n_tasks() const { return tasks.size(); }
  const std::vector<CompletionRecord> &completions() const { return recs; }

private:
  uint32_t update_edges(const TaskSpec &d);
  void dispatch_ready();
  void drain_completions();

  ArenaHeader *AH;
  int nexec_;
  std::unique_ptr<EnvSource> env_src;
  uint64_t next_offset = 0; // arena bump allocator, starts at data_off()

  std::vector<FrontierBlock>  frontier;
  std::vector<TaskState>      tasks; // indexed by id - 1
  std::vector<TaskID>         ready;
  std::vector<CompletionRecord> recs;
  TaskID next_task = 1;
  std::mt19937 rng_seed{42};
};
