#include "include/kernel_internal.h"
#include "include/fonts.h"

extern uint32_t boot_ver;

const uint8_t (*font)[256][16];

int cursor_x = 0;
int cursor_y = 0;
uint32_t bg_color = 0x00000000;
boot_video_t* video;
st_flags_t state;

const char* const kernel_messages[] = {
	"NO_ERROR_DEBUG",
	"STACK_SMASHING_DETECTED",
	"VOLUME_ERROR",
	"IDE_ERROR",
	"FS_NOT_FOUND",
	"MEMORY_ERROR",
	"DRIVER_ERROR",
	"ISR_ERROR",
	"BOOT_INFO_INVALID"
};

uint64_t* bitmap = 0;
uint64_t max_blocks = 0;
uint64_t used_blocks = 0;
uint64_t bitmap_size = 0;

#define HEAP_START_ADDR 0xFFFF800040000000
malloc_header_t* free_list_start = (malloc_header_t*)HEAP_START_ADDR;
uint64_t heap_current_limit = HEAP_START_ADDR;
int malloc_initialized = 0;

__attribute__((aligned(16)))
uint8_t kernel_stack[16384];

ide_device_t* system_ide = 0;
ide_device_t mounted_ides[MAX_VOLUMES];
int ide_count = 0;
volume_t* system_volume = 0;
volume_t mounted_volumes[MAX_VOLUMES];
int volume_count = 0;

process_t kernel_process;
thread_t* current_thread;
thread_t* ready_queue;
uint64_t thread_count = 0;
thread_t* zombies_list = 0;

spinlock_t kprint_lock = 0;
spinlock_t heap_lock = 0;
spinlock_t pmm_lock = 0;
volatile uint64_t ticks = 0;
uint64_t boot_time = 0;

driver_info_t* drivers_list_head;
uint64_t keyboard_driver_tid = 0;
uint64_t vfs_driver_tid = 0;

shm_object_t* shm_global_list = 0;
uint64_t next_shm_id = 1;

const unsigned char kbd_us[128] =
{
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8',	/* 9 */
  '9', '0', '-', '=', '\b',	/* Backspace */
  '\t',			/* Tab */
  'q', 'w', 'e', 'r',	/* 19 */
  't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',	/* Enter key */
    0,			/* 29   - Control */
  'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',	/* 39 */
 '\'', '`',   0,		/* Left shift */
 '\\', 'z', 'x', 'c', 'v', 'b', 'n',			/* 49 */
  'm', ',', '.', '/',   0,				/* Right shift */
  '*',
    0,	/* Alt */
  ' ',	/* Space bar */
    0,	/* Caps lock */
    0,	/* 59 - F1 key ... > */
    0,   0,   0,   0,   0,   0,   0,   0,
    0,	/* < ... F10 */
    0,	/* 69 - Num lock*/
    0,	/* Scroll Lock */
    0,	/* Home key */
    0,	/* Up Arrow */
    0,	/* Page Up */
  '-',
    0,	/* Left Arrow */
    0,
    0,	/* Right Arrow */
  '+',
    0,	/* 79 - End key*/
    0,	/* Down Arrow */
    0,	/* Page Down */
    0,	/* Insert Key */
    0,	/* Delete Key */
    0,   0,   0,
    0,	/* F11 Key */
    0,	/* F12 Key */
    0,	/* All other keys are undefined */
};


// --------------------------
//       Time Utils
// --------------------------

uint8_t bcd_to_bin(uint8_t bcd) {
    return (bcd & 0x0F) + ((bcd >> 4) * 10);
}

uint64_t rtc_to_unix(uint8_t sec, uint8_t min, uint8_t hour, 
                     uint8_t day, uint8_t month, uint8_t year) {
    int y = 2000 + year;
    if (y < 1970) y += 100;
    
    int leap;
    int years = y - 1970;
    int days;
    
    leap = (years + 2) / 4;
    if (y % 400 == 0 || (y % 4 == 0 && y % 100 != 0)) {
        leap--;
    }
    
    static const int month_days[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int md = 0;
    for (int i = 0; i < month - 1; i++) {
        md += month_days[i];
    }
    
    days = years * 365 + leap + md + day - 1;
    if (month > 2 && (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0))) {
        days++;
    }
    
    return (uint64_t)days * 86400 + hour * 3600 + min * 60 + sec;
}


