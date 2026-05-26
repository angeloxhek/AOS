#include "hal_arch.h"

extern void syscall_entry(void);
extern void generic_syscall_handler(syscall_args_t* args);
extern kernel_tcb_t kernel_tcb;

#define MSR_EFER    0xC0000080
#define MSR_STAR    0xC0000081
#define MSR_LSTAR   0xC0000082
#define MSR_SFMASK  0xC0000084

#define KERNEL_CODE_SEG 0x08
#define USER_SEG_BASE   0x10

void hal_syscall_init(void) {
    extern uint8_t kernel_stack[];
    kernel_tcb.kernel_rsp = (uint64_t)(kernel_stack + 16384);
    
    uint64_t efer = hal_rdmsr(MSR_EFER);
    hal_wrmsr(MSR_EFER, efer | 1 | (1 << 11)); // SCE + NXE
    uint64_t star = ((uint64_t)USER_SEG_BASE << 48) | ((uint64_t)KERNEL_CODE_SEG << 32);
    hal_wrmsr(MSR_STAR, star);
    hal_wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);
    hal_wrmsr(MSR_SFMASK, 0x200);
}

int hal_is_valid_user_pointer(const void* ptr) {
    uint64_t addr = (uint64_t)ptr;
    if (addr >= 0x800000000000) return 0; // Kernel space x86
    if (ptr == 0) return 0; // NULL
    return 1;
}

uint64_t hal_get_user_tls_base(void) {
    return hal_rdmsr(0xC0000102); // KernelGSBase
}

uint64_t hal_get_kernel_tls_base(void) {
    return hal_rdmsr(0xC0000101); // GSBase
}

void hal_prepare_fork_context(thread_t* parent, thread_t* child) {
    kernel_memcpy((void*)child->stack_base, (void*)parent->stack_base, 16384); // KERNEL_STACK_SIZE
    
    uint64_t stack_diff = child->stack_base - parent->stack_base;
    child->rsp = parent->rsp + stack_diff;
    
    syscall_regs_t* child_regs = (syscall_regs_t*)child->rsp;
    child_regs->rax = 0;
}

void hal_set_exec_context(void* arch_context, uint64_t entry_point, uint64_t user_stack, uint64_t arg1, uint64_t arg2) {
    syscall_regs_t* regs = (syscall_regs_t*)arch_context;
    regs->rcx = entry_point;
    regs->rsp = user_stack;
    regs->rdi = arg1;
    regs->rsi = arg2;
}

void syscall_handler(syscall_regs_t* regs) {
    syscall_args_t args;
    args.syscall_nr = regs->rax;
    args.arg1 = regs->rdi;
    args.arg2 = regs->rsi;
    args.arg3 = regs->rdx;
    args.arg4 = regs->r10;
    args.arch_context = regs;

    generic_syscall_handler(&args);

    regs->rax = args.ret;
}