#ifndef MYOS_MEMORY_H
#define MYOS_MEMORY_H

/* Very simple SRAM heap allocator, per spec sections 23-24. The ATmega328P
 * only has 2KB of SRAM, shared between .data/.bss, the heap (grows up from
 * right after .bss), and the stack (grows down from RAMEND) — so this
 * allocator is deliberately minimal: a bump-pointer heap with a first-fit
 * free list, forward-only coalescing on free, and a heuristic check before
 * every heap growth that the new high-water mark won't run into the live
 * stack pointer. See kernel/memory.c and docs/memory.md for the full
 * design and its known limitations. */

void *kmalloc(unsigned int size);
void kfree(void *ptr);

/* Bytes currently available between the heap's high-water mark and the
 * live stack pointer — the same quantity kmalloc() checks internally
 * before growing the heap. Used by the shell's `mem` command. */
unsigned int kmem_free_bytes(void);

#endif /* MYOS_MEMORY_H */
