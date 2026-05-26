#include "hal_arch.h"

uint64_t* pml4_table_virt = 0;

extern uint64_t pmm_alloc_block(void);
extern void* kernel_memset(void* ptr, uint8_t value, uint64_t n);
extern void* temp_map(uint64_t phys_addr);
extern void temp_unmap(void* virt_ptr);

static inline void invlpg(uint64_t vaddr) {
    asm volatile("invlpg %0" : : "m" (*(char*)vaddr) : "memory");
}

static uint64_t* vmm_get_pte(uint64_t virt, int alloc) {
    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;
    
    if (!(pml4_table_virt[pml4_idx] & 0x1)) { // PAGE_PRESENT
        if (!alloc) return 0;
        uint64_t new_pdpt_phys = pmm_alloc_block();
        if (!new_pdpt_phys) return 0;
        pml4_table_virt[pml4_idx] = new_pdpt_phys | 0x7; // PRESENT | WRITE | USER
        kernel_memset((void*)(new_pdpt_phys + 0xFFFF800000000000), 0, 4096);
    }
    
    uint64_t pdpt_phys = pml4_table_virt[pml4_idx] & PAGE_FRAME;
    uint64_t* pdpt_virt = (uint64_t*)(pdpt_phys + 0xFFFF800000000000);
    if (!(pdpt_virt[pdpt_idx] & 0x1)) {
        if (!alloc) return 0;
        uint64_t new_pd_phys = pmm_alloc_block();
        if (!new_pd_phys) return 0;
        pdpt_virt[pdpt_idx] = new_pd_phys | 0x7;
        kernel_memset((void*)(new_pd_phys + 0xFFFF800000000000), 0, 4096);
    }
    
    uint64_t pd_phys = pdpt_virt[pdpt_idx] & PAGE_FRAME;
    uint64_t* pd_virt = (uint64_t*)(pd_phys + 0xFFFF800000000000);
    if (!(pd_virt[pd_idx] & 0x1)) {
        if (!alloc) return 0;
        uint64_t new_pt_phys = pmm_alloc_block();
        if (!new_pt_phys) return 0;
        pd_virt[pd_idx] = new_pt_phys | 0x7;
        kernel_memset((void*)(new_pt_phys + 0xFFFF800000000000), 0, 4096);
    }
    
    uint64_t pt_phys = pd_virt[pd_idx] & PAGE_FRAME;
    uint64_t* pt_virt = (uint64_t*)(pt_phys + 0xFFFF800000000000);
    return &pt_virt[pt_idx];
}

void hal_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t* pte = vmm_get_pte(virt, 1);
    if (!pte) {
        kernel_error(0x05, virt, 0, 0, 0);
        return;
    }
    *pte = (phys & PAGE_FRAME) | flags;
    invlpg(virt);
}

void hal_unmap_page(uint64_t virt) {
    uint64_t* pte = vmm_get_pte(virt, 0);
    if (pte && (*pte & 0x1)) { // PAGE_PRESENT
        *pte = 0;
        invlpg(virt);
    }
}

uint64_t hal_get_current_address_space(void) {
    uint64_t pml4;
    asm volatile("mov %%cr3, %0" : "=r"(pml4));
    return pml4;
}

void hal_set_current_address_space(uint64_t phys_addr) {
    asm volatile("mov %0, %%cr3" :: "r"(phys_addr));
}

uint64_t hal_get_phys(uint64_t space_root_phys, uint64_t virt) {
    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;
    
    uint64_t* pml4_virt = (uint64_t*)temp_map(space_root_phys);
    uint64_t pml4_entry = pml4_virt[pml4_idx];
    if (!(pml4_entry & 0x1)) { temp_unmap(pml4_virt); return 0; }
    
    uint64_t pdpt_phys = pml4_entry & PAGE_FRAME;
    uint64_t* pdpt_virt = (uint64_t*)temp_map(pdpt_phys);
    uint64_t pdpt_entry = pdpt_virt[pdpt_idx];
    if (!(pdpt_entry & 0x1)) { temp_unmap(pdpt_virt); return 0; }
    
    if (pdpt_entry & 0x80) {
        temp_unmap(pdpt_virt);
        return (pdpt_entry & PAGE_FRAME) + (virt & 0x3FFFFFFF);
    }
    
    uint64_t pd_phys = pdpt_entry & PAGE_FRAME;
    uint64_t* pd_virt = (uint64_t*)temp_map(pd_phys);
    uint64_t pd_entry = pd_virt[pd_idx];
    if (!(pd_entry & 0x1)) { temp_unmap(pd_virt); return 0; }
    
    if (pd_entry & 0x80) {
        temp_unmap(pd_virt);
        return (pd_entry & PAGE_FRAME) + (virt & 0x1FFFFF);
    }
    
    uint64_t pt_phys = pd_entry & PAGE_FRAME;
    uint64_t* pt_virt = (uint64_t*)temp_map(pt_phys);
    uint64_t pt_entry = pt_virt[pt_idx];
    temp_unmap(pt_virt);
    
    if (!(pt_entry & 0x1)) return 0;
    return pt_entry & PAGE_FRAME;
}

