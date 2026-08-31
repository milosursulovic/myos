#include <avr/io.h>
#include <stddef.h>
#include "memory.h"

/* Block header, singly-linked, address-ordered list of ALL blocks
 * (allocated and free). kmalloc() hands out a pointer to the data area
 * immediately after the header; kfree() recovers the header by stepping
 * back one struct width from the pointer it's given. */
typedef struct block_header {
    unsigned int size;         /* usable data bytes in this block */
    unsigned char free;        /* 1 = free, 0 = allocated */
    struct block_header *next; /* next block in address order, NULL at end */
} block_header_t;

/* linker.ld places this symbol right after .bss. Note &_heap_start, not
 * _heap_start — it's a zero-size linker symbol marking an address, not a
 * variable holding one; reading "_heap_start" as a value would read
 * whatever byte happens to live at that address instead of taking the
 * address itself. Classic avr-gcc linker-symbol gotcha. */
extern unsigned char _heap_start;

/* Heuristic safety buffer kept between the heap's high-water mark and the
 * *current* stack pointer whenever the heap grows. It is NOT a guarantee —
 * it only reflects how deep the stack happens to be at the moment kmalloc()
 * is called; nothing stops the stack from growing deeper afterward (e.g. a
 * deeper call chain or an ISR firing right after the check passes). 64
 * bytes is a reasonable buffer for this project's call depths, not a
 * proven bound. */
#define SAFETY_MARGIN 64

/* A found free block is only worth splitting if the leftover remainder is
 * big enough to be a useful block on its own (a header plus a few bytes of
 * payload) — otherwise the split produces a free block nobody could ever
 * allocate into, wasting a header's worth of space for nothing. */
#define SPLIT_SLACK 4

/* No single allocation can plausibly need more than this on a 2KB-SRAM
 * device. Rejecting anything above it up front (before the alignment
 * rounding and the header-size addition below) avoids unsigned int (16-bit)
 * wraparound: without this cap, kmalloc(65535) rounds to 0 and slips past
 * the size==0 guard, and sizes just under 65536 make `sizeof(header)+size`
 * wrap to a tiny number — both silently corrupt the heap instead of
 * failing. This bound is generous, not tuned; it exists to fail fast with
 * NULL rather than to be a tight limit. */
#define MAX_ALLOC_SIZE 1024

static block_header_t *block_list_head = NULL;
static block_header_t *block_list_tail = NULL;

/* NULL doubles as "heap never touched yet" — &_heap_start is always a
 * valid non-zero SRAM address, so it's a safe sentinel. */
static unsigned char *heap_high_water_mark = NULL;

static void heap_ensure_init(void)
{
    if (heap_high_water_mark == NULL) {
        heap_high_water_mark = &_heap_start;
    }
}

void *kmalloc(unsigned int size)
{
    block_header_t *blk;
    unsigned int total_needed;
    unsigned int prospective_mark;
    unsigned int current_sp;
    unsigned char *new_block_addr;

    if (size == 0 || size > MAX_ALLOC_SIZE) {
        return NULL;
    }

    /* Round up to 2-byte alignment (AVR pointers are 2 bytes). */
    size = (size + 1) & ~1;

    heap_ensure_init();

    /* First-fit search over the existing block list (allocated and free
     * blocks interleaved, in address order). */
    for (blk = block_list_head; blk != NULL; blk = blk->next) {
        if (blk->free && blk->size >= size) {
            unsigned int remainder = (unsigned int)(blk->size - size);

            if (remainder > sizeof(block_header_t) + SPLIT_SLACK) {
                /* Split: carve a new free block out of the tail end of
                 * this one, splice it into the chain right after blk. */
                block_header_t *split = (block_header_t *)
                    ((unsigned char *)blk + sizeof(block_header_t) + size);

                split->size = (unsigned int)(remainder - sizeof(block_header_t));
                split->free = 1;
                split->next = blk->next;

                if (blk == block_list_tail) {
                    block_list_tail = split;
                }

                blk->size = size;
                blk->next = split;
            }
            /* Not worth splitting: hand back the block as-is, oversized. */

            blk->free = 0;
            return (void *)(blk + 1);
        }
    }

    /* No existing free block fits — grow the heap (bump-pointer). This is
     * where the stack/heap collision check happens: never place a new
     * block if doing so would leave less than SAFETY_MARGIN bytes between
     * the new high-water mark and the live stack pointer. */
    total_needed = (unsigned int)(sizeof(block_header_t) + size);
    current_sp = SP;
    prospective_mark = (unsigned int)((unsigned int)heap_high_water_mark + total_needed);

    if (prospective_mark + SAFETY_MARGIN > current_sp) {
        return NULL; /* would collide (or come too close) — overflow case */
    }

    new_block_addr = heap_high_water_mark;
    blk = (block_header_t *)new_block_addr;
    blk->size = size;
    blk->free = 0;
    blk->next = NULL;

    heap_high_water_mark = new_block_addr + total_needed;

    if (block_list_tail != NULL) {
        block_list_tail->next = blk;
    } else {
        block_list_head = blk;
    }
    block_list_tail = blk;

    return (void *)(blk + 1);
}

void kfree(void *ptr)
{
    block_header_t *blk;
    unsigned char *addr;

    if (ptr == NULL) {
        return;
    }

    heap_ensure_init();

    blk = ((block_header_t *)ptr) - 1;
    addr = (unsigned char *)blk;

    /* Defensive bounds check: if this doesn't look like a header this
     * allocator produced, do nothing rather than risk corrupting memory —
     * there's no error-reporting channel to do anything else. */
    if (addr < &_heap_start || addr >= heap_high_water_mark) {
        return;
    }

    /* Double-free guard: without this, freeing an already-free block (or
     * the same pointer twice) would look identical to a normal free, and a
     * later kmalloc() could then hand the same memory to two owners at
     * once while the first is still using it. */
    if (blk->free) {
        return;
    }

    blk->free = 1;

    /* Coalesce forward only: merging with a physically adjacent free
     * successor is an O(1) check with this singly-linked layout. Merging
     * with a predecessor would need a backward link or an O(n) scan from
     * the head, so it's deliberately not done — a known, accepted
     * limitation of this "very simple" allocator, not a bug to fix now. */
    if (blk->next != NULL && blk->next->free &&
        (unsigned char *)blk + sizeof(block_header_t) + blk->size ==
            (unsigned char *)blk->next) {
        if (blk->next == block_list_tail) {
            block_list_tail = blk;
        }
        blk->size = (unsigned int)(blk->size + sizeof(block_header_t) + blk->next->size);
        blk->next = blk->next->next;
    }
}

unsigned int kmem_free_bytes(void)
{
    unsigned int current_sp;
    unsigned int mark;

    heap_ensure_init();
    current_sp = SP;
    mark = (unsigned int)heap_high_water_mark;

    /* Defensive clamp: this subtraction assumes current_sp >= mark (heap
     * above, stack below, gap between them). That should always hold given
     * SAFETY_MARGIN in kmalloc(), but if the stack ever grew deeper than
     * anticipated between checks, an unclamped unsigned subtraction here
     * would wrap to a huge bogus value instead of reporting "no room". */
    if (current_sp < mark) {
        return 0;
    }

    return (unsigned int)(current_sp - mark);
}
