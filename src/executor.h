// executor.hpp — per-core worker process entry point.
#pragma once

struct Arena;

// Child-side entry after fork(). Never returns; exits via the arena's
// shutdown flag. `idx` selects this executor's ring and pinned core.
[[noreturn]] void executor_main(Arena *a, int idx);
