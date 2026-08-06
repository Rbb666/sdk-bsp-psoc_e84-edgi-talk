# SDLPal Engine Heap Fallback Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prevent battle resource allocation failures when the primary SRAM heap is fragmented by adding a fast GFX SRAM fallback and a final HyperRAM fallback to the platform-owned SDLPal heap hooks.

**Architecture:** Keep upstream battle and fight code unchanged. Add a platform `pal_engine_heap` adapter that delegates normal allocations to libc SRAM first, then uses two fixed 64 KiB GFX resource slots, and finally uses the existing cold-memory policy. Track only GFX and cold pointers so ordinary libc and `strdup` pointers continue to use libc semantics.

**Tech Stack:** C99, RT-Thread memory APIs, GNU linker script, SCons, host GCC tests, Python `unittest` contract tests.

## Global Constraints

- Do not modify `libraries/components/Infineon_vglite-latest/.../vg_lite_hal.c`.
- Do not modify SDLPal battle, fight, resource, or utility core behavior.
- Preserve allocation priority for save buffers: SRAM, dedicated 192 KiB GFX save reserve, then HyperRAM.
- Preserve surface allocation priority: SRAM, dedicated two-slot GFX surface pool, then HyperRAM.
- Reserve exactly 128 KiB for the new resource pool in `.cy_gpu_buf.sdlpal_resource`.
- Keep total GFX usage within the 3,145,728-byte `gfx_mem` region.
- Prefix every shell command with `rtk`.
- Use `apply_patch` for manual file edits.

---

### Task 1: Commit The Verified Surface Fallback Baseline

**Files:**
- Modify: `projects/Edgi_Talk_M55_SDLPAL/board/linker_scripts/link.ld`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/sdlpal/UPSTREAM.md`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/sdlpal/upstream/esp32s3/native_engine_shim/sdl_shim.c`
- Create: `projects/Edgi_Talk_M55_SDLPAL/platform/pal_surface_storage.c`
- Create: `projects/Edgi_Talk_M55_SDLPAL/platform/pal_surface_storage.h`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/tests/host/Makefile`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/tests/host/test_project_contract.py`
- Create: `projects/Edgi_Talk_M55_SDLPAL/tests/host/test_sdl_shim_surface_fallback.c`
- Create: `projects/Edgi_Talk_M55_SDLPAL/tests/host/test_surface_storage.c`

**Interfaces:**
- Produces: `pal_surface_alloc(size_t, pal_memory_tag_t, pal_surface_storage_kind_t *)` and `pal_surface_free(void *, size_t, pal_memory_tag_t, pal_surface_storage_kind_t)` as the stable surface fallback baseline.

- [ ] **Step 1: Include the direct surface fallback test in `all`**

Update the host Makefile `all` dependency list so its final entries are:

```make
	test_touch_poll_cache test_surface_storage \
	test_sdl_shim_surface_fallback
```

- [ ] **Step 2: Re-run the baseline tests**

Run:

```powershell
rtk python -m unittest discover -s projects/Edgi_Talk_M55_SDLPAL/tests/host -p 'test_*.py'
rtk powershell -NoProfile -Command "Set-Location 'projects/Edgi_Talk_M55_SDLPAL/tests/host'; & make all"
```

Expected: 34 Python tests pass, including `sdl_shim surface fallback: PASS`.

- [ ] **Step 3: Verify the baseline diff is isolated**

Run:

```powershell
rtk git diff --check
rtk git status --short
```

Expected: only the nine surface-fallback files listed above are dirty or untracked.

- [ ] **Step 4: Commit the surface fallback**

Run:

```powershell
rtk git add -- projects/Edgi_Talk_M55_SDLPAL/board/linker_scripts/link.ld projects/Edgi_Talk_M55_SDLPAL/sdlpal/UPSTREAM.md projects/Edgi_Talk_M55_SDLPAL/sdlpal/upstream/esp32s3/native_engine_shim/sdl_shim.c projects/Edgi_Talk_M55_SDLPAL/platform/pal_surface_storage.c projects/Edgi_Talk_M55_SDLPAL/platform/pal_surface_storage.h projects/Edgi_Talk_M55_SDLPAL/tests/host/Makefile projects/Edgi_Talk_M55_SDLPAL/tests/host/test_project_contract.py projects/Edgi_Talk_M55_SDLPAL/tests/host/test_sdl_shim_surface_fallback.c projects/Edgi_Talk_M55_SDLPAL/tests/host/test_surface_storage.c
rtk git commit -m "fix: add GFX fallback for SDLPal surfaces"
```

