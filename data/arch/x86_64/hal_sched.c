#include "hal_arch.h"

extern void switch_to_task(thread_t* prev, thread_t* next);
extern void trampoline_enter_user(void);
extern void trampoline_enter_kernel(void);
extern void kernel_memcpy(void* dest, const void* src, uint64_t n);
extern struct tss_entry_t tss;
extern kernel_tcb_t kernel_tcb;

extern uint8_t default_fpu_state[512]; 

// Сегменты GDT для x86_64
// В Long Mode сегментация почти отключена, но селекторы важны для CPL (Rings).
// Типичная раскладка для поддержки syscall/sysret:
// 0x00 - Null
// 0x08 - Kernel Code
// 0x10 - Kernel Data
// 0x18 - User Data (или User Code 32-bit compat)
// 0x20 - User Data
// 0x28 - User Code 64-bit
#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_DATA   0x1B
#define GDT_USER_CODE   0x23

void hal_cpu_relax(void) {
    asm volatile("pause" ::: "memory");
}

uint64_t hal_get_random_seed(void) {
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

uint64_t hal_irq_save(void) {
    uint64_t flags;
    asm volatile("pushfq; pop %0; cli" : "=r"(flags) :: "memory");
    return flags;
}

void hal_irq_restore(uint64_t flags) {
    if (flags & 0x200) {
        asm volatile("sti" ::: "memory");
    }
}

void hal_init_current_thread(thread_t* t) {
    uint64_t current_rsp;
    asm volatile("mov %%rsp, %0" : "=r"(current_rsp));
    t->rsp = current_rsp;
    t->cr3 = hal_get_current_address_space();
    kernel_memcpy(t->fpu_state, default_fpu_state, 512);
}

void hal_setup_user_thread(thread_t* t, uint64_t entry_point, uint64_t user_stack, uint64_t arg1, uint64_t arg2) {
    kernel_memcpy(t->fpu_state, default_fpu_state, 512);
    
    uint64_t* sp = (uint64_t*)t->rsp;
    
    *(--sp) = GDT_USER_DATA;   // SS
    *(--sp) = user_stack;      // RSP (User)
    *(--sp) = 0x202;           // RFLAGS (IF=1)
    *(--sp) = GDT_USER_CODE;   // CS
    *(--sp) = entry_point;     // RIP
    
    *(--sp) = arg2;            // RSI
    *(--sp) = arg1;            // RDI
    
    *(--sp) = (uint64_t)trampoline_enter_user;
    
    *(--sp) = 0x202; // RFLAGS (начальный)
    *(--sp) = 0;     // R15
    *(--sp) = 0;     // R14
    *(--sp) = 0;     // R13
    *(--sp) = 0;     // R12
    *(--sp) = 0;     // RBP
    *(--sp) = 0;     // RBX

    t->rsp = (uint64_t)sp;
}

void hal_setup_kernel_thread(thread_t* t, uint64_t entry_point) {
    kernel_memcpy(t->fpu_state, default_fpu_state, 512);
    
    uint64_t* sp = (uint64_t*)t->rsp;

    *(--sp) = entry_point;
    *(--sp) = (uint64_t)trampoline_enter_kernel;
    
    *(--sp) = 0x202; // RFLAGS (IF=1)
    *(--sp) = 0; // R15
    *(--sp) = 0; // R14
    *(--sp) = 0; // R13
    *(--sp) = 0; // R12
    *(--sp) = 0; // RBP
    *(--sp) = 0; // RBX

    t->rsp = (uint64_t)sp;
}

void hal_switch_task(thread_t* prev, thread_t* next) {
    tss.rsp0 = next->stack_base + 16384; // KERNEL_STACK_SIZE
    kernel_tcb.kernel_rsp = next->stack_base + 16384;
	
	hal_set_io_permissions(next->owner->id);
    
    switch_to_task(prev, next);
}