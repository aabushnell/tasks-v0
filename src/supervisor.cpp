#include "supervisor.h"

BlockID Supervisor::alloc_block(uint64_t size) {
  if (next_offset == 0) next_offset = arena_data_offset();

  BlockID id = AH->n_blocks++;
  AH->blocks[id] = { next_offset, size };

  // NOTE: standardize implementation
  frontier.push_back({ next_offset, size });

  next_offset += size;
  return id;
}

EnvID Supervisor::declare_env(const std::string& ref) {
  std::string so = env_src->resolve(ref);
  if (so.empty() || so.size() >= ENV_PATH_LEN || AH->n_envs >= MAX_ENVS) {
    fprintf(stderr, "declare_env: cannot use '%s'\n", ref.c_str());
    return INVALID_ENV;
  }

  EnvID id = AH->n_envs++;
  memset(AH->envs[id].path, 0, ENV_PATH_LEN);
  memcpy(AH->envs[id].path, so.data(), so.size());

  return id;
}

void Supervisor::submit_task(const TaskSpec &task) {
  TaskSpec spec = task;
  spec.id = next_task++;
  tasks.push_back({spec, update_edges(spec), {}});
  if (tasks.back().n_preds == 0)
    ready.push_back(spec.id);
}

void Supervisor::run_until_idle() {
  recs.reserve(tasks.size());
  while (recs.size() < tasks.size()) {
    dispatch_ready();
    drain_completions();
    sched_yield();
  }
}

uint32_t Supervisor::update_edges(const TaskSpec &task) {
  // WARNING: cannot have >64 preds (fix later)
  TaskID preds[64];
  uint32_t n_preds = 0;

  auto add_pred_dedup = [&](TaskID pred) {
    if (!pred)
      return;
    for (uint32_t i = 0; i < n_preds; i++) {
      if (preds[i] == pred)
        return;
    }
    preds[n_preds++] = pred;
  };

  // collect task dependencies from frontier
  for (int i = 0; i < task.n_depends; i++) {
    FrontierBlock &block = frontier[task.dependencies[i].id];
    if (task.dependencies[i].mode == Read) {
      add_pred_dedup(block.last_writer);
    } else {
      add_pred_dedup(block.last_writer);
      for (TaskID rd : block.readers)
        add_pred_dedup(rd);
    }
  }
  for (uint32_t i = 0; i < n_preds; i++)
    tasks[preds[i] - 1].succ.push_back(task.id);

  // update frontier based on R/W dependencies
  for (int i = 0; i < task.n_depends; i++) {
    FrontierBlock &block = frontier[task.dependencies[i].id];
    if (task.dependencies[i].mode == Read)
      block.readers.push_back(task.id);
    else {
      block.readers.clear();
      block.last_writer = task.id;
    }
  }
  return n_preds;
}

void Supervisor::dispatch_ready() {
  while (!ready.empty()) {
    TaskID id = ready.back();
    ready.pop_back();

    TaskSpec &task = tasks[id - 1].spec;
    int a = rng_seed() % nexec_, b = rng_seed() % nexec_;
    int tgt = (AH->task_ring[a].depth() <= AH->task_ring[b].depth())
                  ? a : b;
    task.enqueue_ts = now_ns();
    while (!AH->task_ring[tgt].push(task))
      sched_yield();
  }
}

void Supervisor::drain_completions() {
  CompletionRecord rec;
  while (AH->cmpl.pop(rec)) {
    recs.push_back(rec);
    for (TaskID s : tasks[rec.id - 1].succ) {
      if (--tasks[s - 1].n_preds == 0)
        ready.push_back(s);
    }
  }
}
