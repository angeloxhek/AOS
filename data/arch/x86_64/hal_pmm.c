#include "hal_arch.h"

extern uint64_t pmm_alloc_block(void);
extern void pmm_free_block(uint64_t p_addr);
extern void* temp_map(uint64_t phys);
extern void temp_unmap(void* virt);
extern void kernel_memcpy(void* dest, const void* src, uint64_t n);
extern void kernel_error(uint64_t code, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4);
extern thread_t* current_thread;

#define GET_PHYS_ADDR(entry) ((entry) & 0x000FFFFFFFFFF000)

static void copy_data_page(uint64_t src_phys, uint64_t dst_phys) {
    void* src = temp_map(src_phys);
    void* dst = temp_map(dst_phys);
    kernel_memcpy(dst, src, 4096);
    temp_unmap(src);
    temp_unmap(dst);
}

static void copy_table_recursive(uint64_t* src_table, uint64_t* dst_table, int level, uint64_t* dst_pml4) {
    for (int i = 0; i < 512; i++) {
        if (!(src_table[i] & 0x1)) continue;

        uint64_t phys_next = pmm_alloc_block();
        if (!phys_next) kernel_error(0x5, 0, 0, 0, 0);

        uint64_t flags = src_table[i] & 0xFFF;
        dst_table[i] = phys_next | flags;

        if (level > 0) {
            uint64_t* src_next = (uint64_t*)temp_map(GET_PHYS_ADDR(src_table[i]));
            uint64_t* dst_next = (uint64_t*)temp_map(phys_next);
            
            copy_table_recursive(src_next, dst_next, level - 1, dst_pml4);
            
            temp_unmap(src_next);
            temp_unmap(dst_next);
        } else {
            uint64_t src_data_phys = GET_PHYS_ADDR(src_table[i]);
            uint64_t dst_data_phys = pmm_alloc_block();
            
            dst_table[i] = dst_data_phys | flags;
            copy_data_page(src_data_phys, dst_data_phys);
        }
    }
}

void hal_copy_address_space(uint64_t* src_pml4_virt, uint64_t* dst_pml4_virt) {
    for (int i = 0; i < 256; i++) {
        if (!(src_pml4_virt[i] & 0x1)) continue;

        uint64_t pdpt_phys = pmm_alloc_block();
        uint64_t flags = src_pml4_virt[i] & 0xFFF;
        dst_pml4_virt[i] = pdpt_phys | flags;

        uint64_t* src_pdpt = (uint64_t*)temp_map(GET_PHYS_ADDR(src_pml4_virt[i]));
        uint64_t* dst_pdpt = (uint64_t*)temp_map(pdpt_phys);

        copy_table_recursive(src_pdpt, dst_pdpt, 2, dst_pml4_virt);

        temp_unmap(src_pdpt);
        temp_unmap(dst_pdpt);
    }
}

static void destroy_pt(uint64_t pt_phys) {
    uint64_t* pt_virt = (uint64_t*)temp_map(pt_phys);
    for (int i = 0; i < 512; i++) {
        if (pt_virt[i] & 0x1) {
            uint64_t page_phys = GET_PHYS_ADDR(pt_virt[i]);
            pmm_free_block(page_phys);
        }
    }
    temp_unmap(pt_virt);
    pmm_free_block(pt_phys);
}

static void destroy_pd(uint64_t pd_phys) {
    uint64_t* pd_virt = (uint64_t*)temp_map(pd_phys);
    for (int i = 0; i < 512; i++) {
        if (pd_virt[i] & 0x1) {
            if (!(pd_virt[i] & (1 << 7))) {
                destroy_pt(GET_PHYS_ADDR(pd_virt[i]));
            } else {
                pmm_free_block(GET_PHYS_ADDR(pd_virt[i]));
            }
        }
    }
    temp_unmap(pd_virt);
    pmm_free_block(pd_phys);
}

static void destroy_pdpt(uint64_t pdpt_phys) {
    uint64_t* pdpt_virt = (uint64_t*)temp_map(pdpt_phys);
    for (int i = 0; i < 512; i++) {
        if (pdpt_virt[i] & 0x1) {
            destroy_pd(GET_PHYS_ADDR(pdpt_virt[i]));
        }
    }
    temp_unmap(pdpt_virt);
    pmm_free_block(pdpt_phys);
}

void hal_destroy_address_space(process_t* proc) {
    uint64_t* pml4_virt = (uint64_t*)temp_map((uint64_t)proc->page_directory);
    
    for (int i = 0; i < 256; i++) {
        if (pml4_virt[i] & 0x1) {
            destroy_pdpt(GET_PHYS_ADDR(pml4_virt[i]));
            pml4_virt[i] = 0;
        }
    }
    temp_unmap(pml4_virt);
    
    proc->heap_limit = 0;
    
    if (current_thread && current_thread->owner == proc) {
        asm volatile("mov %0, %%cr3" : : "r"(proc->page_directory));
    }
}

uint64_t hal_get_total_ram(boot_info_t* boot_info) {
    uint32_t entry_count = boot_info->mmap.map_size / sizeof(e820_entry_t);
    e820_entry_t* e820_entries = (e820_entry_t*)boot_info->mmap.map_addr;
    uint64_t total_ram = 0;
    for (uint32_t i = 0; i < entry_count; i++) {
        if (e820_entries[i].type == E820_RAM) {
            total_ram += e820_entries[i].length;
        }
    }
    return total_ram;
}