uint64_t* hal_create_address_space(void) {
    uint64_t pml4_phys = pmm_alloc_block();
    if (!pml4_phys) return 0;
    
    uint64_t* pml4_virt = (uint64_t*)temp_map(pml4_phys);
    kernel_memset(pml4_virt, 0, 4096);

    uint64_t* kernel_pml4_virt = (uint64_t*)temp_map(hal_get_current_address_space());
    for (int i = 256; i < 512; i++) {
        pml4_virt[i] = kernel_pml4_virt[i];
    }
    temp_unmap(kernel_pml4_virt);

    pml4_virt[510] = pml4_phys | 0x3;
    temp_unmap(pml4_virt);

    return (uint64_t*)pml4_phys;
}

static uint64_t get_or_alloc_table(uint64_t parent_phys, int index, int flags) {
    uint64_t* parent_virt = (uint64_t*)temp_map(parent_phys);
    if (!(parent_virt[index] & 0x1)) {
        uint64_t new_table_phys = pmm_alloc_block();
        parent_virt[index] = new_table_phys | flags | 0x3; // PRESENT | WRITE
        uint64_t* new_table_virt = (uint64_t*)temp_map(new_table_phys);
        kernel_memset(new_table_virt, 0, 4096);
        temp_unmap(new_table_virt); 
    }
    uint64_t entry = parent_virt[index];
    temp_unmap(parent_virt);
    return entry & PAGE_MASK;
}

void hal_map_page_in_space(uint64_t space_root_phys, uint64_t virt, uint64_t phys, uint64_t flags) {
    int pml4_idx = (virt >> 39) & 0x1FF;
    int pdp_idx  = (virt >> 30) & 0x1FF;
    int pd_idx   = (virt >> 21) & 0x1FF;
    int pt_idx   = (virt >> 12) & 0x1FF;

    int table_flags = 0x7;
    uint64_t pdp_phys = get_or_alloc_table(space_root_phys, pml4_idx, table_flags);
    uint64_t pd_phys  = get_or_alloc_table(pdp_phys, pdp_idx, table_flags);
    uint64_t pt_phys  = get_or_alloc_table(pd_phys, pd_idx, table_flags);

    uint64_t* pt_virt = (uint64_t*)temp_map(pt_phys);
    pt_virt[pt_idx] = (phys & PAGE_MASK) | flags;
    temp_unmap(pt_virt);
}

void hal_flush_tlb(void) {
    uint64_t current_cr3;
    asm volatile("mov %%cr3, %0" : "=r"(current_cr3));
    asm volatile("mov %0, %%cr3" :: "r"(current_cr3));
}

void hal_vm_init(void) {
    uint64_t *pml4 = (uint64_t *)PHYS_PML4;
    uint64_t *asm_pd = (uint64_t *)PHYS_ASM_PD;
    uint64_t phys_addr = 0x200000;
    
    for (int i = 1; i < 512; i++) {
        asm_pd[i] = phys_addr | 0x83;
        phys_addr += 0x200000;
    }
    
    uint64_t *hhdm_pdpt = (uint64_t *)PHYS_HHDM_PDPT;
    uint64_t *hhdm_pd   = (uint64_t *)PHYS_HHDM_PD;
    kernel_memset(hhdm_pdpt, 0, 4096);
    kernel_memset(hhdm_pd, 0, 4096);
    
    pml4[256] = PHYS_HHDM_PDPT | 0x3;
    hhdm_pdpt[0] = PHYS_HHDM_PD | 0x3;
    
    phys_addr = 0;
    for (int i = 0; i < 512; i++) {
        hhdm_pd[i] = phys_addr | 0x83;
        phys_addr += 0x200000;
    }
    pml4[510] = PHYS_PML4 | 0x3;
    
    asm volatile ("mov %0, %%cr3" :: "r"((uint64_t)PHYS_PML4));
    
    pml4_table_virt = (uint64_t *)(P2V(PHYS_PML4));
}