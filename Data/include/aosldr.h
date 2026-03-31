#ifndef AOSKERNEL
#define AOSKERNEL 0x01

#include <stdint.h>
#include "bootparams.h"
#include "elf-min.h"
#include "aoslib.h"

#define MAX_VOLUMES 32
#define KBD_BUFFER_SIZE 256
#define KERNEL_BASE 0xFFFFFFFF80000000

#define ATA_CMD_READ_PIO_EXT    0x24
#define ATA_CMD_WRITE_PIO_EXT   0x34
#define ATA_CMD_CACHE_FLUSH_EXT 0xEA

// --- Структуры данных ---

struct fat32_bpb {
    uint8_t  boot_jmp[3];
    uint8_t  oem_name[8];
    uint16_t bytes_per_sec;
    uint8_t  sec_per_clus;
    uint16_t reserved_sec_cnt;
    uint8_t  num_fats;
    uint16_t root_ent_cnt;
    uint16_t tot_sec_16;
    uint8_t  media;
    uint16_t fat_sz_16;
    uint16_t sec_per_trk;
    uint16_t num_heads;
    uint32_t hidd_sec;
    uint32_t tot_sec_32;
    uint32_t fat_sz_32;
    uint16_t ext_flags;
    uint16_t fs_ver;
    uint32_t root_clus;
} __attribute__((packed));

struct fat32_dir_entry {
    char     name[11];
    uint8_t  attr;
    uint8_t  nt_res;
    uint8_t  crt_time_ten;
    uint16_t crt_time;
    uint16_t crt_date;
    uint16_t lst_acc_date;
    uint16_t cluster_high;
    uint16_t wrt_time;
    uint16_t wrt_date;
    uint16_t cluster_low;
    uint32_t file_size;
} __attribute__((packed));

struct fat32_lfn_entry {
    uint8_t  order;
    uint16_t name1[5];
    uint8_t  attr;
    uint8_t  type;
    uint8_t  checksum;
    uint16_t name2[6];
    uint16_t zero;
    uint16_t name3[2];
} __attribute__((packed));

typedef struct {
	uint64_t    id;           // ID устройства
    uint16_t    io_base;      // Порт ввода-вывода
    uint8_t     drive_select; // Выбор диска
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

typedef struct {
    char name[256];
    uint64_t cluster;
    uint64_t size;
    uint8_t  attr;
    uint16_t write_date;
    uint16_t write_time;
} fat32_dirent_t;

typedef struct {
    uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp, r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags, rsp, ss; // Процессор кладет это автоматически
} __attribute__((packed)) registers_t;

typedef struct {
    // Последний push в ASM -> Первое поле здесь (Low Address)
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rdx;
    uint64_t rbp;
    uint64_t rbx;
    uint64_t rax;      // Номер сисколла
    uint64_t rcx;      // RIP
    uint64_t r11;      // RFLAGS
    uint64_t rsp;      // User RSP
    // Первый push в ASM -> Последнее поле (High Address)
} syscall_regs_t;

struct idt_entry {
    uint16_t base_low;      // Смещение 0-15
    uint16_t sel;           // Селектор сегмента кода
    uint8_t  ist;           // Interrupt Stack Table offset (обычно 0)
    uint8_t  flags;         // Тип и атрибуты (P, DPL, Type)
    uint16_t base_mid;      // Смещение 16-31
    uint32_t base_high;     // Смещение 32-63
    uint32_t reserved;      // Зарезервировано (должно быть 0)
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint64_t base;          // Адрес таблицы теперь 64-битный
} __attribute__((packed));

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

typedef struct symbol {
    char name[16];
    uint32_t address;
    struct symbol* next;
} symbol_t;

typedef struct st_flags {
	uint32_t system_flags; // CAN_REGISTER_KERNEL_DRIVERS, CAN_PRINT, KERNEL_PANIC
	uint16_t cpu_flags; // FSGSBASE
} st_flags_t;

#define E820_RAM      1
#define E820_RESERVED 2
#define E820_ACPI     3

typedef struct {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t unused;
} __attribute__((packed)) e820_entry_t;

typedef struct {
    uint64_t user_rsp_scratch; // Смещение 0x00 (Сюда сохраняем User RSP)
    uint64_t kernel_rsp;       // Смещение 0x08 (Отсюда берем Kernel RSP)
    uint64_t reserved[3];
    uint64_t canary;
} __attribute__((packed)) kernel_tcb_t;

struct tss_entry_t {
    uint32_t reserved1;
    uint64_t rsp0;       // Стек ядра для прерываний
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved2;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved3;
    uint16_t reserved4;
    uint16_t iomap_base;
} __attribute__((packed));

typedef enum {
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_ZOMBIE
} thread_state_t;

//typedef struct mutex_t mutex_t;

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
	void*            owned_mutexes; // Reserved
	msg_node_t*      msg_queue_head;
    msg_node_t*      msg_queue_tail;
	process_t*       owner;
    int              waiting_for_msg;
	thread_state_t   state;
	int exit_code;
	uint8_t fpu_state[512] __attribute__((aligned(16)));
} thread_t;

