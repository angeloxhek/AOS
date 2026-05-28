#ifndef HAL_H
#define HAL_H

#include <stdint.h>
#include "bootparams.h"

struct thread_t;
struct process_t;

uint64_t hal_irq_save(void);
void hal_irq_restore(uint64_t flags);
void hal_cpu_relax(void);
uint64_t hal_get_random_seed(void);

void hal_cpu_init(void);
void hal_interrupts_init(void);
void hal_timer_init(uint32_t frequency);
uint64_t hal_get_boot_time(void);
void hal_enable_interrupts(void);
uint64_t hal_get_total_ram(boot_info_t* boot_info);

void hal_disable_interrupts(void);
__attribute__((noreturn)) void hal_halt(void);
void hal_debug_print_early(const char* str);
void hal_debug_pause(void);

void hal_outb(uint16_t port, uint8_t val);
uint8_t hal_inb(uint16_t port);
uint16_t hal_inw(uint16_t port);
void hal_outw(uint16_t port, uint16_t val);
void hal_insw(uint16_t port, void* addr, uint32_t count);
void hal_outsw(uint16_t port, const void* addr, uint32_t count);
void hal_wrmsr(uint32_t msr, uint64_t value);
uint64_t hal_rdmsr(uint32_t msr);

void hal_vm_init(void);
void hal_map_page(uint64_t virt, uint64_t phys, uint64_t flags);
void hal_unmap_page(uint64_t virt);
uint64_t hal_get_current_address_space(void);
void hal_set_current_address_space(uint64_t phys_addr);
uint64_t hal_get_phys(uint64_t space_root_phys, uint64_t virt);
uint64_t* hal_create_address_space(void);
void hal_copy_address_space(uint64_t* src_pml4_virt, uint64_t* dst_pml4_virt);
void hal_destroy_address_space(struct process_t* proc);
void hal_map_page_in_space(uint64_t space_root_phys, uint64_t virt, uint64_t phys, uint64_t flags);
void hal_flush_tlb(void);

void hal_storage_init(uint8_t boot_drive_id);
int hal_disk_read(uint32_t drive_id, uint64_t lba, uint16_t count, uint8_t* buffer);
int hal_disk_write(uint32_t drive_id, uint64_t lba, uint16_t count, const uint8_t* buffer);

void hal_init_current_thread(struct thread_t* t);
void hal_setup_kernel_thread(struct thread_t* t, uint64_t entry_point);
void hal_setup_user_thread(struct thread_t* t, uint64_t entry_point, uint64_t user_stack, uint64_t arg1, uint64_t arg2);
void hal_switch_task(struct thread_t* prev, struct thread_t* next);

typedef struct {
    uint64_t syscall_nr;
    uint64_t arg1;
    uint64_t arg2;
    uint64_t arg3;
    uint64_t arg4;
    uint64_t ret;
    void* arch_context;
} syscall_args_t;

void hal_syscall_init(void);
int hal_is_valid_user_pointer(const void* ptr);
uint64_t hal_get_user_tls_base(void);
uint64_t hal_get_kernel_tls_base(void);
void hal_prepare_fork_context(struct thread_t* parent, struct thread_t* child);
void hal_set_exec_context(void* arch_context, uint64_t entry_point, uint64_t user_stack, uint64_t arg1, uint64_t arg2);

void hal_set_io_permissions(uint32_t pid);

#endif