Expected: one commit containing only the surface allocation fallback.

---

### Task 2: Add A Tested Engine Heap Adapter

**Files:**
- Create: `projects/Edgi_Talk_M55_SDLPAL/platform/pal_engine_heap.h`
- Create: `projects/Edgi_Talk_M55_SDLPAL/platform/pal_engine_heap.c`
- Create: `projects/Edgi_Talk_M55_SDLPAL/tests/host/test_engine_heap.c`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/tests/host/Makefile`

**Interfaces:**
- Consumes: `pal_cold_alloc(size_t, pal_memory_tag_t)` and `pal_cold_free(void *, size_t, pal_memory_tag_t)` from `pal_memory_policy.h`.
- Produces: `pal_engine_heap_malloc`, `pal_engine_heap_calloc`, `pal_engine_heap_realloc`, `pal_engine_heap_free`, `pal_engine_heap_fallback_malloc`, and `pal_engine_heap_set_primary_ops`.

- [ ] **Step 1: Write the failing heap behavior test**

Create `test_engine_heap.c` with a configurable primary allocator that can be forced to fail and a cold allocator backed by host `malloc`. The test must include these assertions:

```c
pal_engine_heap_set_primary_ops(&primary_ops);
pal_memory_policy_configure(cold_test_fail_alloc, cold_test_free, NULL,
                            test_cold_alloc, test_cold_free, NULL);

first = pal_engine_heap_malloc(21246u);
second = pal_engine_heap_malloc(64u * 1024u);
third = pal_engine_heap_malloc(21246u);
assert(first != NULL);
assert(second != NULL);
assert(third != NULL);
assert(cold_allocations == 1u);

zeroed = pal_engine_heap_calloc(53u, 401u);
assert(zeroed != NULL);
for (i = 0u; i < 53u * 401u; ++i)
{
    assert(((unsigned char *)zeroed)[i] == 0u);
}

memset(third, 0x5a, 21246u);
resized = pal_engine_heap_realloc(third, 30000u);
assert(resized != NULL);
for (i = 0u; i < 21246u; ++i)
{
    assert(((unsigned char *)resized)[i] == 0x5a);
}
```

Free every returned pointer and assert cold-policy current bytes return to zero. Also assert `pal_engine_heap_calloc(SIZE_MAX, 2u)` returns `NULL`.

- [ ] **Step 2: Add the host target and verify RED**

Add `test_engine_heap` to `all` in the host Makefile and compile it from `test_engine_heap.c`, `pal_engine_heap.c`, and `pal_memory_policy.c`.

Run:

```powershell
rtk powershell -NoProfile -Command "Set-Location 'projects/Edgi_Talk_M55_SDLPAL/tests/host'; & make test_engine_heap"
```

Expected: FAIL because `pal_engine_heap.c` and `pal_engine_heap.h` do not exist.

- [ ] **Step 3: Define the heap adapter interface**

Create `pal_engine_heap.h` with:

```c
#ifndef PAL_ENGINE_HEAP_H
#define PAL_ENGINE_HEAP_H

#include <stddef.h>

typedef void *(*pal_engine_malloc_fn)(size_t size);
typedef void *(*pal_engine_calloc_fn)(size_t count, size_t size);
typedef void *(*pal_engine_realloc_fn)(void *pointer, size_t size);
typedef void (*pal_engine_free_fn)(void *pointer);

typedef struct pal_engine_heap_primary_ops
{
    pal_engine_malloc_fn malloc_fn;
    pal_engine_calloc_fn calloc_fn;
    pal_engine_realloc_fn realloc_fn;
    pal_engine_free_fn free_fn;
} pal_engine_heap_primary_ops_t;

void pal_engine_heap_set_primary_ops(
    const pal_engine_heap_primary_ops_t *ops);
void *pal_engine_heap_malloc(size_t size);
void *pal_engine_heap_fallback_malloc(size_t size);
void *pal_engine_heap_calloc(size_t count, size_t size);
void *pal_engine_heap_realloc(void *pointer, size_t size);
void pal_engine_heap_free(void *pointer);

