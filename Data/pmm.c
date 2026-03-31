#include "include/kernel_internal.h"

// --------------------------
//            PMM
// --------------------------

void mmap_set(uint64_t bit) {
    bitmap[bit / 64] |= (1ULL << (bit % 64));
}

void mmap_unset(uint64_t bit) {
    bitmap[bit / 64] &= ~(1ULL << (bit % 64));
}

int mmap_test(uint64_t bit) {
    return (bitmap[bit / 64] & (1ULL << (bit % 64))) != 0;
}

int64_t mmap_first_free() {
    uint64_t limit = max_blocks / 64;
    for (uint64_t i = 0; i < limit; i++) {
        if (bitmap[i] != 0xFFFFFFFFFFFFFFFF) {
            int bit = __builtin_ctzll(~bitmap[i]);
            return i * 64 + bit;
        }
    }
    return -1;
}

void init_pmm(uint64_t mem_size, uint64_t bitmap_base) {
    max_blocks = mem_size / BLOCK_SIZE;
    used_blocks = max_blocks;
    bitmap = (uint64_t*)bitmap_base;
    bitmap_size = max_blocks / 8;
    kernel_memset(bitmap, 0xFF, bitmap_size);
}

uint64_t pmm_alloc_block() {
    uint64_t irq = spinlock_irq_save();
    spinlock_acquire(&pmm_lock);
    if (max_blocks <= used_blocks) {
        spinlock_release(&pmm_lock);
        spinlock_irq_restore(irq);
        return 0;
    }
    int64_t frame = mmap_first_free();
    if (frame == -1) {
        spinlock_release(&pmm_lock);
        spinlock_irq_restore(irq);
        return 0;
    }
    mmap_set(frame);
    used_blocks++;
    uint64_t addr = (uint64_t)frame * BLOCK_SIZE;
    spinlock_release(&pmm_lock);
    spinlock_irq_restore(irq);
    return addr;
}

void pmm_free_block(uint64_t p_addr) {
    uint64_t irq = spinlock_irq_save();
    spinlock_acquire(&pmm_lock);
    uint64_t frame = p_addr / BLOCK_SIZE;
    mmap_unset(frame);
    if (used_blocks > 0) used_blocks--;
    spinlock_release(&pmm_lock);
    spinlock_irq_restore(irq);
}

void pmm_init_region(uint64_t base, uint64_t size) {
    uint64_t align = base / BLOCK_SIZE;
    uint64_t blocks = size / BLOCK_SIZE;
    for (; blocks > 0; blocks--) {
        mmap_unset(align++);
        used_blocks--;
    }
}

void pmm_deinit_region(uint64_t base, uint64_t size) {
    uint64_t align = base / BLOCK_SIZE;
    uint64_t blocks = size / BLOCK_SIZE;
    for (; blocks > 0; blocks--) {
        mmap_set(align++);
        used_blocks++;
    }
}
