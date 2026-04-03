#include "include/kernel_internal.h"

spinlock_t temp_map_lock = 0;
uint64_t saved_irq_flags = 0;

// -------------------------
//           VMM
// -------------------------

static inline void invlpg(uint64_t vaddr) {
    asm volatile("invlpg (%0)" :: "r" (vaddr) : "memory");
}

uint64_t* vmm_get_pte(uint64_t virt, int alloc) {
    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;
    if (!(pml4_table_virt[pml4_idx] & PAGE_PRESENT)) {
        if (!alloc) return 0;
        uint64_t new_pdpt_phys = pmm_alloc_block();
        if (!new_pdpt_phys) return 0; // OOM
        pml4_table_virt[pml4_idx] = new_pdpt_phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
        kernel_memset((void*)P2V(new_pdpt_phys), 0, 4096);
    }
    uint64_t pdpt_phys = pml4_table_virt[pml4_idx] & PAGE_FRAME;
    uint64_t* pdpt_virt = (uint64_t*)P2V(pdpt_phys);
    if (!(pdpt_virt[pdpt_idx] & PAGE_PRESENT)) {
        if (!alloc) return 0;
        uint64_t new_pd_phys = pmm_alloc_block();
        if (!new_pd_phys) return 0;
        pdpt_virt[pdpt_idx] = new_pd_phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
        kernel_memset((void*)P2V(new_pd_phys), 0, 4096);
    }
    uint64_t pd_phys = pdpt_virt[pdpt_idx] & PAGE_FRAME;
    uint64_t* pd_virt = (uint64_t*)P2V(pd_phys);
    if (!(pd_virt[pd_idx] & PAGE_PRESENT)) {
        if (!alloc) return 0;
        uint64_t new_pt_phys = pmm_alloc_block();
        if (!new_pt_phys) return 0;
        pd_virt[pd_idx] = new_pt_phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
        kernel_memset((void*)P2V(new_pt_phys), 0, 4096);
    }
    uint64_t pt_phys = pd_virt[pd_idx] & PAGE_FRAME;
    uint64_t* pt_virt = (uint64_t*)P2V(pt_phys);
    return &pt_virt[pt_idx];
}

uint64_t vmm_get_phys_from_pml4(uint64_t* pml4_phys_root, uint64_t virt) {
    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;
    uint64_t* pml4_virt = (uint64_t*)temp_map((uint64_t)pml4_phys_root);
    uint64_t pml4_entry = pml4_virt[pml4_idx];
    if (!(pml4_entry & PAGE_PRESENT)) {
        temp_unmap();
        return 0;
    }
    uint64_t pdpt_phys = pml4_entry & PAGE_FRAME;
    uint64_t* pdpt_virt = (uint64_t*)temp_map(pdpt_phys);
    uint64_t pdpt_entry = pdpt_virt[pdpt_idx];
    if (!(pdpt_entry & PAGE_PRESENT)) {
        temp_unmap();
        return 0;
    }
    if (pdpt_entry & 0x80) {
        temp_unmap();
        return (pdpt_entry & PAGE_FRAME) + (virt & 0x3FFFFFFF);
    }
    uint64_t pd_phys = pdpt_entry & PAGE_FRAME;
    uint64_t* pd_virt = (uint64_t*)temp_map(pd_phys);
    uint64_t pd_entry = pd_virt[pd_idx];
    if (!(pd_entry & PAGE_PRESENT)) {
        temp_unmap();
        return 0;
    }
    if (pd_entry & 0x80) {
        temp_unmap();
        return (pd_entry & PAGE_FRAME) + (virt & 0x1FFFFF);
    }
    uint64_t pt_phys = pd_entry & PAGE_FRAME;
    uint64_t* pt_virt = (uint64_t*)temp_map(pt_phys);
    uint64_t pt_entry = pt_virt[pt_idx];
    temp_unmap();
    if (!(pt_entry & PAGE_PRESENT)) {
        return 0;
    }
    return pt_entry & PAGE_FRAME;
}

