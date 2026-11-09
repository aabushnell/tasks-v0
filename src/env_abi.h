#pragma once

#include <stddef.h>
#include <stdint.h>

#define ENV_MAX_VIEWS 24   /* must match INLINE_DEPS in common.h */

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*EnvKernelFn)(
    void* const* views,
    const uint64_t* lengths,
    uint16_t n_views
);

typedef struct {
  const char* name;
  EnvKernelFn fn;
} EnvKernelMeta;

#ifdef __cplusplus
}
#endif

// extern "C" const   uint32_t        env_kernel_count;
#define ENV_SYM_COUNT "env_kernel_count"
// extern "C" const   EnvKernelMeta   env_kernel_table[];
#define ENV_SYM_TABLE "env_kernel_table"
