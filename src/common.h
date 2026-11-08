// common.hpp — shared layout; must be identical in every process.
// Everything the supervisor, executors, and (later) clients agree on:
// descriptor formats, ring types, and the arena header that lives at the
// base of the shared mapping.
#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>

using TaskId = uint64_t;
using RegionId = uint32_t;
using KernelId = uint32_t;
using UserId = uint32_t;

constexpr size_t ARENA_SIZE = 64ull << 20;
constexpr size_t RING_CAP = 1024; // per executor, power of two
constexpr size_t CMPL_CAP = 4096;
constexpr int MAX_EXEC = 16;
constexpr int MAX_REGIONS = 64;
constexpr uint16_t MAX_INLINE_BORROWS = 24; // spill-to-arena beyond this

inline uint64_t now_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

enum Access : uint8_t { Read, Write };

struct Borrow {
  RegionId r;
  uint64_t off, len; // relative to region base
  Access mode;
};

struct TaskDesc { // POD, fixed size, pushed through rings
  TaskId id;
  UserId user;
  KernelId kernel; // index into the executor's kernel table
  TaskId parent;   // always 0 for now (reserved: nesting)
  uint16_t n_borrows;
  uint64_t t_enqueue; // supervisor stamps at push; telemetry
  Borrow borrows[MAX_INLINE_BORROWS];
  uint64_t arg0, arg1; // POD args (stand-in for spill blob)
};

struct CompletionRec {
  TaskId id;
  uint64_t enqueue_ts, pop_ts, start_ts, end_ts;
  uint32_t status;
};

struct SharedRegion {
  uint64_t offset, size;
};

// SPSC supervisor -> executor. One producer, one consumer.
template <class T, size_t Cap> struct SpscRing {
  alignas(64) std::atomic<uint64_t> head{0};
  alignas(64) std::atomic<uint64_t> tail{0};
  T slots[Cap];
  bool push(const T &v) {
    auto h = head.load(std::memory_order_relaxed);
    if (h - tail.load(std::memory_order_acquire) >= Cap)
      return false;
    slots[h % Cap] = v;
    head.store(h + 1, std::memory_order_release);
    return true;
  }
  bool pop(T &v) {
    auto t = tail.load(std::memory_order_relaxed);
    if (head.load(std::memory_order_acquire) == t)
      return false;
    v = slots[t % Cap];
    tail.store(t + 1, std::memory_order_release);
    return true;
  }
  uint64_t depth() const {
    return head.load(std::memory_order_acquire) -
           tail.load(std::memory_order_acquire);
  }
};

// MPSC executors -> supervisor. Producers claim slots by ticket; the single
// consumer pops in ticket order once the slot's ready flag is set.
template <class T, size_t Cap> struct MpscRing {
  alignas(64) std::atomic<uint64_t> head{0};
  alignas(64) uint64_t tail{0};
  T slots[Cap];
  std::atomic<uint8_t> rdy[Cap];
  void push(const T &v) { // overflow unchecked: Cap >> n_tasks for now
    auto h = head.fetch_add(1, std::memory_order_acq_rel);
    slots[h % Cap] = v;
    rdy[h % Cap].store(1, std::memory_order_release);
  }
  bool pop(T &v) {
    if (!rdy[tail % Cap].load(std::memory_order_acquire))
      return false;
    v = slots[tail % Cap];
    rdy[tail % Cap].store(0, std::memory_order_release);
    ++tail;
    return true;
  }
};

// Arena header sits at the base of the shared mapping; data follows.
struct Arena {
  alignas(64) std::atomic<uint8_t> shutdown{0};
  uint32_t n_exec = 0, n_regions = 0;
  SharedRegion regions[MAX_REGIONS];
  SpscRing<TaskDesc, RING_CAP> task_ring[MAX_EXEC];
  MpscRing<CompletionRec, CMPL_CAP> cmpl;
};

inline uint64_t data_off() { return (sizeof(Arena) + 4095) & ~4095ull; }
