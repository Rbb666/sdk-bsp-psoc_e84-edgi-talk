/* SPDX-License-Identifier: Apache-2.0 */
#ifndef SPINE_VGLITE_CACHE_H
#define SPINE_VGLITE_CACHE_H

#include "cy_pdl.h"
#include <string.h>

/* Only the Spine project's vg_lite.c includes this file. Its DMA memory is
 * non-cacheable, and the VG_LITE_SUBMIT wrapper publishes completed commands. */
#define SCB_CleanInvalidateDCache() __COMPILER_BARRIER()

/* The nano C library uses byte loops. Aligned command words and target
 * descriptors can use word accesses, with the library handling the tail. */
typedef uint32_t spine_vg_word_t __attribute__((__may_alias__));

static inline int spine_vglite_memcmp(const void *first, const void *second, size_t length)
{
    const spine_vg_word_t *left = first;
    const spine_vg_word_t *right = second;
    if ((((uintptr_t)left | (uintptr_t)right) & (sizeof(*left) - 1U)) == 0U)
    {
        while (length >= sizeof(*left) && *left == *right)
        {
            ++left;
            ++right;
            length -= sizeof(*left);
        }
    }
    return length == 0U ? 0 : memcmp(left, right, length);
}

static inline void *spine_vglite_memcpy(void *destination, const void *source, size_t length)
{
    spine_vg_word_t *out = destination;
    const spine_vg_word_t *in = source;
    if ((((uintptr_t)out | (uintptr_t)in) & (sizeof(*out) - 1U)) == 0U)
    {
        while (length >= 4U * sizeof(*out))
        {
            out[0] = in[0];
            out[1] = in[1];
            out[2] = in[2];
            out[3] = in[3];
            out += 4;
            in += 4;
            length -= 4U * sizeof(*out);
        }
        while (length >= sizeof(*out))
        {
            *out++ = *in++;
            length -= sizeof(*out);
        }
    }
    if (length != 0U)
        memcpy(out, in, length);
    return destination;
}

#define memcmp spine_vglite_memcmp
#define memcpy spine_vglite_memcpy

#endif
