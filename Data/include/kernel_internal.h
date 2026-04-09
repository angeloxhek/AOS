#ifndef KERNEL_INTERNAL_H
#define KERNEL_INTERNAL_H

#include "aosldr.h"
#include "aoslib.h"

/* Page table constants shared across all kernel modules */
#define P2V(phys)      ((uint64_t)(phys) + KERNEL_BASE)
#define V2P(virt)      ((uint64_t)(virt) - KERNEL_BASE)
#define PAGE_SIZE      4096
#define PAGE_PRESENT   0x1
#define PAGE_WRITE     0x2
#define PAGE_USER      0x4
#define PAGE_FRAME     0x000FFFFFFFFFF000
#define PAGE_MASK      0xFFFFFFFFFFFFF000
#define PML4_INDEX(x)  (((x) >> 39) & 0x1FF)
#define PDP_INDEX(x)   (((x) >> 30) & 0x1FF)
#define PD_INDEX(x)    (((x) >> 21) & 0x1FF)
#define PT_INDEX(x)    (((x) >> 12) & 0x1FF)
#define PHYS_PML4       0x80000

#define BLOCK_SIZE 4096
#define TEMP_PAGE_VIRT 0xFFFFFFFFFFE00000
#define TEMP_PAGES_COUNT 256
#define KERNEL_STACK_SIZE 16384

/* Global variables declared in aosldr.c, used across modules */
extern uint64_t* bitmap;
extern uint64_t max_blocks;
extern uint64_t used_blocks;
extern uint64_t bitmap_size;
extern uint64_t* pml4_table_virt;

extern spinlock_t pmm_lock;
extern spinlock_t kprint_lock;
extern spinlock_t heap_lock;

extern volatile uint64_t ticks;
extern uint64_t boot_time;
#define TIMER_FREQ 1000

extern thread_t* current_thread;
extern thread_t* ready_queue;
extern uint64_t thread_count;

extern process_t kernel_process;
extern kernel_tcb_t kernel_tcb;
extern uint8_t kernel_stack[16384];
extern uint8_t default_fpu_state[512];

extern ide_device_t* system_ide;
extern ide_device_t mounted_ides[MAX_VOLUMES];
extern int ide_count;
extern volume_t* system_volume;
extern volume_t mounted_volumes[MAX_VOLUMES];
extern int volume_count;

extern driver_info_t* drivers_list_head;
extern uint64_t keyboard_driver_tid;
extern thread_t* zombies_list;

extern shm_object_t* shm_global_list;
extern uint64_t next_shm_id;

extern const uint8_t (*font)[256][16];
extern int cursor_x;
extern int cursor_y;
extern uint32_t bg_color;
extern boot_video_t* video;
extern st_flags_t state;

#define KERNEL_MESSAGES_COUNT 9
extern const char* const kernel_messages[KERNEL_MESSAGES_COUNT];

extern struct tss_entry_t tss;

#endif /* KERNEL_INTERNAL_H */
