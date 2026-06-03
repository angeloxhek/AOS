#ifndef KERNEL_INTERNAL_H
#define KERNEL_INTERNAL_H

#include "aosldr.h"
#include "aoslib.h"
#include "hal.h"

extern uint64_t* bitmap;
extern uint64_t max_blocks;
extern uint64_t used_blocks;
extern uint64_t bitmap_size;

extern spinlock_t pmm_lock;
extern spinlock_t kprint_lock;
extern spinlock_t heap_lock;

extern volatile uint64_t ticks;
extern uint64_t boot_time;

extern thread_t* current_thread;
extern thread_t* ready_queue;
extern uint64_t thread_count;
extern process_t kernel_process;
extern uint8_t kernel_stack[KERNEL_STACK_SIZE];
extern thread_t* zombies_list;
extern thread_t* idle_thread_ptr;

extern driver_info_t* drivers_list_head;
extern uint64_t keyboard_driver_tid;

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

#endif /* KERNEL_INTERNAL_H */