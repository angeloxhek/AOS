#include "include/kernel_internal.h"

extern void switch_to_task(thread_t* current, thread_t* next);
extern void trampoline_enter_user();
extern void trampoline_enter_kernel();

// -------------------------
//        Scheduler
// -------------------------

void init_scheduler() {
    kernel_memset(&kernel_process, 0, sizeof(process_t));
    kernel_process.id = 0;
    kernel_memcpy(kernel_process.name, "KERNEL", 6);
    kernel_process.page_directory = (uint64_t*)get_current_pml4();
    kernel_process.entry_point = (uint64_t)kernel_main;
    thread_t* kthread = (thread_t*)kernel_malloc(sizeof(thread_t));
    if (!kthread) kernel_error(0x5, 0, 0, 0, 0);
    kernel_memset(kthread, 0, sizeof(thread_t));
    kthread->stack_base = (uint64_t)kernel_stack;
    uint64_t current_rsp;
    asm volatile("mov %%rsp, %0" : "=r"(current_rsp));
    kthread->rsp = current_rsp;
    kthread->cr3 = get_current_pml4();
    kthread->state = 1;
    kthread->owner = &kernel_process;
    kthread->next = kthread;
    kthread->tid = thread_count;
    thread_count++;
    current_thread = kthread;
    ready_queue = kthread;
}

thread_t* create_thread_core(uint64_t cr3, process_t* owner) {
    thread_t* t = (thread_t*)kernel_malloc(sizeof(thread_t));
    if (!t) return 0;
    kernel_memset(t, 0, sizeof(thread_t));
    kernel_memcpy(t->fpu_state, default_fpu_state, 512);
    void* stack = kernel_malloc(KERNEL_STACK_SIZE);
    if (!stack) return 0;
    kernel_memset(stack, 0, KERNEL_STACK_SIZE);
    t->stack_base = (uint64_t)stack;
    t->rsp = (uint64_t)stack + KERNEL_STACK_SIZE;
    t->cr3 = cr3;
    t->state = THREAD_READY;
    t->tid = thread_count;
    thread_count++;
    t->owner = owner;
    if (ready_queue == 0) {
        ready_queue = t;
        t->next = t;
    } else {
        t->next = ready_queue->next;
        ready_queue->next = t;
    }
    return t;
}