#endif
```

- [ ] **Step 4: Implement the minimal three-tier allocator**

Create `pal_engine_heap.c` with these constants and state:

```c
#define PAL_ENGINE_GFX_SLOT_BYTES (64u * 1024u)
#define PAL_ENGINE_GFX_SLOTS 2u
#define PAL_ENGINE_COLD_RECORDS 64u

typedef struct pal_engine_cold_record
{
    void *pointer;
    size_t size;
} pal_engine_cold_record_t;

static PAL_ENGINE_GFX uint8_t
    resource_pool[PAL_ENGINE_GFX_SLOTS][PAL_ENGINE_GFX_SLOT_BYTES];
static size_t resource_sizes[PAL_ENGINE_GFX_SLOTS];
static pal_engine_cold_record_t cold_records[PAL_ENGINE_COLD_RECORDS];
```

Implement allocation in this exact order:

```c
void *pal_engine_heap_malloc(size_t size)
{
    void *pointer;

    if (size == 0u)
    {
        return NULL;
    }
    pointer = primary_ops.malloc_fn(size);
    return pointer != NULL ? pointer : fallback_alloc(size, 0);
}
```

`fallback_alloc` must take a free GFX slot when `size <= 64 KiB`, otherwise reserve a cold-record entry before calling `pal_cold_alloc(size, PAL_MEMORY_TAG_RESOURCE)`. If the cold allocation fails, release the reserved record.

`pal_engine_heap_calloc` must reject multiplication overflow, try primary `calloc`, then call `fallback_alloc(total, 1)` and zero fallback memory.

`pal_engine_heap_free` must check GFX slots first, cold records second, and delegate all untracked pointers to primary `free`.

For a tracked GFX or cold pointer, `pal_engine_heap_realloc` must allocate a replacement with `pal_engine_heap_malloc`, copy `min(old_size, new_size)`, then release the old pointer. For `NULL`, call `pal_engine_heap_malloc`; for size zero, free and return `NULL`; for an untracked pointer, delegate to primary `realloc`.

- [ ] **Step 5: Run the focused test to verify GREEN**

Run:

```powershell
rtk powershell -NoProfile -Command "Set-Location 'projects/Edgi_Talk_M55_SDLPAL/tests/host'; & make test_engine_heap"
```

Expected: `engine_heap: PASS`.

- [ ] **Step 6: Commit the heap adapter**

Run:

```powershell
rtk git add -- projects/Edgi_Talk_M55_SDLPAL/platform/pal_engine_heap.c projects/Edgi_Talk_M55_SDLPAL/platform/pal_engine_heap.h projects/Edgi_Talk_M55_SDLPAL/tests/host/test_engine_heap.c projects/Edgi_Talk_M55_SDLPAL/tests/host/Makefile
rtk git commit -m "feat: add SDLPal engine heap fallback"
```

Expected: focused allocator implementation and tests in one commit.

---

### Task 3: Route SDLPal Libc Allocations Through The Adapter

**Files:**
- Modify: `projects/Edgi_Talk_M55_SDLPAL/platform/pal_engine_io.h`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/platform/pal_engine_io.c`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/platform/pal_engine_io_hooks.h`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/tests/host/test_project_contract.py`

**Interfaces:**
- Consumes: all `pal_engine_heap_*` functions from Task 2.
- Produces: `pal_engine_malloc`, `pal_engine_calloc`, `pal_engine_realloc`, and `pal_engine_free` as the complete engine libc allocation contract.

- [ ] **Step 1: Extend the static contract test and verify RED**

Update `test_save_path_is_platform_owned_and_bounded` to require:

```python
for libc_name, platform_name in (
    ("malloc", "pal_engine_malloc"),
    ("calloc", "pal_engine_calloc"),
    ("realloc", "pal_engine_realloc"),
    ("free", "pal_engine_free"),
    ("fopen", "pal_engine_fopen"),
    ("fwrite", "pal_engine_fwrite"),
    ("fclose", "pal_engine_fclose"),
):
    self.assertIn(f"#define {libc_name} {platform_name}", hooks)

self.assertIn("pal_engine_heap_malloc", adapter)
self.assertIn("pal_engine_heap_calloc", adapter)
self.assertIn("pal_engine_heap_realloc", adapter)
self.assertIn("pal_engine_heap_fallback_malloc", adapter)
```

