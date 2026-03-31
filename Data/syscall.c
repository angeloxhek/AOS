#include "include/kernel_internal.h"

#define MSR_EFER    0xC0000080
#define MSR_STAR    0xC0000081
#define MSR_LSTAR   0xC0000082
#define MSR_SFMASK  0xC0000084

#define KERNEL_CODE_SEG 0x08
#define USER_DATA_SEG   0x18
#define USER_SEG_BASE   0x10

extern void syscall_entry(void);

void init_syscall() {
	kernel_tcb.kernel_rsp = (uint64_t)(kernel_stack + sizeof(kernel_stack));
    uint64_t efer = rdmsr(MSR_EFER);
    wrmsr(MSR_EFER, efer | 1 | (1 << 11)); // SCE + NXE
    uint64_t star = ((uint64_t)USER_SEG_BASE << 48) | ((uint64_t)KERNEL_CODE_SEG << 32);
    wrmsr(MSR_STAR, star);
    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);
    wrmsr(MSR_SFMASK, 0x200);
}

int is_valid_user_pointer(const void* ptr) {
    uint64_t addr = (uint64_t)ptr;
    if (addr >= 0x800000000000) return 0; // Kernel
    if (ptr == 0) return 0; // NULL
    return 1;
}

int copy_string_from_user(const char* user_src, char* kernel_dest, int max_len) {
    if (!is_valid_user_pointer(user_src)) return 0;
    for (int i = 0; i < max_len; i++) {
        kernel_dest[i] = user_src[i];
        if (user_src[i] == 0) return 1;
    }
    kernel_dest[max_len - 1] = 0;
    return 1;
}

