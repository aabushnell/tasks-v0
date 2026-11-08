// supervisor.hpp — parent-side runtime: region frontiers, dependency
// inference, dispatch, completion handling. All state here is
// supervisor-private; executors only ever see the arena.
#pragma once

#include <random>
#include <vector>

#include "common.h"

// Heap-side region entry: arena offset/size plus the graph frontier used
// by dependency inference. (Executors read offsets from Arena::regions;
// the frontier never leaves the supervisor.)
struct Region {
  uint64_t size, off;
  TaskId last_writer = 0;
  std::vector<TaskId> readers;
};

struct TaskRec {
  TaskDesc d;
  uint32_t preds = 0;
  std::vector<TaskId> succ;
};

class Supervisor {
public:
  Supervisor(Arena *a, int nexec) : A_(a), nexec_(nexec) {}

  // Carve a region out of the arena and publish it in Arena::regions.
  // Must be called BEFORE forking executors (shared table is fixed at fork).
  RegionId add_region(uint64_t size);

  // Assign an id, run frontier inference, enqueue if immediately ready.
  void submit(const TaskDesc &d);

  // Dispatch ready tasks and drain completions until every submitted task
  // has finished. Single user, power-of-two ring choice, no stealing.
  void run_until_idle();

  size_t n_tasks() const { return tasks_.size(); }
  const std::vector<CompletionRec> &completions() const { return recs_; }

private:
  uint32_t infer_edges(const TaskDesc &d);
  void dispatch_ready();
  void drain_completions();

  Arena *A_;
  int nexec_;
  uint64_t next_off_ = 0; // arena bump allocator, starts at data_off()

  std::vector<Region> regions_;
  std::vector<TaskRec> tasks_; // indexed by id - 1
  std::vector<TaskId> ready_;
  std::vector<CompletionRec> recs_;
  TaskId next_task_ = 1;
  std::mt19937 rng_{42};
};
