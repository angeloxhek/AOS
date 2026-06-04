#include <kernel/internal.h>

// -------------------------
//        Scheduler
// -------------------------

void init_scheduler() {
    kernel_memset(&kernel_process, 0, sizeof(process_t));
    kernel_process.id = 0;
    kernel_memcpy(kernel_process.name, "KERNEL", 6);
    kernel_process.page_directory = (uint64_t*)hal_get_current_address_space();
    
    thread_t* kthread = (thread_t*)kernel_malloc(sizeof(thread_t));
    if (!kthread) kernel_error(0x5, 0, 0, 0, 0);
    kernel_memset(kthread, 0, sizeof(thread_t));
    kthread->stack_base = (uint64_t)kernel_stack;
    
    hal_init_current_thread(kthread);
    
    kthread->state = 1;
    kthread->owner = &kernel_process;
    kthread->next = kthread;
    kthread->tid = thread_count;
    thread_count++;
    current_thread = kthread;
    ready_queue = kthread;
}

thread_t* create_thread_core(uint64_t root_phys, process_t* owner) {
    thread_t* t = (thread_t*)kernel_malloc(sizeof(thread_t));
    if (!t) return 0;
    kernel_memset(t, 0, sizeof(thread_t));
    
    void* stack = kernel_malloc(KERNEL_STACK_SIZE);
    if (!stack) {
        kernel_free(t);
        return 0;
    }
    kernel_memset(stack, 0, KERNEL_STACK_SIZE);
    t->stack_base = (uint64_t)stack;
    
    t->rsp = (uint64_t)stack + KERNEL_STACK_SIZE; 
    t->cr3 = root_phys;
    
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

thread_t* create_user_thread(uint64_t entry_point, uint64_t user_stack, uint64_t cr3_phys, process_t* proc, uint64_t arg1, uint64_t arg2) {
    uint64_t irq = hal_irq_save();
    thread_t* t = create_thread_core(cr3_phys, proc);

    uint64_t tcb_size = 0x30;
    uint64_t total_tls_size = proc->tls_mem_size + tcb_size;

    uint64_t alloc_pages = (total_tls_size + 4096 - 1) / 4096;
    uint64_t tls_virt = 0x00007FFFFF000000;

    for (uint64_t i = 0; i < alloc_pages * 4096; i += 4096) {
        uint64_t phys = pmm_alloc_block();
        hal_map_page_in_space(cr3_phys, tls_virt + i, phys, 0x7); // PRESENT | WRITE | USER
    }

    uint64_t old_space = hal_get_current_address_space();
    hal_set_current_address_space(cr3_phys);

    if (proc->tls_mem_size > 0) {
        kernel_memcpy((void*)tls_virt, (void*)proc->tls_image_vaddr, proc->tls_file_size);
        kernel_memset((void*)(tls_virt + proc->tls_file_size), 0, proc->tls_mem_size - proc->tls_file_size);
    }

    uint64_t fs_base = tls_virt + proc->tls_mem_size;
    aos_tcb_t* tcb = (aos_tcb_t*)fs_base;

    tcb->tcb_self     = (void*)fs_base;
    tcb->tid          = t->tid;
    tcb->pid          = t->owner->id;
    tcb->stack_canary = hal_get_random_seed() ^ 0x595e9fbd94fda766ULL ^ (uint64_t)t->tid;

    hal_set_current_address_space(old_space);
    
    t->fs_base = fs_base;
	uint64_t tcb_virt_page = fs_base & ~4095ULL;
    t->tcb_page_offset     = fs_base & 4095ULL;
    t->tcb_phys_page = hal_get_phys(cr3_phys, tcb_virt_page);

    hal_setup_user_thread(t, entry_point, user_stack, arg1, arg2);
    
    hal_irq_restore(irq);
	return t;
}

thread_t* create_kernel_thread(void (*entry)(void)) {
    uint64_t irq = hal_irq_save();
    thread_t* t = create_thread_core(hal_get_current_address_space(), &kernel_process);
    
    hal_setup_kernel_thread(t, (uint64_t)entry);
    
    hal_irq_restore(irq);
	return t;
}

void schedule() {
    if (ready_queue) {
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
    }

    thread_t* prev = current_thread;
    thread_t* next = 0;

    if (ready_queue) {
        
        thread_t* start_node = ready_queue;
        
        if (prev->state != THREAD_ZOMBIE && prev != idle_thread_ptr) {
            if (prev->next != 0) {
                start_node = prev->next;
            }
        }

        thread_t* temp = start_node;
        if (temp) {
            do {
                if (temp->state == THREAD_READY || temp->state == THREAD_RUNNING) {
                    next = temp;
                    break;
                }
                temp = temp->next;
            } while (temp != start_node);
        }
    }

    if (!next) {
        next = idle_thread_ptr;
    }

    if (next == prev) {
        return;
    }
    
    current_thread = next;
    
    if (prev->state == THREAD_RUNNING) {
        prev->state = THREAD_READY;
    }
    next->state = THREAD_RUNNING;
    
    hal_switch_task(prev, next);
}

void yield(void) {
    uint64_t irq = hal_irq_save();
    schedule();
    hal_irq_restore(irq);
}

int kill_thread(thread_t* target, int exit_code) {
    if (target->tid == 1) return SYS_RES_NO_PERM;
    uint64_t irq = hal_irq_save();
    
    target->state = THREAD_ZOMBIE;
    target->exit_code = exit_code;

    if (target->next == target) {
        if (ready_queue == target) ready_queue = 0;
    } else {
        thread_t* prev = target;
        while (prev->next != target) {
            prev = prev->next;
        }
        prev->next = target->next;
        if (ready_queue == target) {
            ready_queue = target->next;
        }
    }
	
	target->next = 0; 
    target->next_zombie = zombies_list;
    zombies_list = target;
    
    hal_irq_restore(irq);
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

process_t* get_process_by_id(apid_t pid) {
    thread_t* t = ready_queue;
    if (!t) return 0;
    do {
        if (t->owner && t->owner->id == pid) return t->owner;
        t = t->next;
    } while (t != ready_queue);
    return 0;
}

uint64_t get_thread_list(apid_t target_pid, uint64_t* user_buffer, uint64_t* max_elements) {
	if (!user_buffer || !max_elements) return 0;
	uint64_t count = 0;
	uint64_t mx = *max_elements;
	thread_t* t = ready_queue;
	*max_elements = 0;
	if (t) {
		do {
			if (count < mx && (target_pid == (apid_t)-1 || t->owner->id == target_pid)) {
				user_buffer[count++] = t->tid;
			}
			t = t->next;
			(*max_elements)++;
		} while (t != ready_queue);
	}
	return count;
}

uint64_t get_proc_list(apid_t* user_buffer, uint64_t* max_elements) {
	if (!user_buffer || !max_elements) return 0;
	uint64_t count = 0;
	thread_t* t = ready_queue;
	uint64_t mx = *max_elements;
	*max_elements = 0;
	if (t) {
		do {
			apid_t pid = t->owner->id;
			int is_duplicate = 0;
			for (uint64_t i = 0; i < count; i++) {
				if (user_buffer[i] == pid) {
					is_duplicate = 1;
					break;
				}
			}
			if (!is_duplicate && count < mx) {
				user_buffer[count++] = pid;
			}
			t = t->next;
			(*max_elements)++;
		} while (t != ready_queue);
	}
	return count;
}

int get_driver_pid_sleep_wrapper(void* arg) {
    return get_driver_pid(*(driver_type_t*)arg);
}

void sleep(uint64_t ms) {
    uint64_t ticks_to_wait = ms / 10;
    if (ticks_to_wait == 0 && ms > 0) ticks_to_wait = 1;

    uint64_t irq = hal_irq_save();
    current_thread->wake_up_time = ticks + ticks_to_wait;
    current_thread->state = THREAD_BLOCKED;

    schedule();
    hal_irq_restore(irq);
}

int sleep_while_zero(int (*func)(void*), void* arg, uint64_t timeout_ms, int* out_result) {
    int res = func(arg);
    if (res != 0) {
        if (out_result) *out_result = res;
        return 1;
    }

    uint64_t start_tick = ticks;
    uint64_t timeout_ticks = timeout_ms / 10;
    if (timeout_ticks == 0 && timeout_ms > 0) timeout_ticks = 1;

    int spin_count = 0;

    while (1) {
        res = func(arg);
        if (res != 0) break;

        if (spin_count < 1000) {
            spin_count++;
            hal_cpu_relax();
            continue;
        }

        if (timeout_ms != 0) {
            uint64_t elapsed = ticks - start_tick; 
            
            if (elapsed >= timeout_ticks) {
                if (out_result) *out_result = 0;
                return 0;
            }
        }
		
		yield();
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