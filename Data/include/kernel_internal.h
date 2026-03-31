#ifndef KERNEL_INTERNAL_H
#define KERNEL_INTERNAL_H

#include "aosldr.h"
#include "aoslib.h"

// -------------------------
//     Shared Macros
// -------------------------

#define P2V(phys)      ((uint64_t)(phys) + KERNEL_BASE)
#define V2P(virt)      ((uint64_t)(virt) - KERNEL_BASE)
#define PAGE_SIZE      4096
#define PAGE_PRESENT   0x1
#define PAGE_WRITE     0x2
#define PAGE_USER      0x4
#define PAGE_NX        (1ULL << 63)
#define PAGE_FRAME     0x000FFFFFFFFFF000
#define PAGE_MASK      0xFFFFFFFFFFFFF000
#define PML4_INDEX(x)  (((x) >> 39) & 0x1FF)
#define PDP_INDEX(x)   (((x) >> 30) & 0x1FF)
#define PD_INDEX(x)    (((x) >> 21) & 0x1FF)
#define PT_INDEX(x)    (((x) >> 12) & 0x1FF)
#define PHYS_PML4       0x80000
#define BLOCK_SIZE     4096
#define TEMP_PAGE_VIRT 0xFFFFFFFFFFE00000
#define KERNEL_STACK_SIZE 16384

// -------------------------
//   Extern Global Variables
// -------------------------

extern const uint8_t (*font)[256][16];
extern int cursor_x;
extern int cursor_y;
extern uint32_t bg_color;
extern boot_video_t* video;
extern st_flags_t state;
#define KERNEL_MSG_COUNT 9
extern const char* const kernel_messages[KERNEL_MSG_COUNT];

extern uint64_t* bitmap;
extern uint64_t max_blocks;
extern uint64_t used_blocks;
extern uint64_t bitmap_size;

extern uint64_t* pml4_table_virt;

extern const unsigned char const kbd_us[128];

extern struct idt_entry idt[256];
extern struct idt_ptr   idtp;
extern struct gdt_entry gdt[7];
extern struct gdt_ptr   gp;
extern struct tss_entry_t tss;

extern ide_device_t* system_ide;
extern ide_device_t mounted_ides[MAX_VOLUMES];
extern int ide_count;
extern volume_t* system_volume;
extern volume_t mounted_volumes[MAX_VOLUMES];
extern int volume_count;

extern malloc_header_t* free_list_start;
extern uint64_t heap_current_limit;
extern int malloc_initialized;

extern uint8_t kernel_stack[16384];
extern kernel_tcb_t kernel_tcb;

extern process_t kernel_process;
extern thread_t* current_thread;
extern thread_t* ready_queue;
extern uint64_t thread_count;

extern spinlock_t kprint_lock;
extern spinlock_t heap_lock;
extern spinlock_t pmm_lock;
extern volatile uint64_t ticks;

extern driver_info_t* drivers_list_head;
extern uint64_t keyboard_driver_tid;

extern thread_t* zombies_list;

extern uint8_t default_fpu_state[512];

extern shm_object_t* shm_global_list;
extern uint64_t next_shm_id;

extern void switch_to_task(thread_t* current, thread_t* next);
extern void trampoline_enter_user();
extern void trampoline_enter_kernel();

// -------------------------
//   Function Prototypes
// -------------------------

// ASM I/O
void outb(uint16_t port, uint8_t val);
uint8_t inb(uint16_t port);
uint16_t inw(uint16_t port);
void outw(uint16_t port, uint16_t val);
void insw(uint16_t port, void* addr, uint32_t count);
void outsw(uint16_t port, const void* addr, uint32_t count);
void wrmsr(uint32_t msr, uint64_t value);
uint64_t rdmsr(uint32_t msr);

// Data Utils
void* kernel_memset64(void* ptr, uint64_t value, uint64_t n);
void* kernel_memset(void* ptr, uint8_t value, uint64_t num);
void kernel_memcpy(void* dest, const void* src, uint64_t n);
void kernel_to_upper(char* s);
int kernel_strcmp(const char* s1, const char* s2);
char *kernel_strcpy(char *dest, const char *src);

// Console
void put_pixel(uint32_t x, uint32_t y, uint32_t color);
void kprint_scroll();
void _kprint_char(int x_pos, int y_pos, char c, uint32_t fg, uint32_t bg);
void kprint_char(char c, uint32_t color);
void _kprint(const char* str);
void kprint(const char* str);
void _kprint_error(const char* str);
void kprint_error(const char* str);
void _kclear();
void kclear();
void _kprint_error_vga(const char* str);
void uint32_to_hex(uint32_t value, char* out_buffer);
void uint32_to_dec(uint32_t value, char* out_buffer);
void uint64_to_hex(uint64_t value, char* out_buffer);
void uint64_to_dec(uint64_t value, char* out_buffer);
__attribute__((noreturn)) void kernel_error(uint64_t code, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4);
__attribute__((noreturn)) void __stack_chk_fail(void);
__attribute__((noreturn)) void breakpoint();
void pausepoint();

// PMM
void mmap_set(uint64_t bit);
void mmap_unset(uint64_t bit);
int mmap_test(uint64_t bit);
int64_t mmap_first_free();
void init_pmm(uint64_t mem_size, uint64_t bitmap_base);
uint64_t pmm_alloc_block();
void pmm_free_block(uint64_t p_addr);
void pmm_init_region(uint64_t base, uint64_t size);
void pmm_deinit_region(uint64_t base, uint64_t size);

