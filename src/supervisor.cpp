#include "supervisor.h"

BlockID Supervisor::alloc_block(uint64_t size) {
  if (next_offset == 0) next_offset = arena_data_offset();

  BlockID id = arena->n_blocks++;
  arena->blocks[id] = { next_offset, size };

  // NOTE: standardize implementation layer
  frontier.push_back({ /* placeholder */ });

  next_offset += size;
  return id;
}

EnvID Supervisor::declare_env(const std::string& ref) {
  std::string so = env_src->resolve(ref);
  if (so.empty() || so.size() >= ENV_PATH_LEN || arena->n_envs >= MAX_ENVS) {
    fprintf(stderr, "declare_env: cannot use '%s'\n", ref.c_str());
    return INVALID_ENV;
  }

  EnvID id = arena->n_envs++;
  memset(arena->envs[id].path, 0, ENV_PATH_LEN);
  memcpy(arena->envs[id].path, so.data(), so.size());

  return id;
}

void Supervisor::submit_task(const TaskSpec &task) {
  TaskSpec spec = task;
  spec.id = next_task_id++;
  submitted_tasks.push_back(
    { spec, update_edges(spec), { /* empty */ } }
  );
  if (submitted_tasks.back().n_preds == 0)
    ready_tasks.push_back(spec.id);
}

void Supervisor::run() {
  traces.reserve(submitted_tasks.size());
  while (traces.size() < submitted_tasks.size()) {
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
    submitted_tasks[preds[i] - 1].succ.push_back(task.id);

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
  while (!ready_tasks.empty()) {
    TaskID id = ready_tasks.back();
    ready_tasks.pop_back();

    TaskSpec &task = submitted_tasks[id - 1].spec;
    int a = rng() % arena->n_exec;
    int b = rng() % arena->n_exec;
    int tgt = (arena->dispatch_ring[a].depth() <= arena->dispatch_ring[b].depth())
                  ? a : b;
    task.enqueue_ts = now_ns();
    while (!arena->dispatch_ring[tgt].push(task))
      sched_yield();
  }
}

void Supervisor::drain_completions() {
  TaskTrace rec;
  while (arena->completion_ring.pop(rec)) {
    traces.push_back(rec);
    for (TaskID s : submitted_tasks[rec.id - 1].succ) {
      if (--submitted_tasks[s - 1].n_preds == 0)
        ready_tasks.push_back(s);
    }
  }
}