// --------------------------
//        Data Utils
// --------------------------

// DANGER: DONT USE THIS PLS
void* kernel_memset64(void* ptr, uint64_t value, uint64_t n) {
    n /= 8;
	uint64_t* p = ptr;
    while (n--) *p++ = value;
    return ptr;
}

void* kernel_memset(void* ptr, uint8_t value, uint64_t n) {
    uint64_t pattern = (uint64_t)value;
    pattern |= pattern << 8;
    pattern |= pattern << 16;
    pattern |= pattern << 32;
    uint64_t* ptr_64 = (uint64_t*)ptr;
    uint64_t chunks = n / 8;
    uint64_t remainder = n % 8;
    while (chunks--) {
        *ptr_64++ = pattern;
    }
    uint8_t* ptr_8 = (uint8_t*)ptr_64;
    while (remainder--) {
        *ptr_8++ = value;
    }

    return ptr;
}

void kernel_memcpy(void* dest, const void* src, uint64_t n) {
	uint64_t n1 = n / 8, n2 = n % 8;
    uint64_t* d1 = dest;
    const uint64_t* s1 = src;
    while (n1--) *d1++ = *s1++;
	uint8_t* d2 = (uint8_t*)d1;
    const uint8_t* s2 = (const uint8_t*)s1;
	while (n2--) *d2++ = *s2++;
}

void kernel_to_upper(char* s) {
    while (*s) {
        if (*s >= 'a' && *s <= 'z') *s -= 32;
        s++;
    }
}

int kernel_strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

char *kernel_strcpy(char *dest, const char *src) {
    char *start = dest;
    while ((*dest++ = *src++));
    return start;
}

uint64_t kernel_strnlen(const char* s, uint64_t maxlen) {
    uint64_t len = 0;
    while (len < maxlen && s[len] != '\0') {
        len++;
    }
    return len;
}


// -------------------------
//    Hardware Callbacks
// -------------------------

void kernel_on_timer_tick(void) {
    ticks++;
    if (current_thread != 0) {
        schedule();
    }
}

void kernel_on_keyboard_irq(uint8_t scancode) {
    if (keyboard_driver_tid != 0) {
        message_t msg;
        msg.sender_tid = 0;
        msg.type = MSG_TYPE_KEYBOARD;
        msg.subtype = MSG_SUBTYPE_SEND;
        msg.param1 = scancode;
        msg.param2 = 0;
        msg.param3 = 0;
        ipc_send(keyboard_driver_tid, &msg);
    }
}

void kernel_handle_user_exception(uint64_t int_no, uint64_t instruction_pointer) {
    kprint_error("Killing process: ");
    kprint_error(current_thread->owner->name);
    kprint_error(" (PID: ");
    char pid_buf[32];
    uint64_to_dec(current_thread->owner->id, pid_buf);
    kprint_error(pid_buf);
    kprint_error(") due to exception ");
    uint64_to_dec(int_no, pid_buf);
    kprint_error(pid_buf);
    kprint_error(" at IP=0x");
    uint64_to_hex(instruction_pointer, pid_buf);
    kprint_error(pid_buf);
    kprint_error("\n");
    
    kill_thread(current_thread, -((int)int_no));
    schedule();
}


// -------------------------
//           Heap
// -------------------------

int expand_heap(uint64_t size) {
    uint64_t needed_bytes = size + sizeof(malloc_header_t);
    uint64_t needed_pages = (needed_bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint64_t i = 0; i < needed_pages; i++) {
        uint64_t phys = pmm_alloc_block();
        if (!phys) return 0;
        hal_map_page(heap_current_limit, phys, 0x3);
        heap_current_limit += PAGE_SIZE;
    }
    return 1;
}

