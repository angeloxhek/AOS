#include <kernel/internal.h>
#include <kernel/fonts.h>

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
	"RESERVED1",
	"RESERVED2",
	"INITRD_ERROR",
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

process_t kernel_process;
thread_t* current_thread;
thread_t* ready_queue;
uint64_t thread_count = 0;
thread_t* zombies_list = 0;
thread_t* idle_thread_ptr = 0;

spinlock_t kprint_lock = 0;
spinlock_t heap_lock = 0;
spinlock_t pmm_lock = 0;
volatile uint64_t ticks = 0;
uint64_t boot_time = 0;

driver_info_t* drivers_list_head;
apid_t input_driver_pid = 0;

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

char *kernel_strncpy(char *dest, const char *src, uint64_t n) {
    char *start = dest;
    while (n > 0 && *src != '\0') {
        *dest++ = *src++;
        n--;
    }
    while (n > 0) {
        *dest++ = '\0';
        n--;
    }
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

void kernel_on_ps2_irq(int irq_number) {
    if (input_driver_pid == 0) input_driver_pid = get_driver_pid(DT_INPUT);
    if (input_driver_pid == 0) return;
	message_t msg;
	kernel_memset(&msg, 0, sizeof(message_t));
	msg.type = MSG_TYPE_HARDWARE; 
	msg.subtype = MSG_SUBTYPE_SEND;
	msg.param1 = HW_EVT_IRQ;
	msg.param2 = irq_number;
	
	ipc_send(input_driver_pid, &msg);
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
//         Process
// -------------------------

process_t* create_process(const char* name) {
    process_t* new_proc = (process_t*)kernel_malloc(sizeof(process_t));
    if (!new_proc) return 0;
    kernel_memset(new_proc, 0, sizeof(process_t));

    static apid_t next_pid = 1;
    new_proc->id = next_pid++;
    if (name) kernel_strncpy(new_proc->name, name, 31);
    new_proc->state = THREAD_READY;

    new_proc->page_directory = hal_create_address_space();
    if (!new_proc->page_directory) {
        kernel_free(new_proc);
        return 0;
    }

    new_proc->next_shm_vaddr = 0x600000000000ULL;
	
	new_proc->peb_phys_page = pmm_alloc_block();

	hal_map_page_in_space((uint64_t)new_proc->page_directory, PEB_VIRT_ADDR, new_proc->peb_phys_page, 0x5);

	void* kvirt = temp_map(new_proc->peb_phys_page);
	kernel_memset(kvirt, 0, 4096);

	aos_peb_t* peb = (aos_peb_t*)kvirt;
	peb->pid = new_proc->id;
	peb->pending_msgs = 0;
	get_time_info(&peb->startup_time);
	if (name) kernel_strncpy(peb->process_name, name, 31);

	temp_unmap(kvirt);

    return new_proc;
}


// -------------------------
//      Driver Registry
// -------------------------

int64_t register_driver(driver_type_t type, const char* user_name, uint32_t perms, uint16_t* allowed_ports, process_t* process) {
    char name_buf[DRIVER_NAME_MAX];
    kernel_memset(name_buf, 0, DRIVER_NAME_MAX);
    if (user_name != 0) {
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
	if (!process) {
		new_driver->process = current_thread->owner;
		new_driver->pid = current_thread->owner->id;
	} else {
		new_driver->process = process;
		new_driver->pid = process->id;
	}
    new_driver->type = type;
    kernel_memcpy(new_driver->name, name_buf, DRIVER_NAME_MAX);
	new_driver->driver_perms = perms;
	kernel_memcpy(new_driver->allowed_ports, allowed_ports, ALLOWED_PORTS_MAX*sizeof(uint16_t));
    new_driver->next = drivers_list_head;
    drivers_list_head = new_driver;
    
    hal_irq_restore(irq);
    return 0;
}

apid_t get_driver_pid(driver_type_t type) {
    driver_info_t* cur = drivers_list_head;
    while (cur) {
        if (cur->type == type) {
            return cur->pid;
        }
        cur = cur->next;
    }
    return 0;
}

driver_info_t* get_driver_by_pid(apid_t pid) {
    driver_info_t* cur = drivers_list_head;
    while (cur) {
        if (cur->pid == pid) {
            return cur;
        }
        cur = cur->next;
    }
    return 0;
}

apid_t get_driver_pid_by_name(const char* name) {
    driver_info_t* cur = drivers_list_head;
    while (cur) {
        if (kernel_strcmp(cur->name, name) == 0) {
            return cur->pid;
        }
        cur = cur->next;
    }
    return 0;
}


// -------------------------
//          InitRD
// -------------------------

uint8_t* load_file_initrd(void* initrd, const char* name, uint64_t* out_size) {
    uint8_t* ptr = (uint8_t*)initrd;
    
    while (1) {
        tar_header_t* header = (tar_header_t*)ptr;
        
        if (header->name[0] == '\0') break; 
        
        uint64_t file_size = octal_to_int(header->size);
        
        if (kernel_strcmp(header->name, name) == 0) {
            uint8_t* file_buf = (uint8_t*)kernel_malloc(file_size);
            if (!file_buf) return 0;
            
            kernel_memcpy(file_buf, ptr + 512, file_size);
			if (out_size) *out_size = file_size;
            return file_buf;
        }
        
        uint64_t offset = 512 + ((file_size + 511) & ~511);
        ptr += offset;
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
	if (boot_info->version != BOOT_VERSION) kernel_error(0x8, 2, boot_info->version, BOOT_VERSION, 0);
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
    _kprint("Memory is configured!\n");

    hal_interrupts_init();
    hal_timer_init(TIMER_FREQ);
    boot_time = hal_get_boot_time();

    init_scheduler();
    _kprint("Threads are set! We're safe\n");
    
    hal_enable_interrupts();
	serial_init();
    thread_t* t = create_kernel_thread(idle_thread);
	idle_thread_ptr = t;

	if (ready_queue == t) {
		ready_queue = t->next;
	}
	
	thread_t* prev = t;
	while (prev->next != t && prev->next != NULL) prev = prev->next;
	if (prev != t) prev->next = t->next;

    kprint("Load drivers...\n");
    
	void* initrd_vaddr = (void*)boot_info->initrd_addr;
    elf_load_result_t* driver = (elf_load_result_t*)kernel_malloc(sizeof(elf_load_result_t));
    uint8_t* file_buf;
	uint64_t file_size;
    apid_t pid = 0;
    driver_type_t dtype;

     // --- AUTH Driver ---
    kprint("Auth driver..\n");
    dtype = DT_AUTH;
    file_buf = load_file_initrd(initrd_vaddr, "AUTHDRIVER.ELF", &file_size); 
    if (!file_buf) {
        kernel_error(0x4, dtype, 0, 0, 0);
    }
    
    kernel_memset(driver, 0, sizeof(elf_load_result_t));
    load_elf_raw("AUTHDRIVER.ELF", file_buf, file_size, driver); 
    kernel_free(file_buf);
    
    if (driver->result != ELF_RESULT_OK) kernel_error(0x6, driver->result, driver->entry_point, 0, 0);
    start_elf_process(driver, 0, 0);
    pid = 0;
    if(!sleep_while_zero(get_driver_pid_sleep_wrapper, &dtype, 5000, (int*)&pid)) kernel_error(0x6, 0x1DEAD, dtype, 0, 0);


    // --- VFS Driver ---
    kprint("VFS driver..\n");
    dtype = DT_VFS;
    file_buf = load_file_initrd(initrd_vaddr, "VFSDRIVER.ELF", &file_size);
    if (!file_buf) {
        kernel_error(0x4, dtype, 0, 0, 0);
    }
    
    kernel_memset(driver, 0, sizeof(elf_load_result_t));
    load_elf_raw("VFSDRIVER.ELF", file_buf, file_size, driver);
    kernel_free(file_buf);
    
    if (driver->result != ELF_RESULT_OK) kernel_error(0x6, driver->result, driver->entry_point, 0, 0);
    start_elf_process(driver, 0, 0);
    pid = 0;
    if(!sleep_while_zero(get_driver_pid_sleep_wrapper, &dtype, 5000, (int*)&pid)) kernel_error(0x6, 0x1DEAD, dtype, 0, 0);


    // --- INIT Driver ---
    kprint("Init driver..\n");
    dtype = DT_INIT;
    file_buf = load_file_initrd(initrd_vaddr, "INITDRIVER.ELF", &file_size);
    if (!file_buf) {
        kernel_error(0x4, dtype, 0, 0, 0);
    }
    
    kernel_memset(driver, 0, sizeof(elf_load_result_t));
    load_elf_raw("INITDRIVER.ELF", file_buf, file_size, driver);
    kernel_free(file_buf);
    
    if (driver->result != ELF_RESULT_OK) kernel_error(0x6, driver->result, driver->entry_point, 0, 0);
    start_elf_process(driver, 0, 0);
    pid = 0;
    if(!sleep_while_zero(get_driver_pid_sleep_wrapper, &dtype, 5000, (int*)&pid)) kernel_error(0x6, 0x1DEAD, dtype, 0, 0);

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