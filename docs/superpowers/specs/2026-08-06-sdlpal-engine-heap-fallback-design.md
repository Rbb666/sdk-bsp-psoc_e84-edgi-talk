# SDLPal Engine Heap Fallback Design

Date: 2026-08-06

## Problem

During battle, `UTIL_malloc(21246)` can fail even though the exit report later
shows substantial free SRAM. The allocation is made while battle sprites and
other resources are live, and the primary SRAM heap cannot provide a suitable
contiguous block. Cleanup after the fatal exit releases those resources, so the
exit-time free count does not describe the failing allocation point.

All SDLPal engine `malloc` calls are redirected to `pal_engine_malloc`, but the
current platform hook only provides a fallback for the large save-game buffer.
Normal game resources return `NULL` immediately when the primary allocation
fails. The memory report confirms that no cold allocation was attempted.

## Goals

- Keep normal allocations in on-chip SRAM when possible.
- Keep battle resources in fast GFX SRAM before using HyperRAM.
- Preserve the existing save reserve and surface pool behavior.
- Correctly handle `malloc`, `calloc`, `realloc`, and `free` across memory
  sources.
- Avoid changes to SDLPal battle/fight core sources and `vg_lite_hal.c`.

## Allocation Architecture

Add a target-owned engine heap adapter under the SDLPal platform directory.
For normal engine allocations it uses this order:

1. Primary SRAM through the normal libc allocator.
2. A fixed GFX SRAM resource pool with two 64 KiB slots.
3. HyperRAM through `pal_cold_alloc` as the final fallback.

The GFX pool is independent of the existing two-slot surface pool. This is
required because both surface slots are normally occupied by the battle
background and scene surface while the effect sprite is loaded.

The platform I/O hook will redirect `malloc`, `calloc`, `realloc`, and `free`
to matching engine wrappers. The adapter records only non-primary allocations;
ordinary libc pointers, including pointers created by `strdup`, continue to be
released or reallocated by libc.

## Ownership And Reallocation

Each GFX slot stores its requested size. HyperRAM fallback pointers are stored
in a bounded platform registry with their allocation sizes. This permits:

- correct memory-policy accounting on free;
- zero initialization for `calloc` fallbacks;
- data-preserving `realloc` when a GFX or HyperRAM pointer changes storage;
- normal libc `free` and `realloc` for untracked primary pointers.

An allocation fails only after all three sources are unavailable. Registry
capacity is checked before a HyperRAM allocation so an untrackable allocation
cannot leak.

## Save Buffer Interaction

Save-sized `malloc` requests retain their existing priority:

1. Primary SRAM.
2. Dedicated 192 KiB GFX save reserve.
3. HyperRAM only if both previous sources are unavailable.

The save reserve remains outside the generic GFX resource pool. Its current
logging and chunked SD-card write path are unchanged.

## Memory Budget

The new resource pool reserves 128 KiB in
`.cy_gpu_buf.sdlpal_resource`. Based on the current image report:

- Current GFX fixed usage: 2,857,984 bytes.
- New GFX fixed usage: 2,989,056 bytes.
- GFX region capacity: 3,145,728 bytes.
- Remaining GFX margin: 156,672 bytes.

The linker script defines start/end symbols and asserts that the pool is
exactly 128 KiB and that the complete GFX section remains inside the region.

## Testing

Host tests will force the primary allocator to fail and verify:

- a 21,246-byte allocation succeeds from GFX;
- two 64 KiB GFX slots can be occupied independently;
- a third allocation falls back to HyperRAM;
- `calloc` fallback memory is zero-filled;
- `realloc` preserves data across fallback storage;
- every allocation is released through its owning allocator;
- overflow and exhausted-registry cases fail without leaking.

Static contract tests will verify all four allocation hooks, the dedicated GFX
section, linker symbols, and the 128 KiB assertion. Existing host tests and the
target macro syntax check must remain green. A full ARM link is required when
the configured ARM GCC toolchain is available.

## Expected Runtime Result

The 21,246-byte battle allocation first uses a free GFX resource slot when the
primary SRAM heap is fragmented. HyperRAM remains unused for the common battle
path unless both resource slots are already occupied. `UTIL_malloc` therefore
does not terminate the engine for this recoverable SRAM allocation failure.