Remove the old assertion forbidding fallback allocation from the adapter.

Run:

```powershell
rtk python -m unittest projects.Edgi_Talk_M55_SDLPAL.tests.host.test_project_contract.ProjectContractTest.test_save_path_is_platform_owned_and_bounded
```

Expected: FAIL because `calloc` and `realloc` hooks are missing.

- [ ] **Step 2: Add the new wrapper declarations and macros**

Add to `pal_engine_io.h`:

```c
void *pal_engine_calloc(size_t count, size_t size);
void *pal_engine_realloc(void *pointer, size_t size);
```

Add to `pal_engine_io_hooks.h`:

```c
#define calloc pal_engine_calloc
#define realloc pal_engine_realloc
```

- [ ] **Step 3: Wire allocation while preserving save priority**

In `pal_engine_io.c`, include `pal_engine_heap.h`. Implement normal allocation as:

```c
if (!save_sized_allocation(size))
{
    return pal_engine_heap_malloc(size);
}

pointer = malloc(size);
if (pointer != NULL)
{
    /* retain existing SRAM save logging */
    return pointer;
}
pointer = save_reserve_acquire();
if (pointer != NULL)
{
    /* retain existing GFX reserve logging */
    return pointer;
}
return pal_engine_heap_fallback_malloc(size);
```

Implement `pal_engine_calloc` and non-save `pal_engine_realloc` as direct calls to the heap adapter. If `realloc` receives `save_reserve`, return it unchanged when the new size fits; otherwise allocate a replacement, copy 192 KiB, and release the reserve only after the replacement succeeds.

Update `pal_engine_free` to handle `save_reserve` first and delegate every other pointer to `pal_engine_heap_free`.

- [ ] **Step 4: Verify the focused contract and host allocator tests**

Run:

```powershell
rtk python -m unittest projects.Edgi_Talk_M55_SDLPAL.tests.host.test_project_contract.ProjectContractTest.test_save_path_is_platform_owned_and_bounded
rtk powershell -NoProfile -Command "Set-Location 'projects/Edgi_Talk_M55_SDLPAL/tests/host'; & make test_engine_heap test_save_io_core"
```

Expected: all focused tests pass.

- [ ] **Step 5: Commit the hook integration**

Run:

```powershell
rtk git add -- projects/Edgi_Talk_M55_SDLPAL/platform/pal_engine_io.c projects/Edgi_Talk_M55_SDLPAL/platform/pal_engine_io.h projects/Edgi_Talk_M55_SDLPAL/platform/pal_engine_io_hooks.h projects/Edgi_Talk_M55_SDLPAL/tests/host/test_project_contract.py
rtk git commit -m "fix: fall back engine allocations outside SRAM"
```

Expected: one commit containing only hook integration and its contract test.

---

### Task 4: Enforce The GFX Resource Budget And Run Regression Tests