// VMM
uint64_t* vmm_get_pte(uint64_t virt, int alloc);
uint64_t vmm_get_phys_from_pml4(uint64_t* pml4_phys_root, uint64_t virt);
void vmm_map_page(uint64_t phys, uint64_t virt, uint64_t flags);
void vmm_unmap_page(uint64_t virt);
uint64_t get_current_pml4();
void set_current_pml4(uint64_t phys_addr);
void* temp_map(uint64_t phys_addr);
void temp_unmap();
uint64_t get_or_alloc_table(uint64_t parent_phys, int index, int flags);
void map_to_other_pml4(uint64_t* pml4_phys, uint64_t phys, uint64_t virt, int flags);
void process_map_memory(process_t* proc, uint64_t virt, uint64_t size);

// PIC, IDT, GDT, TSS
void pic_remap();
void idt_set_gate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags);
void idt_install();
void timer_init(uint32_t frequency);
void gdt_set_gate(int num, uint64_t base, uint64_t limit, uint8_t access, uint8_t gran);
void write_tss(int32_t num, uint64_t base, uint32_t limit);
void gdt_install();
void isr_handler(registers_t *r);
void fpu_init();

// Syscall
void init_syscall();
int is_valid_user_pointer(const void* ptr);
int copy_string_from_user(const char* user_src, char* kernel_dest, int max_len);
void syscall_handler(syscall_regs_t* regs);

// Scheduler
void init_scheduler();
thread_t* create_thread_core(uint64_t cr3, process_t* owner);
void create_user_thread(uint64_t entry_point, uint64_t user_stack, uint64_t cr3_phys, process_t* proc);
void create_kernel_thread(void (*entry)(void));
void schedule();
int kill_thread(thread_t* target, int exit_code);
thread_t* get_thread_by_id(uint64_t tid);
process_t* get_process_by_id(uint32_t pid);
void sleep(uint64_t ms);
int sleep_while_zero(int (*func)(void*), void* arg, uint64_t timeout_ms, int* out_result);
int get_driver_tid_sleep_wrapper(void* arg);

// Heap
int expand_heap(uint64_t size);
void* kernel_malloc(uint64_t size);
void kernel_free(void* ptr);
void* kernel_malloc_aligned(uint64_t size, uint64_t alignment);

// Process
process_t* create_process(const char* name);

// Drivers
int64_t register_driver(driver_type_t type, const char* user_name);
uint64_t get_driver_tid(driver_type_t type);
uint64_t get_driver_tid_by_name(const char* name);

// IPC
int64_t ipc_send(uint64_t dest_tid, message_t* user_msg);
int64_t ipc_receive(message_t* user_msg_out);

// IDE
int ide_wait_ready(void* dev);
int ide_wait_drq(void* dev);
int ide_identify(ide_device_t* dev);
int ide_read_sectors(ide_device_t* dev, uint64_t lba, uint16_t count, uint8_t* buffer);
int ide_read_sector(ide_device_t* dev, uint64_t lba, uint8_t* buffer);
int ide_write_sectors(ide_device_t* dev, uint64_t lba, uint16_t count, const uint8_t* buffer);
int ide_write_sector(ide_device_t* dev, uint64_t lba, const uint8_t* buffer);
void get_ide_device_name(ide_device_t* device, char* buff);
void get_volume_name(volume_t* v, char* buff);
void mbr_mount_all_partitions(ide_device_t* dev, uint8_t is_boot_device);
void mbr_storage_init(uint8_t boot_drive_id);
uint64_t cluster_to_lba(volume_t* vol, uint32_t cluster);
uint32_t get_next_cluster(volume_t* vol, uint32_t current_cluster);

// FAT32
void fat32_entry_to_dirent(struct fat32_dir_entry* raw, fat32_dirent_t* out);
void fat32_collect_lfn_chars(struct fat32_lfn_entry* lfn, char* lfn_buffer);
void fat32_format_sfn(char* dest, const char* sfn_name);
unsigned char fat32_checksum(unsigned char *pName);
fat32_dirent_t* fat32_read_dir(volume_t* v, fat32_dirent_t* dir_entry, int* out_count);
int fat32_find_in_dir(volume_t* v, fat32_dirent_t* dir_entry, const char* search_name, fat32_dirent_t* result);
void* fat32_read_file(volume_t* v, fat32_dirent_t* file, uint64_t* out_size);

// ELF
void load_elf_raw_fat32(volume_t* v, fat32_dirent_t* file, elf_load_result_t* result);
void start_elf_process(elf_load_result_t* res);

// Spinlock
uint64_t spinlock_irq_save(void);
void spinlock_irq_restore(uint64_t flags);
void spinlock_acquire(spinlock_t* lock);
void spinlock_release(spinlock_t* lock);

// Shared Memory
void shm_init();
shm_object_t* shm_find_by_id(uint64_t id);
void shm_add(shm_object_t* obj);
void shm_remove(shm_object_t* obj);
uint64_t shm_alloc(uint64_t size_bytes, uint64_t* out_vaddr);
int shm_allow(uint64_t shm_id, uint64_t target_tid);
uint64_t shm_map(uint64_t shm_id);
int shm_free(uint64_t shm_id);

// Other
void reset_state();
void kernel_main(boot_info_t* boot_info);
void idle_thread();

#endif /* KERNEL_INTERNAL_H */