void* kernel_malloc(uint64_t size) {
    if (size == 0) return 0;
    size = (size + 15) & ~15;
    uint64_t irq = spinlock_irq_save();
    spinlock_acquire(&heap_lock);
    if (!malloc_initialized) {
        if (!expand_heap(size)) {
            spinlock_release(&heap_lock);
            spinlock_irq_restore(irq);
            return 0;
        }
        free_list_start->size = (heap_current_limit - HEAP_START_ADDR) - sizeof(malloc_header_t);
        free_list_start->is_free = 1;
        free_list_start->next = 0;
        malloc_initialized = 1;
    }
restart_search:
    malloc_header_t* current = free_list_start;
    malloc_header_t* last = 0;
    while (current) {
        if (current->is_free && current->size >= size) {
            if (current->size > size + sizeof(malloc_header_t) + 16) {
                malloc_header_t* next_block = (malloc_header_t*)((uint8_t*)current + sizeof(malloc_header_t) + size);
                next_block->size = current->size - size - sizeof(malloc_header_t);
                next_block->is_free = 1;
                next_block->next = current->next;

                current->size = size;
                current->next = next_block;
            }
            current->is_free = 0;
            spinlock_release(&heap_lock);
            spinlock_irq_restore(irq);
            return (void*)((uint8_t*)current + sizeof(malloc_header_t));
        }
        last = current;
        current = current->next;
    }
    if (expand_heap(size)) {
        if (!last) last = free_list_start;
        while(last->next) last = last->next;
        malloc_header_t* new_block = (malloc_header_t*)((uint8_t*)last + sizeof(malloc_header_t) + last->size);
        if ((uint64_t)new_block >= heap_current_limit) {
            spinlock_release(&heap_lock);
            spinlock_irq_restore(irq);
            return 0;
        }
        new_block->size = (heap_current_limit - (uint64_t)new_block) - sizeof(malloc_header_t);
        new_block->is_free = 1;
        new_block->next = 0;
        last->next = new_block;
        goto restart_search;
    }

    spinlock_release(&heap_lock);
    spinlock_irq_restore(irq);
    return 0;
}

void kernel_free(void* ptr) {
    if (!ptr) return;
    uint64_t irq = spinlock_irq_save();
    spinlock_acquire(&heap_lock);
    malloc_header_t* header = (malloc_header_t*)((uint8_t*)ptr - sizeof(malloc_header_t));
    header->is_free = 1;
    malloc_header_t* current = free_list_start;
    while (current && current->next) {
        if (current->is_free && current->next->is_free) {
            if ((uint8_t*)current + sizeof(malloc_header_t) + current->size == (uint8_t*)current->next) {
                current->size += current->next->size + sizeof(malloc_header_t);
                current->next = current->next->next;
                continue;
            }
        }
        current = current->next;
    }
    spinlock_release(&heap_lock);
    spinlock_irq_restore(irq);
}

// No kernel_free() pls
void* kernel_malloc_aligned(uint64_t size, uint64_t alignment) {
    void* ptr = kernel_malloc(size + alignment + sizeof(void*));
    if (!ptr) return 0;
    uint64_t addr = (uint64_t)ptr;
    uint64_t aligned_addr = (addr + (alignment - 1)) & ~(alignment - 1);
    if (aligned_addr < addr) aligned_addr += alignment;

    return (void*)aligned_addr;
}


// -------------------------
//          FAT32
// -------------------------

void fat32_entry_to_dirent(struct fat32_dir_entry* raw, fat32_dirent_t* out) {
    kernel_memset(out->name, 0, 256);
    kernel_memcpy(out->name, raw->name, 11);

    out->cluster = ((uint64_t)raw->cluster_high << 16) | (uint64_t)raw->cluster_low;
    out->size = (uint64_t)raw->file_size;
    out->attr = raw->attr;

    out->write_date   = raw->wrt_date;
    out->write_time   = raw->wrt_time;
}

void fat32_collect_lfn_chars(struct fat32_lfn_entry* lfn, char* lfn_buffer) {
    int order = lfn->order & 0x1F;
    if (order < 1 || order > 20) return;

    int index = (order - 1) * 13;
    if (index < 0 || index + 13 > 255) return;

    for (int i = 0; i < 5; i++)  lfn_buffer[index++] = (char)(lfn->name1[i] & 0xFF);
    for (int i = 0; i < 6; i++)  lfn_buffer[index++] = (char)(lfn->name2[i] & 0xFF);
    for (int i = 0; i < 2; i++)  lfn_buffer[index++] = (char)(lfn->name3[i] & 0xFF);
}