**Files:**
- Modify: `projects/Edgi_Talk_M55_SDLPAL/board/linker_scripts/link.ld`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/tools/check_elf.py`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/tests/host/test_check_elf.py`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/tests/host/test_project_contract.py`
- Modify: `projects/Edgi_Talk_M55_SDLPAL/sdlpal/UPSTREAM.md`

**Interfaces:**
- Consumes: `.cy_gpu_buf.sdlpal_resource` from Task 2.
- Produces: `__sdlpal_resource_start__` and `__sdlpal_resource_end__` linker symbols validated as an exact 128 KiB range.

- [ ] **Step 1: Add failing linker-contract tests**

Extend `test_check_elf.py` fixtures with:

```python
"__sdlpal_resource_start__": (
    0x26309600 + 593408 + 196608 + 131072
),
"__sdlpal_resource_end__": (
    0x26309600 + 593408 + 196608 + 131072 + 131072
),
"__cy_gpu_buf_end__": (
    0x26309600 + 593408 + 196608 + 131072 + 131072
),
```

Add tests that delete `__sdlpal_resource_end__`, set the size to 131071, and place the end outside `gfx_mem`; each must produce a resource-pool validation error.

Update `test_project_contract.py` to require the section name, linker symbols, and `0x20000` assertion.

Run:

```powershell
rtk python -m unittest projects.Edgi_Talk_M55_SDLPAL.tests.host.test_check_elf projects.Edgi_Talk_M55_SDLPAL.tests.host.test_project_contract
```

Expected: FAIL because the new symbols and validator do not exist.

- [ ] **Step 2: Add the linker section and assertion**

After the surface pool in `link.ld`, add:

```ld
. = ALIGN(64);
__sdlpal_resource_start__ = .;
KEEP(*(.cy_gpu_buf.sdlpal_resource))
. = ALIGN(64);
__sdlpal_resource_end__ = .;
```

Add:

```ld
ASSERT(
  (__sdlpal_resource_end__ - __sdlpal_resource_start__) == 0x20000,
  "SDLPal engine resource GFX pool must be exactly 128 KiB."
)
```

- [ ] **Step 3: Validate the resource range in `check_elf.py`**

Add `PAL_RESOURCE_POOL_BYTES = 128 * 1024`, require both resource symbols, verify the exact size, and verify the range is inside both `gfx_mem` and `__cy_gpu_buf_start__..__cy_gpu_buf_end__`.

- [ ] **Step 4: Update provenance documentation**

Add one target-change item to `sdlpal/UPSTREAM.md` stating that normal engine allocations prefer SRAM, use the fixed GFX resource pool on fragmentation, and use HyperRAM only as the final fallback. State that all changes are in platform hooks and the platform-specific shim; upstream battle/fight code remains unchanged.

- [ ] **Step 5: Run the full host regression suite**

Run:

```powershell
rtk python -m unittest discover -s projects/Edgi_Talk_M55_SDLPAL/tests/host -p 'test_*.py'
rtk powershell -NoProfile -Command "Set-Location 'projects/Edgi_Talk_M55_SDLPAL/tests/host'; & make all"
rtk gcc -std=c99 -Wall -Wextra -Werror -pedantic -fsyntax-only -Iprojects/Edgi_Talk_M55_SDLPAL/platform -Iprojects/Edgi_Talk_M55_SDLPAL/sdlpal/upstream/esp32s3/native_engine_shim/include -DPAL_ENGINE_BRIDGE_REQUIRE_TARGET_HOOKS=1 -DPAL_SDL_SHIM_EXTERNAL_SURFACES_ONLY=1 -DPAL_SDL_SHIM_DYNAMIC_SURFACES=1 -DPAL_PSOC_DIRECT_INDEXED=1 projects/Edgi_Talk_M55_SDLPAL/sdlpal/upstream/esp32s3/native_engine_shim/sdl_shim.c projects/Edgi_Talk_M55_SDLPAL/platform/pal_surface_storage.c projects/Edgi_Talk_M55_SDLPAL/platform/pal_engine_heap.c projects/Edgi_Talk_M55_SDLPAL/platform/pal_memory_policy.c
rtk git diff --check
```

Expected: all Python and C tests pass, target-macro syntax check produces no output, and diff check is clean.

- [ ] **Step 6: Attempt the full target build**

Run:

```powershell
rtk powershell -NoProfile -Command "Set-Location 'projects/Edgi_Talk_M55_SDLPAL'; & 'D:/workspace_work/env-windows/.venv/Scripts/scons.exe' -j1"
```

Expected when the configured ARM GCC exists: build and linker assertions pass, with GFX fixed usage near 2,989,056 bytes. If the configured `RTT_EXEC_PATH` is still absent, record that environmental blocker without claiming a target build pass.

- [ ] **Step 7: Commit the layout and verification contract**

Run:

```powershell
rtk git add -- projects/Edgi_Talk_M55_SDLPAL/board/linker_scripts/link.ld projects/Edgi_Talk_M55_SDLPAL/tools/check_elf.py projects/Edgi_Talk_M55_SDLPAL/tests/host/test_check_elf.py projects/Edgi_Talk_M55_SDLPAL/tests/host/test_project_contract.py projects/Edgi_Talk_M55_SDLPAL/sdlpal/UPSTREAM.md
rtk git commit -m "test: enforce SDLPal GFX resource budget"
```

Expected: final implementation commit with linker validation and documentation.