void create_user_thread(uint64_t entry_point, uint64_t user_stack, uint64_t cr3_phys, process_t* proc, uint64_t arg1, uint64_t arg2) {
    asm volatile("cli");
    thread_t* t = create_thread_core(cr3_phys, proc);

    uint64_t tcb_size = 0x30;
    uint64_t total_tls_size = proc->tls_mem_size + tcb_size;

    uint64_t alloc_pages = (total_tls_size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t tls_virt = 0x00007FFFFF000000;

    for (uint64_t i = 0; i < alloc_pages * PAGE_SIZE; i += PAGE_SIZE) {
        uint64_t phys = pmm_alloc_block();
        map_to_other_pml4((uint64_t*)cr3_phys, phys, tls_virt + i, PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
    }

    uint64_t old_cr3 = get_current_pml4();
    set_current_pml4(cr3_phys);

    if (proc->tls_mem_size > 0) {
        kernel_memcpy((void*)tls_virt, (void*)proc->tls_image_vaddr, proc->tls_file_size);
        kernel_memset((void*)(tls_virt + proc->tls_file_size), 0, proc->tls_mem_size - proc->tls_file_size);
    }

    uint64_t fs_base = tls_virt + proc->tls_mem_size;

    aos_tcb_t* tcb = (aos_tcb_t*)fs_base;

    tcb->tcb_self     = (void*)fs_base;
    tcb->tid          = t->tid;
    tcb->pid          = t->owner->id;
    tcb->thread_errno = 0;
    tcb->pending_msgs = 0;
    tcb->local_heap   = (void*)0;
    {
        uint32_t lo, hi;
        asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
        uint64_t rdtsc_val = ((uint64_t)hi << 32) | lo;
        tcb->stack_canary = rdtsc_val ^ 0x595e9fbd94fda766ULL ^ (uint64_t)t->tid;
    }

    set_current_pml4(old_cr3);
    t->fs_base = fs_base;

    uint64_t* sp = (uint64_t*)t->rsp;
    *(--sp) = GDT_USER_DATA;   // SS
    *(--sp) = user_stack;      // RSP (User)
    *(--sp) = 0x202;           // RFLAGS (IF=1)
    *(--sp) = GDT_USER_CODE;   // CS
    *(--sp) = entry_point;     // RIP
	*(--sp) = arg2;
	*(--sp) = arg1;
    *(--sp) = (uint64_t)trampoline_enter_user;
    *(--sp) = 0x202; // RFLAGS
    *(--sp) = 0;     // R15
    *(--sp) = 0;     // R14
    *(--sp) = 0;     // R13
    *(--sp) = 0;     // R12
    *(--sp) = 0;     // RBP
    *(--sp) = 0;     // RBX

    t->rsp = (uint64_t)sp;
    asm volatile("sti");
}

void create_kernel_thread(void (*entry)(void)) {
    asm volatile("cli");
    thread_t* t = create_thread_core(get_current_pml4(), &kernel_process);

    uint64_t* sp = (uint64_t*)t->rsp;

    *(--sp) = (uint64_t)entry;

    *(--sp) = (uint64_t)trampoline_enter_kernel;

    *(--sp) = 0x202; // RFLAGS
    *(--sp) = 0; // R15 ...
    *(--sp) = 0;
    *(--sp) = 0;
    *(--sp) = 0;
    *(--sp) = 0; // RBP
    *(--sp) = 0; // RBX

    t->rsp = (uint64_t)sp;
    asm volatile("sti");
}

void schedule() {
    thread_t* t = ready_queue;
    do {
        if (t->state == THREAD_BLOCKED && t->wake_up_time > 0) {
            if (ticks >= t->wake_up_time) {
                t->state = THREAD_READY;
                t->wake_up_time = 0;
            }
        }
        t = t->next;
    } while (t != ready_queue);

    thread_t* prev = current_thread;
    thread_t* next = current_thread->next;
    while (next->state > 1 && next != prev) {
        next = next->next;
    }
    if (next->state > 1) {
        next = get_thread_by_id(1);
    }
    if (next == prev) return;
    current_thread = next;
    if (prev->state == THREAD_RUNNING) prev->state = THREAD_READY;
    next->state = THREAD_RUNNING;
    tss.rsp0 = next->stack_base + KERNEL_STACK_SIZE;
    kernel_tcb.kernel_rsp = next->stack_base + KERNEL_STACK_SIZE;
    switch_to_task(prev, next);
}

int kill_thread(thread_t* target, int exit_code) {
    if (target->tid == 1) return SYS_RES_NO_PERM;
    asm volatile("cli");
    target->state = THREAD_ZOMBIE;
    target->exit_code = exit_code;
    thread_t* prev = target;
    while (prev->next != target) {
        prev = prev->next;
        if (prev == target) break;
    }
    if (prev == target) return SYS_RES_NO_PERM;
    prev->next = target->next;
    if (ready_queue == target) {
        ready_queue = target->next;
    }
    target->next_zombie = zombies_list;
    zombies_list = target;
    asm volatile("sti");
    return SYS_RES_OK;
}

thread_t* get_thread_by_id(uint64_t tid) {
    thread_t* t = ready_queue;
    if (!t) return 0;
    do {
        if (t->tid == tid) return t;
        t = t->next;
    } while (t != ready_queue);
    return 0;
}

process_t* get_process_by_id(uint32_t pid) {
    thread_t* t = ready_queue;
    if (!t) return 0;
    do {
        if (t->owner && t->owner->id == pid) {
            return t->owner;
        }
        t = t->next;
    } while (t != ready_queue);
    return 0;
}

int get_driver_tid_sleep_wrapper(void* arg) {
    return get_driver_tid(*(driver_type_t*)arg);
}

void sleep(uint64_t ms) {
    uint64_t ticks_to_wait = ms / 10;
    if (ticks_to_wait == 0 && ms > 0) ticks_to_wait = 1;

    asm volatile("cli");
    current_thread->wake_up_time = ticks + ticks_to_wait;
    current_thread->state = THREAD_BLOCKED;

    schedule();
    asm volatile("sti");
}

int sleep_while_zero(int (*func)(void*), void* arg, uint64_t timeout_ms, int* out_result) {
    int attempts = 0;
    int res = func(arg);
    uint64_t start_tick = ticks;
    uint64_t timeout_ticks = timeout_ms / 10;
    if (timeout_ticks == 0 && timeout_ms > 0) timeout_ticks = 1;
    while (res == 0) {
        if (attempts < 100) {
            attempts++;
            asm volatile("pause");
            res = func(arg);
            if (res != 0) break;
            continue;
        }
        if (timeout_ms != 0) {
            uint64_t current = ticks;
            uint64_t elapsed = (current >= start_tick) ? (current - start_tick) : (0xFFFFFFFFFFFFFFFF - start_tick + current);
            if (elapsed >= timeout_ticks) {
                if (out_result) *out_result = 0;
                return 0;
            }
        }
        sleep(10);
        res = func(arg);
    }
    if (out_result) *out_result = res;
    return 1;
}

void get_time_info(time_info_t* out) {
	if (!out) return;
	out->uptime = ticks;
	out->boot_time = boot_time;
	out->frequency = TIMER_FREQ;
}