void fat32_format_sfn(char* dest, const char* sfn_name) {
    int p = 0;
    for (int i = 0; i < 8; i++) {
        if (sfn_name[i] == ' ') break;
        dest[p++] = sfn_name[i];
    }
    if (sfn_name[8] != ' ') {
        dest[p++] = '.';
        for (int i = 8; i < 11; i++) {
            if (sfn_name[i] == ' ') break;
            dest[p++] = sfn_name[i];
        }
    }
    dest[p] = '\0';
}

unsigned char fat32_checksum(unsigned char *pName) {
    unsigned char sum = 0;
    for (int i = 11; i; i--) {
        sum = ((sum & 1) << 7) + (sum >> 1) + *pName++;
    }
    return sum;
}

fat32_dirent_t* fat32_read_dir(volume_t* v, fat32_dirent_t* dir_entry, int* out_count) {
    uint8_t buffer[512];
    uint32_t start_cluster = (dir_entry == 0) ? v->root_cluster : (uint32_t)dir_entry->cluster;
    uint32_t current_cluster = start_cluster;
    int total_files = 0;

    while (current_cluster < 0x0FFFFFF8 && current_cluster >= 2) {
        uint64_t lba = cluster_to_lba(v, current_cluster);
        for (uint32_t s = 0; s < v->sec_per_clus; s++) {
            hal_disk_read(v->device.id, lba + s, 1, buffer); // ИЗМЕНЕНО НА HAL
            struct fat32_dir_entry* dir = (struct fat32_dir_entry*)buffer;
            for (int i = 0; i < 16; i++) {
                if (dir[i].name[0] == 0x00) goto count_finished;
                if (dir[i].name[0] == 0xE5) continue;
                if (dir[i].attr == 0x0F) continue;
                if (dir[i].attr & 0x08) continue;
                total_files++;
            }
        }
        current_cluster = get_next_cluster(v, current_cluster);
    }

count_finished:
    if (total_files == 0) {
        *out_count = 0;
        return 0;
    }

    fat32_dirent_t* result_array = (fat32_dirent_t*)kernel_malloc(sizeof(fat32_dirent_t) * total_files);
    if (!result_array) {
        *out_count = 0;
        return 0;
    }

    current_cluster = start_cluster;
    int current_index = 0;
    char lfn_temp[256];
    uint8_t lfn_checksum = 0;
    kernel_memset(lfn_temp, 0, 256);

    while (current_cluster < 0x0FFFFFF8 && current_cluster >= 2) {
        uint64_t lba = cluster_to_lba(v, current_cluster);
        for (uint32_t s = 0; s < v->sec_per_clus; s++) {
            hal_disk_read(v->device.id, lba + s, 1, buffer);
            struct fat32_dir_entry* dir = (struct fat32_dir_entry*)buffer;

            for (int i = 0; i < 16; i++) {
                if (dir[i].name[0] == 0x00) {
                    *out_count = current_index;
                    return result_array;
                }
                if (dir[i].name[0] == 0xE5) {
                    kernel_memset(lfn_temp, 0, 256);
                    lfn_checksum = 0;
                    continue;
                }

                if (dir[i].attr == 0x0F) {
                    struct fat32_lfn_entry* lfn = (struct fat32_lfn_entry*)&dir[i];

                    if (lfn->order & 0x40) {
                        kernel_memset(lfn_temp, 0, 256);
                        lfn_checksum = lfn->checksum;
                    }

                    fat32_collect_lfn_chars(lfn, lfn_temp);
                    continue;
                }

                if (dir[i].attr & 0x08) {
                    kernel_memset(lfn_temp, 0, 256);
                    continue;
                }

                fat32_entry_to_dirent(&dir[i], &result_array[current_index]);
                uint8_t sfn_sum = fat32_checksum((unsigned char*)dir[i].name);
                if (lfn_temp[0] != 0 && sfn_sum == lfn_checksum) {
                    kernel_memcpy(result_array[current_index].name, lfn_temp, 256);
                } else {
                    fat32_format_sfn(result_array[current_index].name, dir[i].name);
                }
                kernel_memset(lfn_temp, 0, 256);
                lfn_checksum = 0;
                current_index++;
                if (current_index >= total_files) {
                     *out_count = current_index;
                     return result_array;
                }
            }
        }
        current_cluster = get_next_cluster(v, current_cluster);
    }
    *out_count = current_index;
    return result_array;
}

