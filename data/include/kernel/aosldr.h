#ifndef AOSKERNEL
#define AOSKERNEL 0x01

#include <stdint.h>
#include "bootparams.h"
#include "elf-min.h"
#include "aoslib.h"
#include "hal.h"

#define KBD_BUFFER_SIZE 256

typedef struct {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char padding[12];
} __attribute__((packed)) tar_header_t;

typedef struct {
	uint64_t    id;
    uint16_t    io_base;
    uint8_t     drive_select;
} ide_device_t;

typedef struct {
	uint64_t id;
    uint64_t partition_lba;
    uint64_t fat_lba;
    uint64_t data_lba;
	uint64_t sector_count;
    ide_device_t device; 
    uint32_t root_cluster;
    uint32_t sec_per_clus;
    uint8_t  active;
} volume_t;

typedef struct st_flags {
	uint32_t system_flags; // CAN_REGISTER_KERNEL_DRIVERS, CAN_PRINT, KERNEL_PANIC
	uint16_t cpu_flags; // FSGSBASE
} st_flags_t;

typedef enum {
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_ZOMBIE
} thread_state_t;

typedef struct msg_node_t {
    message_t msg;
    struct msg_node_t* next;
} msg_node_t;

typedef struct process_t {
	char              name[32];
    uint32_t          id;
	uint32_t          sleep_ticks;
    uint64_t*         page_directory;
    uint64_t          entry_point;
    uint64_t          rsp;
    uint64_t          rbp;
	uint64_t          tls_image_vaddr;
    uint64_t          tls_file_size;
    uint64_t          tls_mem_size;
    uint64_t          tls_align;
	uint64_t          heap_limit;
	uint64_t          next_shm_vaddr;
    struct process_t* next;
	uint8_t           state;
} process_t;

typedef struct thread_t {
	uint64_t         tid;
	uint64_t         rsp;
    uint64_t         stack_base;
    uint64_t         cr3;
	uint64_t         fs_base;
	uint64_t         wake_up_time;
	struct thread_t* next;
	struct thread_t* next_zombie;
	struct thread_t* next_waiter;
	auth_id_t        user;
	msg_node_t*      msg_queue_head;
    msg_node_t*      msg_queue_tail;
	process_t*       owner;
    int              waiting_for_msg;
	thread_state_t   state;
	int exit_code;
	uint8_t fpu_state[512] __attribute__((aligned(16)));
} thread_t;

typedef volatile int spinlock_t;

#define ALLOWED_PORTS_MAX 8

typedef struct driver_info_t {
	process_t* process;
	uint32_t pid;
	driver_type_t type;
	char name[DRIVER_NAME_MAX];
	uint32_t driver_perms;
    uint16_t allowed_ports[ALLOWED_PORTS_MAX];
	struct driver_info_t* next;
} driver_info_t;

typedef struct shm_allow_node {
    uint64_t pid;
    struct shm_allow_node* next;
} shm_allow_node_t;

typedef struct shm_map_node {
    uint64_t pid;
    uint64_t vaddr;
    struct shm_map_node* next;
} shm_map_node_t;

typedef struct shm_object {
    uint64_t id;
    uint64_t owner_pid;
    uint64_t owner_vaddr;
    uint64_t* phys_pages;
    uint64_t  page_count;
    shm_allow_node_t* allow_list;
    shm_map_node_t*   map_list;
    struct shm_object* next;
} shm_object_t;
	

// -------------------------
//     Print Functions
// -------------------------

void put_pixel(uint32_t x, uint32_t y, uint32_t color);
void _kclear();
void kclear();
void kprint_scroll();
void _kprint_char(int x_pos, int y_pos, char c, uint32_t fg, uint32_t bg);
void kprint_char(char c, uint32_t color);
void kprint(const char* str);
void kprint_error(const char* str);
void _kprint(const char* str);
void _kprint_error(const char* str);


// ------------------------
//      uint to text
// ------------------------

void uint32_to_hex(uint32_t value, char* out_buffer);
void uint64_to_hex(uint64_t value, char* out_buffer);
void uint32_to_dec(uint32_t value, char* out_buffer);
void uint64_to_dec(uint64_t value, char* out_buffer);
uint64_t octal_to_int(const char* str);


// --------------------------
//        Data Utils
// --------------------------

void* kernel_memset64(void* ptr, uint64_t value, uint64_t num);
void* kernel_memset(void* ptr, uint8_t value, uint64_t num);
void kernel_memcpy(void* dest, const void* src, uint64_t n);
void kernel_to_upper(char* s);
int kernel_strcmp(const char* s1, const char* s2);
char *kernel_strcpy(char *dest, const char *src);
uint64_t kernel_strnlen(const char* s, uint64_t maxlen);
#define kernel_strlen(s) kernel_strnlen(s, UINT64_MAX)


// --------------------------
//            PMM
// --------------------------