/*typedef struct mutex_t {
    int locked;
    thread_t* owner;
    thread_t* wait_queue;
	mutex_t* next_owned;
} mutex_t;*/

typedef volatile int spinlock_t;

#define DRIVER_NAME_MAX 32

typedef struct driver_info_t {
	thread_t* thread;
	uint64_t tid;
	driver_type_t type;
	char name[DRIVER_NAME_MAX];
	struct driver_info_t* next;
} driver_info_t;

typedef struct shm_allow_node {
    uint64_t tid;
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
	

// --------------------------
//           ASM
// --------------------------

void outb(uint16_t port, uint8_t val);
uint8_t inb(uint16_t port);
uint16_t inw(uint16_t port);
void outw(uint16_t port, uint16_t val);
void insw(uint16_t port, void* addr, uint32_t count);
void outsw(uint16_t port, const void* addr, uint32_t count);
void wrmsr(uint32_t msr, uint64_t value);
uint64_t rdmsr(uint32_t msr);


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
void _kprint_error_vga(const char* str);


// ------------------------
//      uint to text
// ------------------------

void uint32_to_hex(uint32_t value, char* out_buffer);
void uint64_to_hex(uint64_t value, char* out_buffer);
void uint32_to_dec(uint32_t value, char* out_buffer);
void uint64_to_dec(uint64_t value, char* out_buffer);


// --------------------------
//        Data Utils
// --------------------------

void* kernel_memset64(void* ptr, uint64_t value, uint64_t num);
void* kernel_memset(void* ptr, uint8_t value, uint64_t num);
void kernel_memcpy(void* dest, const void* src, uint64_t n);
void kernel_to_upper(char* s);
int kernel_strcmp(const char* s1, const char* s2);
char *kernel_strcpy(char *dest, const char *src);


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
//           VMM
// -------------------------

uint64_t* vmm_get_pte(uint64_t virt, int alloc);
uint64_t vmm_get_phys_from_pml4(uint64_t* pml4_phys_root, uint64_t virt);
void vmm_map_page(uint64_t phys, uint64_t virt, uint64_t flags);
void vmm_unmap_page(uint64_t virt);


// -------------------------
//        PIC & IDT
// -------------------------

void pic_remap();
void idt_set_gate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags);
void idt_install();
void timer_init(uint32_t frequency);
void schedule();
void isr_handler(registers_t *r);


// -------------------------
//           GDT
// -------------------------

void gdt_set_gate(int num, uint64_t base, uint64_t limit, uint8_t access, uint8_t gran);
void write_tss(int32_t num, uint64_t base, uint32_t limit);
void gdt_install();
void init_syscall();
int is_valid_user_pointer(const void* ptr);
void syscall_handler(syscall_regs_t* regs);
void fpu_init();


// -------------------------
//           IDE
// -------------------------