int fat32_find_in_dir(volume_t* v, fat32_dirent_t* dir_entry, const char* search_name, fat32_dirent_t* result) {
    if (dir_entry != 0 && !(dir_entry->attr & 0x10)) return 0;

    char name_upper[256];
    kernel_memcpy(name_upper, search_name, 255);
    name_upper[255] = 0;
    kernel_to_upper(name_upper);

    int count = 0;
    fat32_dirent_t* file_list = fat32_read_dir(v, dir_entry, &count);

    if (!file_list) return 0;

    int found = 0;
    for (int i = 0; i < count; i++) {
        char file_name_upper[256];
        kernel_memcpy(file_name_upper, file_list[i].name, 256);
        kernel_to_upper(file_name_upper);

        if (kernel_strcmp(file_name_upper, name_upper) == 0) {
            kernel_memcpy(result, &file_list[i], sizeof(fat32_dirent_t));
            found = 1;
            break;
        }
    }

    kernel_free(file_list);
    return found;
}

void* fat32_read_file(volume_t* v, fat32_dirent_t* file, uint64_t* out_size) {
    if (file == 0 || (file->attr & 0x10)) return 0;
    uint8_t* destination = (uint8_t*)kernel_malloc(file->size);
    if (!destination) {
        return 0;
    }
    if (out_size != 0) {
        *out_size = file->size;
    }
    uint32_t cluster = (uint32_t)file->cluster;
    uint64_t bytes_left = file->size;
    uint64_t offset = 0;
    uint8_t temp_sector[512];

    while (bytes_left > 0 && cluster < 0x0FFFFFF8 && cluster >= 2) {
        uint64_t lba = cluster_to_lba(v, cluster);
        uint64_t cluster_size = v->sec_per_clus * 512;
        if (bytes_left >= cluster_size) {
            hal_disk_read(v->device.id, lba, v->sec_per_clus, destination + offset);
            offset += cluster_size;
            bytes_left -= cluster_size;
        }
        else {
            for (uint32_t i = 0; i < v->sec_per_clus && bytes_left > 0; i++) {
                if (bytes_left >= 512) {
                    hal_disk_read(v->device.id, lba + i, 1, destination + offset);
                    offset += 512;
                    bytes_left -= 512;
                } else {
                    hal_disk_read(v->device.id, lba + i, 1, temp_sector);
                    kernel_memcpy(destination + offset, temp_sector, bytes_left);
                    bytes_left = 0;
                    break;
                }
            }
        }
        if (bytes_left > 0) {
            cluster = get_next_cluster(v, cluster);
        }
    }
    return (void*)destination;
}


// -------------------------
//         Process
// -------------------------

process_t* create_process(const char* name) {
    process_t* new_proc = (process_t*)kernel_malloc(sizeof(process_t));
    if (!new_proc) return 0;
    kernel_memset(new_proc, 0, sizeof(process_t));

    static uint32_t next_pid = 1;
    new_proc->id = next_pid++;
    if (name) kernel_memcpy(new_proc->name, name, 31);
    new_proc->state = THREAD_READY;

    new_proc->page_directory = hal_create_address_space();
    if (!new_proc->page_directory) {
        kernel_free(new_proc);
        return 0;
    }

    new_proc->next_shm_vaddr = 0x600000000000ULL;

    return new_proc;
}


// -------------------------
//      Driver Registry
// -------------------------