void mmap_set(uint64_t bit);
void mmap_unset(uint64_t bit);
int mmap_test(uint64_t bit);
int64_t mmap_first_free();
void init_pmm(uint64_t mem_size, uint64_t bitmap_base);
uint64_t pmm_alloc_block();
void pmm_free_block(uint64_t p_addr);
void pmm_init_region(uint64_t base, uint64_t size);
void pmm_deinit_region(uint64_t base, uint64_t size);


// -------------------------
//           GDT
// -------------------------

int is_valid_user_pointer(const void* ptr);
int copy_string_from_user(const char* user_src, char* kernel_dest, int max_len);
void generic_syscall_handler(syscall_args_t* regs);


// -------------------------
//           Heap
// -------------------------

int expand_heap(uint64_t size);
void* kernel_malloc(uint64_t size);
void kernel_free(void* ptr);
void* kernel_realloc(void* ptr, uint64_t new_size);
void* kernel_malloc_aligned(uint64_t size, uint64_t alignment);


// -------------------------
//         Process
// -------------------------

void* temp_map(uint64_t phys_addr);
void temp_unmap(void* virt_ptr);
process_t* create_process(const char* name);
void process_map_memory(process_t* proc, uint64_t virt, uint64_t size);
int copy_string_from_user(const char* user_src, char* kernel_dest, int max_len);


// -------------------------
//        Scheduler
// -------------------------

void init_scheduler();
thread_t* create_thread_core(uint64_t cr3, process_t* owner);
thread_t* create_user_thread(uint64_t entry_point, uint64_t user_stack, uint64_t cr3_phys, process_t* proc, uint64_t arg1, uint64_t arg2);
thread_t* create_kernel_thread(void (*entry)(void));
int kill_thread(thread_t* target, int exit_code);
thread_t* get_thread_by_id(uint64_t tid);
process_t* get_process_by_id(uint32_t pid);
void schedule(void);


// -------------------------
//      Driver Registry
// -------------------------

int64_t register_driver(driver_type_t type, const char* user_name, uint32_t perms, uint16_t* allowed_ports, process_t* process);
uint32_t get_driver_pid(driver_type_t type);
driver_info_t* get_driver_by_pid(uint32_t pid);
uint32_t get_driver_pid_by_name(const char* name);
int get_driver_pid_sleep_wrapper(void* arg);


// -------------------------
//           ELF
// -------------------------

void load_elf_raw_proc(process_t* proc, uint8_t* raw_data, uint64_t file_size, elf_load_result_t* result);
void load_elf_raw(char* name, uint8_t* raw_data, uint64_t file_size, elf_load_result_t* result);
int start_elf_process(elf_load_result_t* res, startup_info_t* info, uint64_t arg2);
int startup_info_args_copy(startup_info_t* k_info, startup_info_t* child_info, void* kvirt, uint64_t child_virt_addr);
startup_info_t* prepare_child_startup_info(process_t* proc, startup_info_t* user_info_ptr);
startup_info_t* prepare_child_startup_info_kernel(process_t* proc, startup_info_t* k_info);


// -------------------------
//          InitRD
// -------------------------

uint8_t* load_file_initrd(void* initrd, const char* name, uint64_t* out_size);


// -------------------------
//         Spinlock
// -------------------------

uint64_t spinlock_irq_save(void);
void spinlock_irq_restore(uint64_t flags);
void spinlock_acquire(spinlock_t* lock);
void spinlock_release(spinlock_t* lock);


// -------------------------
//          Timers
// -------------------------

void sleep(uint64_t ms);
int sleep_while_zero(int (*func)(void*), void* arg, uint64_t timeout_ms, int* out_result);
void get_time_info(time_info_t* out);


// -------------------------
//           IPC
// -------------------------

int64_t ipc_forward(uint64_t dest_tid, message_t* user_msg);
int64_t ipc_requeue(message_t* user_msg);
int64_t ipc_send(uint64_t dest_tid, message_t* msg);
int64_t ipc_try_receive(message_t* out_msg);
int64_t ipc_receive(message_t* out_msg);
int64_t ipc_receive_ex(uint64_t tid, msg_type_t type, msg_subtype_t subtype, message_t* out_msg);

// -------------------------
//       Shared Memory
// -------------------------

void shm_init();
shm_object_t* shm_find_by_id(uint64_t id);
void shm_add(shm_object_t* obj);
void shm_remove(shm_object_t* obj);
uint64_t shm_alloc(uint64_t size_bytes, uint64_t* out_vaddr);
int shm_allow(uint64_t shm_id, uint64_t target_pid);
uint64_t shm_map(uint64_t shm_id);
int shm_free(uint64_t shm_id);


// -------------------------
//          Debug
// -------------------------

__attribute__((noreturn)) void kernel_error(uint64_t code, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4);
__attribute__((noreturn)) void __stack_chk_fail(void);
__attribute__((noreturn)) void breakpoint();
void pausepoint();


// ------------------------
//         Other
// ------------------------

void reset_state();


// ------------------------
//      MAIN THREADS
// ------------------------
void kernel_main(boot_info_t* boot_info);
void idle_thread();

#endif