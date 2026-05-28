#include "hal_arch.h"

struct gdt_entry gdt[7];
struct gdt_ptr   gp;
struct tss_entry_t tss;

__attribute__((aligned(16)))
kernel_tcb_t kernel_tcb = {
    .canary = 0xDEADBEEF
};

__attribute__((aligned(16)))
uint8_t default_fpu_state[512];

extern void* kernel_memset(void* ptr, uint8_t value, uint64_t n);

static void gdt_set_gate(int num, uint64_t base, uint64_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;
    gdt[num].limit_low = (limit & 0xFFFF);
    gdt[num].granularity = ((limit >> 16) & 0x0F);
    gdt[num].granularity |= (gran & 0xF0);
    gdt[num].access = access;
}

static void write_tss(int32_t num, uint64_t base, uint32_t limit) {
    gdt_set_gate(num, base, limit, 0x89, 0x00);
    gdt[num + 1].limit_low = 0;
    gdt[num + 1].base_low = 0;
    gdt[num + 1].base_middle = 0;
    gdt[num + 1].access = 0;
    gdt[num + 1].granularity = 0;
    uint64_t *high_desc = (uint64_t *)&gdt[num + 1];
    *high_desc = (base >> 32);
}

static void gdt_install() {
    gp.limit = (sizeof(struct gdt_entry) * 7) - 1;
    gp.base  = (uint64_t)&gdt;
    gdt_set_gate(0, 0, 0, 0, 0);
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xAF);
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0x00);
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xF2, 0x00);
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xFA, 0xAF);
    
    kernel_memset(&tss, 0, sizeof(struct tss_entry_t));
    tss.rsp0 = 0xFFFFFFFF80090000;
    
    tss.iopb_offset = 104; 
    
    kernel_memset(tss.io_bitmap, 0xFF, sizeof(tss.io_bitmap));
    
    tss.iopb_end = 0xFF;   
    
    write_tss(5, (uint64_t)&tss, sizeof(tss) - 1);
    
    __asm__ volatile("lgdt (%0)" : : "r"(&gp));
    __asm__ volatile(
        "mov $0x10, %ax \n"
        "mov %ax, %ds \n"
        "mov %ax, %es \n"
        "mov %ax, %fs \n"
        "mov %ax, %gs \n"
        "mov %ax, %ss \n"
    );
    __asm__ volatile("ltr %%ax" :: "a" (0x28));
    
    hal_wrmsr(0xC0000100, (uint64_t)&kernel_tcb);
    hal_wrmsr(0xC0000101, (uint64_t)&kernel_tcb);
    hal_wrmsr(0xC0000102, (uint64_t)&kernel_tcb);
}

static void fpu_init() {
    uint64_t cr0, cr4;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1 << 2);
    cr0 |= (1 << 1);
    cr0 |= (1 << 5);
    asm volatile("mov %0, %%cr0" :: "r"(cr0));
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1 << 9);
    cr4 |= (1 << 10);
    asm volatile("mov %0, %%cr4" :: "r"(cr4));
    
    uint32_t mxcsr = 0x1F80;
    asm volatile("ldmxcsr %0" :: "m"(mxcsr));
    asm volatile("fninit");
    asm volatile("fxsave %0" : "=m"(default_fpu_state));
}

void hal_cpu_init(void) {
    gdt_install();
    fpu_init();

    uint32_t eax = 7, ebx, ecx = 0, edx;
    asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(eax), "c"(ecx));
    
    uint64_t cr4;
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    
    if (ebx & 1) { // FSGSBASE
        extern st_flags_t state;
        state.cpu_flags |= 1; // FSGSBASE
        cr4 |= 0x10000;
    }
    if (ebx & (1 << 7)) { // SMEP
        cr4 |= (1 << 20);
    }
    
    asm volatile("mov %0, %%cr4" :: "r"(cr4));

    kernel_tcb.canary = hal_get_random_seed() ^ 0xDEADBEEFCAFEBABEULL;
}

void hal_set_io_permissions(uint32_t pid) {
	driver_info_t* drv = get_driver_by_pid(pid);
    if (!drv || !(drv->driver_perms & DRV_PERM_IO_PORTS)) {
        kernel_memset(tss.io_bitmap, 0xFF, sizeof(tss.io_bitmap));
        return;
    }

    if (drv->allowed_ports[0] == 0xFFFF) {
        kernel_memset(tss.io_bitmap, 0x00, sizeof(tss.io_bitmap));
        return;
    }

    kernel_memset(tss.io_bitmap, 0xFF, sizeof(tss.io_bitmap));

    for (int i = 0; i < 8; i++) {
        uint16_t port = drv->allowed_ports[i];
        if (port != 0) {
            int byte_index = port / 8;
            int bit_index  = port % 8;
            
            tss.io_bitmap[byte_index] &= ~(1 << bit_index);
        }
    }
}