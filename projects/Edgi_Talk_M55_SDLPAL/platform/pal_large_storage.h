#ifndef PAL_LARGE_STORAGE_H
#define PAL_LARGE_STORAGE_H

#ifndef PAL_LARGE
#if defined(__GNUC__) || defined(__clang__)
#define PAL_LARGE static __attribute__((section(".cy_gpu_buf.sdlpal_large"), aligned(4)))
#else
#error "PAL_LARGE requires a compiler-specific GFX SRAM section attribute"
#endif
#endif

#endif
