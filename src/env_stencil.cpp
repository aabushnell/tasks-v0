#include "env_abi.h"

// borrows: [0] = block (write), [1] = 8-byte seed (read, params-as-data)
static void k_init(void* const* v, const uint64_t* l, uint16_t) {
    int64_t* o = (int64_t*)v[0];
    size_t n = l[0] / 8;
    uint32_t seed = (uint32_t)*(const int64_t*)v[1];
    for (size_t i = 0; i < n; i++)
        o[i] = (int64_t)((seed * 2654435761u + i) % 1000);
}

// borrows: [0..2] = left/mid/right (read), [3] = out (write)
static void k_step(void* const* v, const uint64_t* l, uint16_t) {
    const int64_t *L = (const int64_t*)v[0], *M = (const int64_t*)v[1],
                  *R = (const int64_t*)v[2];
    int64_t* O = (int64_t*)v[3];
    size_t n = l[3] / 8;
    for (size_t i = 0; i < n; i++) O[i] = (L[i] + 2 * M[i] + R[i]) / 4;
}

// borrows: [0..n-2] = partials (read), [n-1] = scalar (write)
static void k_sum(void* const* v, const uint64_t* l, uint16_t nv) {
    int64_t s = 0;
    for (uint16_t b = 0; b + 1 < nv; b++) {
        const int64_t* p = (const int64_t*)v[b];
        for (size_t i = 0; i < l[b] / 8; i++) s += p[i];
    }
    *(int64_t*)v[nv - 1] = s;
}

extern "C" const uint32_t env_kernel_count = 3;
extern "C" const EnvKernelMeta env_kernel_table[] = {
    { "init",         k_init },
    { "stencil_step", k_step },
    { "reduce_sum",   k_sum  },
};