int64_t register_driver(driver_type_t type, const char* user_name) {
    char name_buf[DRIVER_NAME_MAX];
    kernel_memset(name_buf, 0, DRIVER_NAME_MAX);
    if (user_name != 0 && !copy_string_from_user(user_name, name_buf, DRIVER_NAME_MAX)) {
        return -1;
    }
    
    uint64_t irq = hal_irq_save();
    
    driver_info_t* cur = drivers_list_head;
    while (cur) {
        if (type != DT_NONE && type != DT_USER && cur->type == type) {
            hal_irq_restore(irq);
            return -2;
        }
        if (name_buf[0] != 0 && kernel_strcmp(cur->name, name_buf) == 0) {
            hal_irq_restore(irq);
            return -3;
        }
        cur = cur->next;
    }
    driver_info_t* new_driver = (driver_info_t*)kernel_malloc(sizeof(driver_info_t));
    if (!new_driver) {
        hal_irq_restore(irq);
        return -4;
    }
    new_driver->thread = current_thread;
    new_driver->tid = current_thread->tid;
    new_driver->type = type;
    kernel_memcpy(new_driver->name, name_buf, DRIVER_NAME_MAX);
    new_driver->next = drivers_list_head;
    drivers_list_head = new_driver;
    
    hal_irq_restore(irq);
    return 0;
}

uint64_t get_driver_tid(driver_type_t type) {
    driver_info_t* cur = drivers_list_head;
    while (cur) {
        if (cur->type == type) {
            return cur->tid;
        }
        cur = cur->next;
    }
    return 0;
}

uint64_t get_driver_tid_by_name(const char* name) {
    driver_info_t* cur = drivers_list_head;
    while (cur) {
        if (kernel_strcmp(cur->name, name) == 0) {
            return cur->tid;
        }
        cur = cur->next;
    }
    return 0;
}


// -------------------------
//         Spinlock
// -------------------------

uint64_t spinlock_irq_save(void) {
    return hal_irq_save();
}

void spinlock_irq_restore(uint64_t flags) {
    hal_irq_restore(flags);
}

void spinlock_acquire(spinlock_t* lock) {
    while (__sync_lock_test_and_set(lock, 1)) {
        hal_cpu_relax();
    }
}

void spinlock_release(spinlock_t* lock) {
    __sync_lock_release(lock);
}


// ------------------------
//         Other
// ------------------------

void reset_state() {
	kernel_memset(&state, 0, sizeof(st_flags_t));
	state.system_flags = CAN_REGISTER_KERNEL_DRIVERS | CAN_PRINT;
}


// ------------------------
//      MAIN THREADS
// ------------------------