void get_ide_device_name(ide_device_t* device, char* buff);
void get_volume_name(volume_t* v, char* buff);
int ide_identify(ide_device_t* dev);
int ide_wait_ready(void* dev);
int ide_wait_drq(void* dev);
int ide_read_sectors(ide_device_t* dev, uint64_t lba, uint16_t count, uint8_t* buffer);
int ide_read_sector(ide_device_t* dev, uint64_t lba, uint8_t* buffer);
int ide_write_sectors(ide_device_t* dev, uint64_t lba, uint16_t count, const uint8_t* buffer);
int ide_write_sector(ide_device_t* dev, uint64_t lba, const uint8_t* buffer);


// ----------------------------
//         File System
// ----------------------------

void mbr_mount_all_partitions(ide_device_t* dev, uint8_t is_boot_device);
void mbr_storage_init(uint8_t boot_drive_id);
uint64_t cluster_to_lba(volume_t* vol, uint32_t cluster);
uint32_t get_next_cluster(volume_t* vol, uint32_t current_cluster);

// -------------------------
//           Heap
// -------------------------

int expand_heap(uint64_t size);
void* kernel_malloc(uint64_t size);
void kernel_free(void* ptr);
void* kernel_realloc(void* ptr, uint64_t new_size);
void* kernel_malloc_aligned(uint64_t size, uint64_t alignment);


// -------------------------
//          FAT32
// -------------------------

void fat32_entry_to_dirent(struct fat32_dir_entry* raw, fat32_dirent_t* out);
void fat32_collect_lfn_chars(struct fat32_lfn_entry* lfn, char* lfn_buffer);
void fat32_format_sfn(char* dest, const char* sfn_name);
unsigned char fat32_checksum(unsigned char *pName);
fat32_dirent_t* fat32_read_dir(volume_t* v, fat32_dirent_t* dir_entry, int* out_count);
int fat32_find_in_dir(volume_t* v, fat32_dirent_t* dir_entry, const char* search_name, fat32_dirent_t* result);
void* fat32_read_file(volume_t* v, fat32_dirent_t* file, uint64_t* out_size);


// -------------------------
//         Process
// -------------------------

uint64_t get_current_pml4();
void set_current_pml4(uint64_t phys_addr);
void* temp_map(uint64_t phys_addr);
void temp_unmap();
process_t* create_process(const char* name);
uint64_t get_or_alloc_table(uint64_t parent_phys, int index, int flags);
void map_to_other_pml4(uint64_t* pml4_phys, uint64_t phys, uint64_t virt, int flags);
void process_map_memory(process_t* proc, uint64_t virt, uint64_t size);
int copy_string_from_user(const char* user_src, char* kernel_dest, int max_len);


// -------------------------
//        Scheduler
// -------------------------

void init_scheduler();
thread_t* create_thread_core(uint64_t cr3, process_t* owner);
void create_user_thread(uint64_t entry_point, uint64_t user_stack, uint64_t cr3_phys, process_t* proc);
void create_kernel_thread(void (*entry)(void));
int kill_thread(thread_t* target, int exit_code);
thread_t* get_thread_by_id(uint64_t tid);
process_t* get_process_by_id(uint32_t pid);
int64_t register_driver(driver_type_t type, const char* user_name);
uint64_t get_driver_tid(driver_type_t type);
uint64_t get_driver_tid_by_name(const char* name);
int get_driver_tid_sleep_wrapper(void* arg);


// -------------------------
//           ELF
// -------------------------

void load_elf_raw_fat32(volume_t* v, fat32_dirent_t* file, elf_load_result_t* result);
void start_elf_process(elf_load_result_t* res);


/*
// -------------------------
//          Mutex
// -------------------------

void mutex_init(mutex_t* m);
void mutex_lock(mutex_t* m);
void mutex_unlock(mutex_t* m);
*/


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


// -------------------------
//           IPC
// -------------------------

int64_t ipc_send(uint64_t dest_tid, message_t* msg);
int64_t ipc_receive(message_t* out_msg);

// -------------------------
//       Shared Memory
// -------------------------

void shm_init();
shm_object_t* shm_find_by_id(uint64_t id);
void shm_add(shm_object_t* obj);
void shm_remove(shm_object_t* obj);
uint64_t shm_alloc(uint64_t size_bytes, uint64_t* out_vaddr);
int shm_allow(uint64_t shm_id, uint64_t target_tid);
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