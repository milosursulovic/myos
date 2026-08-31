# Memory manager — Milestone 10

## What it does

`kernel/memory.c` / `kernel/memory.h` implement a very simple SRAM heap
allocator, per spec sections 23-24: `kmalloc()`/`kfree()` on a bump-pointer
heap with a first-fit free list, plus a stack/heap collision check on every
heap growth. `shell/shell.c` gets a `mem` command reporting RAM usage.

The ATmega328P has only 2KB of SRAM total, shared between `.data`/`.bss`,
the heap (grows **up** from right after `.bss`), and the stack (grows
**down** from `RAMEND`) — per spec section 24's memory layout diagram.
They must never meet.

## Block header

```c
typedef struct block_header {
    unsigned int size;         /* usable data bytes in this block */
    unsigned char free;        /* 1 = free, 0 = allocated */
    struct block_header *next; /* next block in address order, NULL at end */
} block_header_t;              /* 5 bytes on AVR: no padding, byte-aligned */
```

A single singly-linked, address-ordered list holds *every* block, allocated
and free interleaved. `kmalloc()` returns `(void *)(header + 1)` — a
pointer to the data area right after the header; `kfree()` recovers the
header with `((block_header_t *)ptr) - 1`.

The heap base is `linker.ld`'s existing `_heap_start` symbol (placed right
after `.bss`, unchanged by this milestone). It's referenced as
`extern unsigned char _heap_start;` and used as `&_heap_start` — **not**
`_heap_start` — since it's a zero-size linker symbol marking an address,
not a variable holding one; reading it as a value would read whatever byte
happens to live there instead of taking the address itself (a classic
avr-gcc linker-symbol gotcha).

## Algorithm

Every `kmalloc(size)` call, in order:

1. Reject `size == 0` -> `NULL` (no zero-size allocations).
2. Round `size` up to 2-byte alignment: `(size + 1) & ~1`.
3. First-fit search: walk the block list from the head for the first
   **free** block whose `size >= requested size`.
   - If the leftover remainder is bigger than `sizeof(block_header_t) + 4`
     bytes of slack, split it: shrink the found block to exactly the
     requested size, carve a new free block header for the remainder
     immediately after it in memory, and splice it into the `next` chain.
   - If the remainder isn't worth splitting, hand the block back oversized
     (mark `free = 0`, don't shrink `size`) — avoids producing a free block
     nobody could ever allocate into.
4. If nothing in the list fits, grow the heap (bump-pointer): place a new
   block header at the current high-water mark, mark it allocated, advance
   the high-water mark by `sizeof(block_header_t) + size`, append it to the
   list's tail. This is gated by the stack-collision check (below) — if it
   fails, `kmalloc` returns `NULL` without touching any state.

`kfree(ptr)`:

- `ptr == NULL` is a safe no-op.
- Recovers the header and bounds-checks it falls within
  `[&_heap_start, heap_high_water_mark)` before touching it — if it doesn't
  look like a pointer this allocator produced, it's silently ignored rather
  than risking corrupting memory (there's no error-reporting channel to do
  anything else).
- Marks the block `free = 1`.
- Coalesces **forward only**: if the block's `next` is also free and
  physically adjacent in memory, merges them into one block. See
  "Known limitation" below.

## Stack-collision check

Before growing the heap's high-water mark by `N = sizeof(block_header_t) +
size` bytes, `kmalloc()` reads the CPU's *live* stack pointer via avr-libc's
`SP` macro (`<avr/io.h>`, reads the SPH:SPL pair) and checks:

```c
(heap_high_water_mark + N + SAFETY_MARGIN) <= current_sp
```

`SAFETY_MARGIN` is `#define`d as 64 bytes. This is a **heuristic buffer**,
not a guarantee: it only reflects how deep the call stack happens to be
*at the moment kmalloc() is called*. Nothing stops the stack from growing
deeper immediately afterward — a deeper call chain, or an ISR firing right
after the check passes — so this check reduces the chance of a collision,
it does not eliminate it. If the check fails, `kmalloc()` returns `NULL`
untouched; this is the "overflow" case the spec calls out.

## Known limitation: forward-only coalescing

`kfree()` only merges a freed block with its *next* neighbor if that
neighbor is also free and physically adjacent. It does **not** look
backward to merge with a free *predecessor* — that would require either a
doubly-linked list or an O(n) scan from the list head, which this "very
simple" allocator deliberately avoids. Consequence: freeing blocks in
address order low-to-high can leave adjacent free blocks unmerged (e.g.
free A, then free B — A's freeing happened before B was free, so no merge
occurred, and B's freeing only looks forward past itself, not back at A).
Freeing in the opposite order (high-to-low) coalesces correctly. This is
an accepted limitation, not a bug to fix in this milestone.

## API

```c
void *kmalloc(unsigned int size);
void kfree(void *ptr);
unsigned int kmem_free_bytes(void); /* bytes between heap high-water mark and live SP */
```

`kmem_free_bytes()` returns `current_sp - heap_high_water_mark` — the same
quantity `kmalloc()`'s collision check uses internally, so there's one
source of truth for "how much room is left," reused by both the allocator
and the `mem` shell command (rather than a separately tracked "sum of
allocated bytes" that could drift out of sync).

## Shell command

```
myos> mem
RAM total: 2048
Used:      320
Free:      1728
```

`Free` is `kmem_free_bytes()`; `Used` is `2048 - Free`, so the two always
sum to exactly `RAM total` (matches the spec's own example numbers: `320 +
1728 = 2048`).

## How to test manually

Nothing in this milestone calls `kmalloc()`/`kfree()` yet — no task
structures, no shell command that allocates — so hardware testing mostly
means confirming `mem` reports sane numbers and nothing crashes:

1. `make flash PORT=<your port>`.
2. Connect a serial terminal at 9600 8N1.
3. Type `mem` — expect `RAM total: 2048`, plus `Used`/`Free` that sum to
   2048, with `Used` a small number (just `.data`/`.bss`, since nothing has
   allocated from the heap yet) and `Free` close to 2048 minus whatever
   gap exists to the live stack pointer at that call depth.
4. Type `help` — expect `mem` listed alongside `help`, `info`, `echo`,
   `gpio`, `uptime`.
5. General regression check: `gpio`, `echo`, `uptime` should still work as
   before.

Deeper allocator testing (actual `kmalloc`/`kfree` calls, split/coalesce
behavior, the collision check under real stack pressure) will happen
naturally once Milestone 11 (Tasks) starts actually calling `kmalloc`.

**Status:** hardware-verified (2026-08-31) at the `mem` command level —
`RAM total: 2048 / Used: ... / Free: ...` observed over real serial always
sums to 2048 exactly, as expected (exact split shifts slightly with SRAM
usage elsewhere, e.g. Milestone 9's RX buffer size). Nothing calls
`kmalloc`/`kfree` yet, so this
confirms `kmem_free_bytes()` and the shell wiring, not the allocator's
split/coalesce/collision-check logic itself — that's covered by the
hand-traced logic below plus code review, and gets real exercise once
Milestone 11 (Tasks) starts calling `kmalloc`.