void kernel_main(boot_info_t* boot_info) {
    reset_state();
    if (!(boot_info->flags & BOOT_FLAG_VIDEO_PRESENT)) {
        hal_debug_print_early("VIDEO IS NOT SUPPORTED!");
        hal_halt();
    }
    font = &fontvgasys;
    video = &boot_info->video;
    _kclear();
    _kprint("AOSLDR, hello from long mode...\n");

    char buff[32];
    kernel_memset(buff, 0, 32);
    if (boot_info->magic != BOOT_MAGIC) kernel_error(0x8, 0, boot_info->magic, BOOT_MAGIC, 0);
    _kprint("Loader type: 0x");
    uint64_to_hex(boot_info->type, buff);
    _kprint(buff);
    _kprint("\n");
    if (boot_info->type == BOOT_TYPE_UNKNOWN) kernel_error(0x8, 2, boot_info->type, 0, 0);

    hal_cpu_init();
    hal_syscall_init();

    uint64_t total_ram = hal_get_total_ram(boot_info);
    kernel_memset(buff, 0, 32);
    uint64_to_dec(total_ram / 1024 / 1024, buff);
    _kprint("Total RAM: ");
    _kprint(buff);
    _kprint(" MB\n");

    hal_vm_init();
    uint64_t bitmap_addr = KERNEL_BASE + 0x100000;
    init_pmm(total_ram, bitmap_addr);
    pmm_deinit_region(0x0, 0x1000000);
    pmm_init_region(0x1000000, total_ram - 0x1000000);
    _kprint("PMM & VMM is configured!\n");

    hal_interrupts_init();
    hal_timer_init(TIMER_FREQ);
    boot_time = hal_get_boot_time();

    init_scheduler();
    _kprint("IDT, PIC & Scheduler are set! We're safe\n");
    
    hal_enable_interrupts();
    create_kernel_thread(idle_thread);

    hal_storage_init(boot_info->specific.mbr.drive_num);

    kprint("System volume is ");
    kernel_memset(buff, 0, 32);
    get_drv_device_name(system_ide, buff);
    kprint(buff);
    kprint("/");
    kernel_memset(buff, 0, 32);
    get_volume_name(system_volume, buff);
    kprint(buff);
    kprint("\n");

    int entries = 0;
    fat32_dirent_t* files;
    kprint("Load drivers...\n");
    
    fat32_dirent_t file, drivers_dir;
    if (!fat32_find_in_dir(system_volume, 0, "DRIVERS", &drivers_dir)){
        kernel_error(0x4, system_volume->id, 0, 0, 0);
    }
    files = fat32_read_dir(system_volume, &drivers_dir, &entries);
    for (int i = 0; i < entries; i++) {
        kprint(" - ");
        kprint(files[i].name);
        if (files[i].attr & 0x10) kprint(" [D]");
        kprint("\n");
    }

    // AUTH Driver
    elf_load_result_t* driver = (elf_load_result_t*)kernel_malloc(sizeof(elf_load_result_t));
    kprint("Auth driver..\n");
    if (!fat32_find_in_dir(system_volume, &drivers_dir, "AUTHDRIVER.ELF", &file)){
        kernel_error(0x4, system_volume->id, drivers_dir.cluster, 0, 0);
    }
    kernel_memset(driver, 0, sizeof(elf_load_result_t));
    load_elf_raw_fat32(system_volume, &file, driver);
    if (driver->result != ELF_RESULT_OK) kernel_error(0x6, driver->result, driver->entry_point, 0, 0);
    start_elf_process(driver, 0, 0);
    
    int tid = 0;
    driver_type_t dtype = DT_AUTH;
    if(!sleep_while_zero(get_driver_tid_sleep_wrapper, &dtype, 5000, &tid)) kernel_error(0x6, 0x1DEAD, dtype, 0, 0);

    // VFS Driver
    kprint("VFS driver..\n");
    if (!fat32_find_in_dir(system_volume, &drivers_dir, "VFSDRIVER.ELF", &file)){
        kernel_error(0x4, system_volume->id, drivers_dir.cluster, 0, 0);
    }
    kernel_memset(driver, 0, sizeof(elf_load_result_t));
    load_elf_raw_fat32(system_volume, &file, driver);
    if (driver->result != ELF_RESULT_OK) kernel_error(0x6, driver->result, driver->entry_point, 0, 0);
    start_elf_process(driver, 0, 0);
    
    tid = 0;
    dtype = DT_VFS;
    if(!sleep_while_zero(get_driver_tid_sleep_wrapper, &dtype, 5000, &tid)) kernel_error(0x6, 0x1DEAD, dtype, 0, 0);

    // INIT Driver
    kprint("Init driver..\n");
    if (!fat32_find_in_dir(system_volume, &drivers_dir, "INITDRIVER.ELF", &file)){
        kernel_error(0x4, system_volume->id, drivers_dir.cluster, 0, 0);
    }
    kernel_memset(driver, 0, sizeof(elf_load_result_t));
    load_elf_raw_fat32(system_volume, &file, driver);
    if (driver->result != ELF_RESULT_OK) kernel_error(0x6, driver->result, driver->entry_point, 0, 0);
    start_elf_process(driver, 0, 0);
    
    tid = 0;
    dtype = DT_INIT;
    if(!sleep_while_zero(get_driver_tid_sleep_wrapper, &dtype, 5000, &tid)) kernel_error(0x6, 0x1DEAD, dtype, 0, 0);

    kprint("Stage 2 completed!\n");
    
    kill_thread(current_thread, 0);
    schedule();
}

void idle_thread() {
    while(1) {
        hal_enable_interrupts();
        hal_cpu_relax();
    }
}