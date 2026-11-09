#pragma once

#include <random>
#include <unordered_map>
#include <vector>
#include <cstring>

#include "common.h"
#include "env_source.h"

struct FrontierBlock {
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
  Supervisor(ArenaHeader *header,
             std::unique_ptr<EnvSource> src =
                std::make_unique<LocalEnvSource>())
      : arena(header), env_src(std::move(src)) {}

  // WARNING: for now block structure fixed at fork
  BlockID alloc_block(uint64_t size);

  EnvID declare_env(const std::string& ref);

  void submit_task(const TaskSpec &task);

  void run();

  size_t n_tasks() const { return submitted_tasks.size(); }
  const std::vector<TaskTrace> &completions() const { return traces; }

private:
  uint32_t update_edges(const TaskSpec &d);
  void dispatch_ready();
  void drain_completions();

  ArenaHeader *arena;

  // -- env handling --

  std::unique_ptr<EnvSource> env_src;
  std::unordered_map<std::string, EnvID> env_ids;

  // -- counters --
  // NOTE: decide init time
  uint64_t next_offset = 0;
  TaskID next_task_id = 1;

  // -- runtime structures --

  std::vector<FrontierBlock>  frontier;
  std::vector<TaskState>      submitted_tasks;
  std::vector<TaskID>         ready_tasks;
  std::vector<TaskTrace>      traces;

  std::mt19937 rng{42};
};