void vmm_map_page(uint64_t phys, uint64_t virt, uint64_t flags) {
    uint64_t* pte = vmm_get_pte(virt, 1);
    if (!pte) {
        kernel_error(0x05, virt, 0, 0, 0);
        return;
    }
    *pte = (phys & PAGE_FRAME) | flags;
    invlpg(virt);
}

void vmm_unmap_page(uint64_t virt) {
    uint64_t* pte = vmm_get_pte(virt, 0);
    if (pte && (*pte & PAGE_PRESENT)) {
        *pte = 0;
        invlpg(virt);
    }
}

uint64_t get_current_pml4() {
    uint64_t pml4;
    asm volatile("mov %%cr3, %0" : "=r"(pml4));
    return pml4;
}

void set_current_pml4(uint64_t phys_addr) {
    asm volatile("mov %0, %%cr3" :: "r"(phys_addr));
}

void* temp_map(uint64_t phys_addr) {
    uint64_t flags = spinlock_irq_save();
    spinlock_acquire(&temp_map_lock);
    saved_irq_flags = flags;
    uint64_t* pte = vmm_get_pte(TEMP_PAGE_VIRT, 1);
    *pte = (phys_addr & PAGE_MASK) | PAGE_PRESENT | PAGE_WRITE;
    asm volatile("invlpg (%0)" :: "r"((uint64_t)TEMP_PAGE_VIRT) : "memory");
    return (void*)TEMP_PAGE_VIRT;
}

void temp_unmap() {
    uint64_t* pte = vmm_get_pte(TEMP_PAGE_VIRT, 1);
    *pte = 0;
    asm volatile("invlpg (%0)" :: "r"((uint64_t)TEMP_PAGE_VIRT) : "memory");
    uint64_t flags_to_restore = saved_irq_flags;
    spinlock_release(&temp_map_lock);
    spinlock_irq_restore(flags_to_restore);
}

uint64_t get_or_alloc_table(uint64_t parent_phys, int index, int flags) {
    uint64_t* parent_virt = (uint64_t*)temp_map(parent_phys);

    if (!(parent_virt[index] & PAGE_PRESENT)) {
        uint64_t new_table_phys = pmm_alloc_block();
        parent_virt[index] = new_table_phys | flags | PAGE_PRESENT | PAGE_WRITE;
        uint64_t* new_table_virt = (uint64_t*)temp_map(new_table_phys);
        kernel_memset(new_table_virt, 0, 4096);
    }

    parent_virt = (uint64_t*)temp_map(parent_phys);
    uint64_t entry = parent_virt[index];

    return entry & PAGE_MASK;
}

void map_to_other_pml4(uint64_t* pml4_phys, uint64_t phys, uint64_t virt, int flags) {
    int pml4_idx = PML4_INDEX(virt);
    int pdp_idx  = PDP_INDEX(virt);
    int pd_idx   = PD_INDEX(virt);
    int pt_idx   = PT_INDEX(virt);

    int table_flags = PAGE_PRESENT | PAGE_WRITE | PAGE_USER;

    uint64_t pdp_phys = get_or_alloc_table((uint64_t)pml4_phys, pml4_idx, table_flags);
    uint64_t pd_phys  = get_or_alloc_table(pdp_phys, pdp_idx, table_flags);
    uint64_t pt_phys  = get_or_alloc_table(pd_phys, pd_idx, table_flags);

    uint64_t* pt_virt = (uint64_t*)temp_map(pt_phys);
    pt_virt[pt_idx] = (phys & PAGE_MASK) | flags;

    temp_unmap();
}

void process_map_memory(process_t* proc, uint64_t virt, uint64_t size) {
    uint64_t old_pml4 = get_current_pml4();

    set_current_pml4((uint64_t)proc->page_directory);

    for (uint64_t i = 0; i < size; i += 4096) {
        uint64_t phys = pmm_alloc_block();
        vmm_map_page(phys, virt + i, PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
    }

    set_current_pml4(old_pml4);
}
