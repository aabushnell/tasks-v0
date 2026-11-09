#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>

using TaskID    = uint64_t;
using BlockID   = uint32_t;
using KernelID  = uint32_t;
using EnvID     = uint32_t;
using UserID    = uint32_t;

constexpr size_t    ARENA_SIZE      = 64ull << 20;
constexpr size_t    DISP_CAP        = 1024;
constexpr size_t    CMPL_CAP        = 4096;
constexpr int       MAX_EXEC        = 16;
constexpr int       MAX_BLOCKS      = 64;
constexpr int       MAX_ENVS        = 32;
constexpr size_t    ENV_PATH_LEN    = 192;
constexpr uint16_t  INLINE_DEPS     = 24;

constexpr EnvID     INVALID_ENV     = UINT32_MAX;

inline uint64_t now_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

enum AccessMode : uint8_t { Read, Write };

struct Depend {
  BlockID id;
  uint64_t offset; // relative to block base
  uint64_t len;
  AccessMode mode;
};

struct TaskSpec {
  TaskID id;
  EnvID env;
  KernelID kernel;
  UserID user;
  uint64_t enqueue_ts;
  uint16_t n_depends;
  Depend dependencies[INLINE_DEPS];
};

struct TaskTrace {
  TaskID id;
  uint64_t enqueue_ts;
  uint64_t pop_ts;
  uint64_t start_ts;
  uint64_t end_ts;
  uint32_t status;
};

struct BlockDesc {
  uint64_t offset;
  uint64_t size;
};

struct EnvDesc {
  char path[ENV_PATH_LEN];
};

// supervisor -> executor
template <class T, size_t Cap> struct SPSCRing {
  T slots[Cap];

  alignas(64) std::atomic<uint64_t> head{0};
  alignas(64) std::atomic<uint64_t> tail{0};

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

// executors -> supervisor
template <class T, size_t Cap> struct MPSCRing {
  T slots[Cap];
  std::atomic<uint8_t> rdy[Cap];

  alignas(64) std::atomic<uint64_t> head{0};
  alignas(64) uint64_t tail{0};

  void push(const T &v) {
    // WARNING: overflow unchecked for now
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

// ArenaHeader sits at base of shared memory;
//  block-data sits below it
struct ArenaHeader {
  alignas(64) std::atomic<uint8_t> shutdown{ 0 };

  uint32_t n_exec = 0;

  uint32_t n_blocks = 0;
  BlockDesc blocks[MAX_BLOCKS];

  uint32_t n_envs = 0;
  EnvDesc envs[MAX_ENVS];

  SPSCRing<TaskSpec, DISP_CAP> dispatch_ring[MAX_EXEC];
  MPSCRing<TaskTrace, CMPL_CAP> completion_ring;
};

// ptr offset to arena block-data section
inline uint64_t arena_data_offset() {
  return (sizeof(ArenaHeader) + 4095) & ~4095ull;
}
