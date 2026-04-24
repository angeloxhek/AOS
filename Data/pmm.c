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

static void copy_data_page(uint64_t src_phys, uint64_t dst_phys) {
    void* src = temp_map(src_phys);
    void* dst = temp_map(dst_phys);
    kernel_memcpy(dst, src, PAGE_SIZE);
    temp_unmap(src);
    temp_unmap(dst);
}

void copy_table_recursive(uint64_t* src_table, uint64_t* dst_table, int level, uint64_t* dst_pml4) {
    for (int i = 0; i < 512; i++) {
        if (!(src_table[i] & PAGE_PRESENT)) continue;

        uint64_t phys_next = pmm_alloc_block();
        if (!phys_next) kernel_error(0x5, 0, 0, 0, 0);

        uint64_t flags = src_table[i] & 0xFFF;
        dst_table[i] = phys_next | flags;

        if (level > 0) {
            uint64_t* src_next = (uint64_t*)temp_map(src_table[i] & PAGE_FRAME);
            uint64_t* dst_next = (uint64_t*)temp_map(phys_next);
            
            copy_table_recursive(src_next, dst_next, level - 1, dst_pml4);
            
            temp_unmap(src_next);
            temp_unmap(dst_next);
        } else {
            uint64_t src_data_phys = src_table[i] & PAGE_FRAME;
            uint64_t dst_data_phys = pmm_alloc_block();
            
            dst_table[i] = dst_data_phys | flags;
            
            copy_data_page(src_data_phys, dst_data_phys);
        }
    }
}

void copy_address_space(uint64_t* src_pml4_virt, uint64_t* dst_pml4_virt) {
    for (int i = 0; i < 256; i++) {
        if (!(src_pml4_virt[i] & PAGE_PRESENT)) continue;

        uint64_t pdpt_phys = pmm_alloc_block();
        uint64_t flags = src_pml4_virt[i] & 0xFFF;
        dst_pml4_virt[i] = pdpt_phys | flags;

        uint64_t* src_pdpt = (uint64_t*)temp_map(src_pml4_virt[i] & PAGE_FRAME);
        uint64_t* dst_pdpt = (uint64_t*)temp_map(pdpt_phys);

        copy_table_recursive(src_pdpt, dst_pdpt, 2, dst_pml4_virt);

        temp_unmap(src_pdpt);
        temp_unmap(dst_pdpt);
    }
}