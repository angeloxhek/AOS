#include <kernel/internal.h>

// -------------------------
//        Syscall
// -------------------------

int copy_string_from_user(const char* user_src, char* kernel_dest, int max_len) {
    if (!hal_is_valid_user_pointer(user_src)) return 0;
    int i;
    for (i = 0; i < max_len; i++) {
        kernel_dest[i] = user_src[i];
        if (user_src[i] == 0) return i + 1;
    }
    kernel_dest[max_len - 1] = 0;
    return max_len;
}

void generic_syscall_handler(syscall_args_t* args) {
    uint64_t syscall_nr = args->syscall_nr;
    switch (syscall_nr) {
        case SYS_PRINT: {
            const char* user_msg = (const char*)args->arg1;
            if (!hal_is_valid_user_pointer(user_msg)) {
                args->ret = SYS_RES_INVALID;
                break;
            }
            kprint(user_msg);
            args->ret = SYS_RES_OK;
            break;
        }
        case SYS_EXIT: {
            int exit_code = (int)args->arg1;
            uint64_t tid = args->arg2;
            thread_t* th;
            if (tid == 0) { th = current_thread; }
            else {
                th = get_thread_by_id(tid);
                if (th == 0) {
                    args->ret = SYS_RES_INVALID;
                    break;
                }
            }
            args->ret = kill_thread(th, exit_code);
            if (tid == 0) schedule();
            break;
        }
        case SYS_IPC_SEND: {
            uint64_t dest = args->arg1;
            message_t* user_msg = (message_t*)args->arg2;
            if (!hal_is_valid_user_pointer(user_msg)) {
                args->ret = SYS_RES_INVALID;
                break;
            }
            args->ret = ipc_send(dest, user_msg);
            break;
        }
        case SYS_IPC_RECV: {
            message_t* user_msg_buffer = (message_t*)args->arg1;
            if (!hal_is_valid_user_pointer(user_msg_buffer)) {
                args->ret = SYS_RES_INVALID;
                break;
            }
            args->ret = ipc_receive(user_msg_buffer);
            break;
        }
        case SYS_IPC_TRYRECV: {
            message_t* user_msg_buffer = (message_t*)args->arg1;
            if (!hal_is_valid_user_pointer(user_msg_buffer)) {
                args->ret = SYS_RES_INVALID;
                break;
            }
            args->ret = ipc_try_receive(user_msg_buffer);
            break;
        }

        case SYS_GET_DRIVER_PID:
            args->ret = get_driver_pid((driver_type_t)args->arg1);
            break;

        case SYS_GET_DRIVER_PID_BY_NAME: {
            const char* name_ptr = (const char*)args->arg1;
            if (!hal_is_valid_user_pointer(name_ptr)) {
                args->ret = SYS_RES_INVALID;
                break;
            }
            args->ret = get_driver_pid_by_name(name_ptr);
            break;
        }

        case SYS_GET_SYSTEM_INFO: {
            system_info_t* info = (system_info_t*)args->arg1;
            if (hal_is_valid_user_pointer(info)) {
                info->flags = state.system_flags;
                info->cpu_flags = state.cpu_flags;
                info->uptime = ticks;
                info->fs_base = current_thread->fs_base;
                info->gs_base = hal_get_user_tls_base();
                info->kernel_gs_base = hal_get_kernel_tls_base();
                args->ret = SYS_RES_OK;
            } else {
                args->ret = SYS_RES_INVALID;
            }
            break;
        }

        case SYS_SBRK: {
            uint64_t irq = hal_irq_save();
            int64_t increment = (int64_t)args->arg1;
            process_t* proc = current_thread->owner;
            uint64_t old_brk = proc->heap_limit;
            if (increment == 0) {
                args->ret = old_brk;
                hal_irq_restore(irq);
                break;
            }
            uint64_t new_brk = old_brk + increment;
            uint64_t old_page_limit = (old_brk + 4095) & ~4095ULL;
            uint64_t new_page_limit = (new_brk + 4095) & ~4095ULL;
            if (new_page_limit > old_page_limit) {
                uint64_t pages_needed = (new_page_limit - old_page_limit) / 4096;
                for (uint64_t i = 0; i < pages_needed; i++) {
                    uint64_t phys_addr = pmm_alloc_block();
                    if (!phys_addr) {
                        args->ret = SYS_RES_KERNEL_ERR;
                        hal_irq_restore(irq);
                        return;
                    }
                    hal_map_page_in_space((uint64_t)proc->page_directory, old_page_limit + (i * 4096), phys_addr, 0x7);
                    void* ptr = temp_map(phys_addr);
                    kernel_memset(ptr, 0, 4096);
                    temp_unmap(ptr);
                }
            }
            proc->heap_limit = new_brk;
            args->ret = old_brk;
            hal_irq_restore(irq);
            break;
        }

        case SYS_GET_PROC_INFO: {
            apid_t target_pid = (apid_t)args->arg1;
            proc_info_user_t* user_ptr = (proc_info_user_t*)args->arg2;

            if (!hal_is_valid_user_pointer(user_ptr)) {
                args->ret = SYS_RES_INVALID;
                break;
            }

            process_t* target_proc = 0;
            thread_t* t = ready_queue;
            if (t) {
                do {
                    if (t->owner->id == target_pid) {
                        target_proc = t->owner;
                        break;
                    }
                    t = t->next;
                } while (t != ready_queue);
            }

            if (!target_proc) {
                args->ret = SYS_RES_NOTFOUND;
                break;
            }

            proc_info_user_t info;
            kernel_memset(&info, 0, sizeof(proc_info_user_t));
            info.pid = target_proc->id;
            kernel_memcpy(info.name, target_proc->name, 32);
            info.state = target_proc->state;
            info.heap_limit = target_proc->heap_limit;
			info.user.raw = target_proc->user.raw;

            info.threads_count = 0;
            t = ready_queue;
            if (t) {
                do {
                    if (t->owner->id == target_pid) info.threads_count++;
                    t = t->next;
                } while (t != ready_queue);
            }

            kernel_memcpy(user_ptr, &info, sizeof(proc_info_user_t));
            args->ret = SYS_RES_OK;
            break;
        }

        case SYS_GET_THREAD_INFO: {
            uint64_t target_tid = args->arg1;
            thread_info_user_t* user_ptr = (thread_info_user_t*)args->arg2;

            if (!hal_is_valid_user_pointer(user_ptr)) {
                args->ret = SYS_RES_INVALID;
                break;
            }

            thread_t* target_thread = get_thread_by_id(target_tid);
            if (!target_thread) {
                thread_t* z = zombies_list;
                while (z) {
                    if (z->tid == target_tid) { target_thread = z; break; }
                    z = z->next_zombie;
                }
            }
            if (!target_thread) {
                args->ret = SYS_RES_NOTFOUND;
                break;
            }

            thread_info_user_t info;
            kernel_memset(&info, 0, sizeof(thread_info_user_t));
            info.tid = target_thread->tid;
            info.parent_pid = target_thread->owner->id;
            info.state = target_thread->state;
            info.waiting_for_msg = target_thread->waiting_for_msg;
            info.wake_up_time = target_thread->wake_up_time;

            kernel_memcpy(user_ptr, &info, sizeof(thread_info_user_t));
            args->ret = SYS_RES_OK;
            break;
        }

        case SYS_SHM_ALLOC: {
            uint64_t size = args->arg1;
            uint64_t* user_out_vaddr = (uint64_t*)args->arg2;

            uint64_t out_vaddr = 0;
            uint64_t shm_id = shm_alloc(size, &out_vaddr);

            if (shm_id != 0 && user_out_vaddr != 0 && hal_is_valid_user_pointer(user_out_vaddr)) {
                *user_out_vaddr = out_vaddr;
            }
            args->ret = shm_id;
            break;
        }

        case SYS_SHM_ALLOW:
            args->ret = shm_allow(args->arg1, (apid_t)args->arg2);
            break;

        case SYS_SHM_MAP:
            args->ret = shm_map(args->arg1);
            break;

        case SYS_SHM_FREE:
            args->ret = shm_free(args->arg1);
            break;
        
        case SYS_YIELD:
            schedule();
            args->ret = SYS_RES_OK;
            break;
        
        case SYS_GET_PID_LIST: {
            apid_t* user_buffer = (apid_t*)args->arg1;
            uint64_t* max_elements = (uint64_t*)args->arg2;

            if (!hal_is_valid_user_pointer(user_buffer) || !hal_is_valid_user_pointer(max_elements)) {
                args->ret = SYS_RES_INVALID;
                break;
            }

            args->ret = get_proc_list(user_buffer, max_elements);
            break;
        }

        case SYS_GET_TID_LIST: {
            apid_t target_pid = (apid_t)args->arg1;
            uint64_t* user_buffer = (uint64_t*)args->arg2;
            uint64_t* max_elements = (uint64_t*)args->arg3;

            if (!hal_is_valid_user_pointer(user_buffer) || !hal_is_valid_user_pointer(max_elements)) {
                args->ret = SYS_RES_INVALID;
                break;
            }
			
            args->ret = get_thread_list(target_pid, user_buffer, max_elements);
            break;
        }
        
        case SYS_GET_TIME_INFO: {
            time_info_t* info = (time_info_t*)args->arg1;
            if (!hal_is_valid_user_pointer(info)) {
                args->ret = SYS_RES_INVALID;
                break;
            }
            get_time_info(info);
            args->ret = SYS_RES_OK;
            break;
        }
        
        case SYS_SPAWN: {
            spawn_args_t* user_sargs = (spawn_args_t*)args->arg1;
            
            if (!hal_is_valid_user_pointer(user_sargs)) {
                args->ret = SYS_RES_INVALID;
                break;
            }
            
            spawn_args_t kargs;
            kernel_memcpy(&kargs, user_sargs, sizeof(spawn_args_t));
            
            if (!hal_is_valid_user_pointer(kargs.name) || 
                !hal_is_valid_user_pointer(kargs.data) || 
                (kargs.info && !hal_is_valid_user_pointer(kargs.info))) {
                args->ret = SYS_RES_INVALID;
                break;
            }
            
            char name[32];
            kernel_memset(name, 0, 32);
            for (int i = 0; i < 31; i++) {
                name[i] = kargs.name[i];
                if (name[i] == '\0') break;
            }
            
            elf_load_result_t elf;
            load_elf_raw(name, kargs.data, kargs.size, &elf);
            
            if (elf.result != ELF_RESULT_OK || start_elf_process(&elf, kargs.info, kargs.arg_val) != 0) {
                args->ret = SYS_RES_KERNEL_ERR;
                break;
            }
            args->ret = SYS_RES_OK;
            break;
        }
        
        case SYS_FORK: {
            process_t* parent = current_thread->owner;
            process_t* child = create_process(parent->name);
            if (!child) {
                args->ret = SYS_RES_KERNEL_ERR;
                break;
            }

            hal_copy_address_space(parent->page_directory, child->page_directory);

            thread_t* child_thread = create_thread_core((uint64_t)child->page_directory, child);
            
            hal_prepare_fork_context(current_thread, child_thread);
            
            child_thread->state = THREAD_READY;
            
            args->ret = child->id;
            break;
        }
        
        case SYS_EXEC: {
            spawn_args_t* user_sargs = (spawn_args_t*)args->arg1;
            
            if (!hal_is_valid_user_pointer(user_sargs)) {
                args->ret = SYS_RES_INVALID;
                break;
            }
            
            spawn_args_t kargs;
            kernel_memcpy(&kargs, user_sargs, sizeof(spawn_args_t));
            
            if (!hal_is_valid_user_pointer(kargs.name) || 
                !hal_is_valid_user_pointer(kargs.data) || 
                (kargs.info && !hal_is_valid_user_pointer(kargs.info))) {
                args->ret = SYS_RES_INVALID;
                break;
            }
            
            uint8_t* k_data = (uint8_t*)kernel_malloc(kargs.size);
            if (!k_data) {
                args->ret = SYS_RES_KERNEL_ERR;
                break;
            }
            kernel_memcpy(k_data, kargs.data, kargs.size);
            
            process_t* proc = current_thread->owner;
            
            hal_destroy_address_space(proc); 
            
            elf_load_result_t elf;
            load_elf_raw_proc(proc, k_data, kargs.size, &elf);
            
            kernel_free(k_data); 
            
            if (elf.result != ELF_RESULT_OK) {
                kill_thread(current_thread, -1);
                args->ret = SYS_RES_KERNEL_ERR;
                break;
            }
            
            uint64_t info_addr = 0;
            if (kargs.info) {
                info_addr = (uint64_t)prepare_child_startup_info(proc, kargs.info);
            }
            
            uint64_t user_stack_virt = 0x0000700000000000;
            uint64_t stack_pages = 8;

            for (uint64_t i = 0; i < stack_pages; i++) {
                uint64_t phys_page = pmm_alloc_block();
                if (phys_page == 0) {
                    kill_thread(current_thread, -1);
                    args->ret = SYS_RES_KERNEL_ERR;
                    break;
                }
                hal_map_page_in_space((uint64_t)proc->page_directory, user_stack_virt - (i * 4096), phys_page, 0x7);
                void* ptr = temp_map(phys_page);
                kernel_memset(ptr, 0, 4096);
                temp_unmap(ptr);
            }
            uint64_t user_rsp = user_stack_virt + 4096;
            user_rsp = (user_rsp & ~0xFULL) - 8;
            
            hal_set_exec_context(args->arch_context, elf.entry_point, user_rsp, info_addr, kargs.arg_val);
            
            args->ret = SYS_RES_OK;
            hal_set_current_address_space((uint64_t)proc->page_directory);
            break;
        }
		
		case SYS_SLEEP: {
			sleep(args->arg1);
			args->ret = SYS_RES_OK;
			break;
		}

        default: {
            kprint("Unknown Syscall invoked!\n");
            args->ret = SYS_RES_INVALID;
            break;
        }
    }
}