// supervisor.cpp — see supervisor.hpp. The graph exists only as
// pred counters + successor lists + region frontiers: O(borrows) per
// submission, and dynamic submission needs no structural change later.
#include "supervisor.h"

RegionId Supervisor::add_region(uint64_t size) {
  if (next_off_ == 0)
    next_off_ = data_off();
  RegionId id = A_->n_regions;
  A_->regions[id] = {next_off_, size};
  A_->n_regions++;
  regions_.push_back({size, next_off_});
  next_off_ += size;
  return id;
}

uint32_t Supervisor::infer_edges(const TaskDesc &d) {
  TaskId preds[64];
  uint32_t n = 0;
  auto add = [&](TaskId p) {
    if (!p)
      return;
    for (uint32_t i = 0; i < n; i++)
      if (preds[i] == p)
        return;
    preds[n++] = p;
  };

  // collect predecessors from region frontiers (RAW / WAW / WAR)
  for (int i = 0; i < d.n_borrows; i++) {
    Region &r = regions_[d.borrows[i].r];
    if (d.borrows[i].mode == Read) {
      add(r.last_writer);
    } else {
      add(r.last_writer);
      for (TaskId rd : r.readers)
        add(rd);
    }
  }
  for (uint32_t i = 0; i < n; i++)
    tasks_[preds[i] - 1].succ.push_back(d.id);

  // this task becomes the new frontier on every region it touches
  for (int i = 0; i < d.n_borrows; i++) {
    Region &r = regions_[d.borrows[i].r];
    if (d.borrows[i].mode == Read)
      r.readers.push_back(d.id);
    else {
      r.readers.clear();
      r.last_writer = d.id;
    }
  }
  return n;
}

void Supervisor::submit(const TaskDesc &din) {
  TaskDesc d = din;
  d.id = next_task_++;
  tasks_.push_back({d, infer_edges(d), {}});
  if (tasks_.back().preds == 0)
    ready_.push_back(d.id);
}

void Supervisor::dispatch_ready() {
  while (!ready_.empty()) {
    TaskId id = ready_.back();
    ready_.pop_back();
    TaskDesc &d = tasks_[id - 1].d;
    int a = rng_() % nexec_, b = rng_() % nexec_;
    int tgt = (A_->task_ring[a].depth() <= A_->task_ring[b].depth()) ? a : b;
    d.t_enqueue = now_ns();
    while (!A_->task_ring[tgt].push(d))
      sched_yield();
  }
}

void Supervisor::drain_completions() {
  CompletionRec rec;
  while (A_->cmpl.pop(rec)) {
    recs_.push_back(rec);
    for (TaskId s : tasks_[rec.id - 1].succ)
      if (--tasks_[s - 1].preds == 0)
        ready_.push_back(s);
  }
}

void Supervisor::run_until_idle() {
  recs_.reserve(tasks_.size());
  while (recs_.size() < tasks_.size()) {
    dispatch_ready();
    drain_completions();
    sched_yield();
  }
}
