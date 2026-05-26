#include "include/kernel_internal.h"

spinlock_t temp_lock = 0;
uint64_t temp_bitmap[TEMP_PAGES_COUNT / 64] = {0};

// -------------------------
//           VMM
// -------------------------

void* temp_map(uint64_t phys_addr) {
    uint64_t irq = spinlock_irq_save();
    spinlock_acquire(&temp_lock);

    int bit = -1;
    for (int i = 0; i < (TEMP_PAGES_COUNT / 64); i++) {
        if (temp_bitmap[i] != 0xFFFFFFFFFFFFFFFFULL) {
            int local_bit = __builtin_ctzll(~temp_bitmap[i]);
            temp_bitmap[i] |= (1ULL << local_bit);
            bit = i * 64 + local_bit;
            break;
        }
    }

    spinlock_release(&temp_lock);
    spinlock_irq_restore(irq);

    if (bit == -1) {
        kernel_error(0x5, 0x2, 0, 0, 0);
    }

    uint64_t virt_addr = TEMP_PAGE_VIRT + (bit * 4096ULL);

    hal_map_page(virt_addr, phys_addr, 0x3);

    return (void*)virt_addr;
}

void temp_unmap(void* virt_ptr) {
    uint64_t virt_addr = (uint64_t)virt_ptr;

    if (virt_addr < TEMP_PAGE_VIRT || virt_addr >= TEMP_PAGE_VIRT + (TEMP_PAGES_COUNT * 4096ULL)) {
        return; 
    }

    int bit = (virt_addr - TEMP_PAGE_VIRT) / 4096ULL;

    hal_unmap_page(virt_addr);

    uint64_t irq = spinlock_irq_save();
    spinlock_acquire(&temp_lock);
    
    temp_bitmap[bit / 64] &= ~(1ULL << (bit % 64));
    
    spinlock_release(&temp_lock);
    spinlock_irq_restore(irq);
}

void process_map_memory(process_t* proc, uint64_t virt, uint64_t size) {
    uint64_t old_space = hal_get_current_address_space();

    hal_set_current_address_space((uint64_t)proc->page_directory);

    for (uint64_t i = 0; i < size; i += 4096) {
        uint64_t phys = pmm_alloc_block();
        hal_map_page(virt + i, phys, 0x7);
    }

    hal_set_current_address_space(old_space);
}