void syscall_handler(syscall_regs_t* regs) {
    uint64_t syscall_nr = regs->rax;
    switch (syscall_nr) {
        case SYS_PRINT: {
            const char* user_msg = (const char*)regs->rdi;
			if (!is_valid_user_pointer(user_msg)) {
				regs->rax = SYS_RES_INVALID;
				break;
			}
			kprint(user_msg);
			regs->rax = SYS_RES_OK;
            break;
        }
        case SYS_EXIT: {
			int exit_code = (int)regs->rdi;
			uint64_t tid = (uint64_t)regs->rsi;
			thread_t* th;
			if (tid == 0) { th = current_thread; }
			else {
				th = get_thread_by_id(tid);
				if (th == 0) {
					regs->rax = SYS_RES_INVALID;
					break;
				}
			}
			regs->rax = kill_thread(th, exit_code);
			if (tid == 0) schedule();
			break;
		}
		case SYS_IPC_SEND: {
            uint64_t dest = regs->rdi;
            message_t* user_msg = (message_t*)regs->rsi;
			if (!is_valid_user_pointer(user_msg)) {
				regs->rax = SYS_RES_INVALID;
				break;
			}
            regs->rax = ipc_send(dest, user_msg);
            break;
        }
        case SYS_IPC_RECV: {
            message_t* user_msg_buffer = (message_t*)regs->rdi;
			if (!is_valid_user_pointer(user_msg_buffer)) {
				regs->rax = SYS_RES_INVALID;
				break;
			}
            regs->rax = ipc_receive(user_msg_buffer);
            break;
        }
		case SYS_REGISTER_DRIVER:
            regs->rax = register_driver((driver_type_t)regs->rdi, (const char*)regs->rsi);
            break;

        case SYS_GET_DRIVER_TID:
            regs->rax = get_driver_tid((driver_type_t)regs->rdi);
            break;

        case SYS_GET_DRIVER_TID_BY_NAME: {
			const char* name_ptr = (const char*)regs->rdi;
			if (!is_valid_user_pointer(name_ptr)) {
				regs->rax = SYS_RES_INVALID;
				break;
			}
            regs->rax = get_driver_tid_by_name(name_ptr);
            break;
		}

		case SYS_GET_SYSTEM_INFO: {
			system_info_t* info = (system_info_t*)regs->rdi;
			if (is_valid_user_pointer(info)) {
				info->flags = state.system_flags;
				info->cpu_flags = state.cpu_flags;
				info->uptime = ticks;
				info->fs_base = current_thread->fs_base;
				info->gs_base = rdmsr(0xC0000102);
				info->kernel_gs_base = rdmsr(0xC0000101);
				regs->rax = SYS_RES_OK;
			} else {
				regs->rax = SYS_RES_INVALID;
			}
			break;
		}

		case SYS_SBRK: {
			asm volatile("cli");
			int64_t increment = (int64_t)regs->rdi;
			process_t* proc = current_thread->owner;
			uint64_t old_brk = proc->heap_limit;
			if (increment == 0) {
				regs->rax = old_brk;
				asm volatile("sti");
				break;
			}
			uint64_t new_brk = old_brk + increment;
			uint64_t old_page_limit = (old_brk + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
			uint64_t new_page_limit = (new_brk + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
			if (new_page_limit > old_page_limit) {
				uint64_t pages_needed = (new_page_limit - old_page_limit) / PAGE_SIZE;
				for (uint64_t i = 0; i < pages_needed; i++) {
					uint64_t phys_addr = pmm_alloc_block();
					if (!phys_addr) {
						regs->rax = SYS_RES_KERNEL_ERR;
						asm volatile("sti");
						return;
					}
					map_to_other_pml4(proc->page_directory, phys_addr, old_page_limit + (i * PAGE_SIZE), PAGE_USER | PAGE_WRITE | PAGE_PRESENT | PAGE_NX);
					kernel_memset((void*)P2V(phys_addr), 0, PAGE_SIZE);
				}
			}
			proc->heap_limit = new_brk;
			regs->rax = old_brk;
			asm volatile("sti");
			break;
		}

		case SYS_BLOCK_READ: {
			uint64_t dev_id = regs->rdi;
			uint64_t lba    = regs->rsi;
			uint64_t count  = regs->rdx;
			void* buffer    = (void*)regs->r10;

			if (!is_valid_user_pointer(buffer)) {
				regs->rax = SYS_RES_INVALID;
				break;
			}
			ide_device_t* target_dev = 0;
			for (int i = 0; i < ide_count; i++) {
				if (mounted_ides[i].id == dev_id) {
					target_dev = &mounted_ides[i];
					break;
				}
			}
			if (!target_dev) {
				regs->rax = SYS_RES_INVALID;
				break;
			}
			regs->rax = ide_read_sectors(target_dev, lba, count, buffer) ? SYS_RES_OK : SYS_RES_DSK_ERR;
			break;
		}

		case SYS_BLOCK_WRITE: {
            uint64_t dev_id = regs->rdi;
            uint64_t lba    = regs->rsi;
            uint64_t count  = regs->rdx;
            const void* buffer = (const void*)regs->r10;

			if (!is_valid_user_pointer(buffer)) {
				regs->rax = SYS_RES_INVALID;
				break;
			}
            ide_device_t* target_dev = 0;
            for (int i = 0; i < ide_count; i++) {
                if (mounted_ides[i].id == dev_id) {
                    target_dev = &mounted_ides[i];
                    break;
                }
            }
            if (!target_dev) {
                regs->rax = SYS_RES_INVALID;
                break;
            }
            regs->rax = ide_write_sectors(target_dev, lba, count, buffer) ? SYS_RES_OK : SYS_RES_DSK_ERR;
            break;
        }

		case SYS_GET_DISK_COUNT: {
			regs->rax = ide_count;
			break;
		}

		case SYS_GET_DISK_INFO: {
			uint64_t idx = regs->rdi;
			disk_info_t* user_info = (disk_info_t*)regs->rsi;
			if (!is_valid_user_pointer(user_info)) {
				regs->rax = SYS_RES_INVALID;
				break;
			}
			if (idx >= ide_count) {
				regs->rax = SYS_RES_INVALID;
				break;
			}
			ide_device_t* dev = &mounted_ides[idx];
			user_info->id = idx;
			user_info->sector_size = 512;
			user_info->type = DISK_TYPE_IDE;
			user_info->total_sectors = 0;
			kernel_strcpy(user_info->model, "Generic IDE Drive");
			regs->rax = SYS_RES_OK;
			break;
		}

		case SYS_GET_PARTITION_COUNT: {
			regs->rax = volume_count;
			break;
		}

		case SYS_GET_PARTITION_INFO: {
			uint64_t idx = regs->rdi;
			partition_info_t* user_info = (partition_info_t*)regs->rsi;
			if (!is_valid_user_pointer(user_info)) {
				regs->rax = SYS_RES_INVALID;
				break;
			}
			if (idx >= volume_count) {
				regs->rax = SYS_RES_INVALID;
				break;
			}
			volume_t* vol = &mounted_volumes[idx];
			user_info->id = vol->id;
			user_info->parent_disk_id = vol->device.id;
			user_info->start_lba = vol->partition_lba;
			user_info->size_sectors = vol->sector_count;
			user_info->bootable = vol->active;
			user_info->partition_type = 0x0B;
			regs->rax = SYS_RES_OK;
			break;
		}

		case SYS_GET_PROC_INFO: {
            uint32_t target_pid = (uint32_t)regs->rdi;
            proc_info_user_t* user_ptr = (proc_info_user_t*)regs->rsi;

            if (!is_valid_user_pointer(user_ptr)) {
                regs->rax = SYS_RES_INVALID;
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
                regs->rax = SYS_RES_NOTFOUND;
                break;
            }

            proc_info_user_t info;
            kernel_memset(&info, 0, sizeof(proc_info_user_t));
            info.pid = target_proc->id;
            kernel_memcpy(info.name, target_proc->name, 32);
            info.state = target_proc->state;
            info.heap_limit = target_proc->heap_limit;

            info.threads_count = 0;
            t = ready_queue;
            if (t) {
                do {
                    if (t->owner->id == target_pid) info.threads_count++;
                    t = t->next;
                } while (t != ready_queue);
            }

            kernel_memcpy(user_ptr, &info, sizeof(proc_info_user_t));
            regs->rax = SYS_RES_OK;
            break;
        }

        case SYS_GET_THREAD_INFO: {
            uint64_t target_tid = regs->rdi;
            thread_info_user_t* user_ptr = (thread_info_user_t*)regs->rsi;

            if (!is_valid_user_pointer(user_ptr)) {
                regs->rax = SYS_RES_INVALID;
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
                regs->rax = SYS_RES_NOTFOUND;
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
            regs->rax = SYS_RES_OK;
            break;
        }

		case SYS_SHM_ALLOC: {
			uint64_t size = regs->rdi;
			uint64_t* user_out_vaddr = (uint64_t*)regs->rsi;

			uint64_t out_vaddr = 0;
			uint64_t shm_id = shm_alloc(size, &out_vaddr);

			if (shm_id != 0 && user_out_vaddr != 0 && is_valid_user_pointer(user_out_vaddr)) {
				*user_out_vaddr = out_vaddr;
			}

			regs->rax = shm_id;
			break;
		}

		case SYS_SHM_ALLOW: {
			uint64_t shm_id = regs->rdi;
			uint64_t target_tid = regs->rsi;

			int result = shm_allow(shm_id, target_tid);
			regs->rax = (uint64_t)result;
			break;
		}

		case SYS_SHM_MAP: {
			uint64_t shm_id = regs->rdi;

			uint64_t vaddr = shm_map(shm_id);
			regs->rax = vaddr;
			break;
		}

		case SYS_SHM_FREE: {
			uint64_t shm_id = regs->rdi;

			int result = shm_free(shm_id);
			regs->rax = (uint64_t)result;
			break;
		}

        default: {
            kprint("Unknown Syscall invoked!\n");
            regs->rax = SYS_RES_INVALID;
            break;
        }
    }
}
