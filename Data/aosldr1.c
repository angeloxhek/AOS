#include "include/aosldr.h"

#define IDT_INTERRUPTS \
    X(0)  X(1)  X(2)  X(3)  X(4)  X(5)  X(6)  X(7)  \
    X(8)  X(9)  X(10) X(11) X(12) X(13) X(14) X(15) \
    X(16) X(17) X(18) X(19) X(20) X(21) X(22) X(23) \
    X(24) X(25) X(26) X(27) X(28) X(29) X(30) X(31) \
    X(32) X(33) X(34) X(35) X(36) X(37) X(38) X(39) \
    X(40) X(41) X(42) X(43) X(44) X(45) X(46) X(47)
#define X(n) extern void isr##n();
IDT_INTERRUPTS
#undef X

// -------------------------
//           DATA
// -------------------------

unsigned char kbd_us[128] =
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

const char* kernel_messages[] = {
	"NO_ERROR_DEBUG",
	"STACK_SMASHING_DETECTED",
	"VOLUME_ERROR",
	"IDE_ERROR",
	"FS_NOT_FOUND",
	"MALLOC_ERROR",
	"DRIVER_ERROR",
	"ISR_ERROR"
};

struct idt_entry idt[256];
struct idt_ptr idtp;

extern uint32_t export_table[];
extern uint32_t export_table_end[];
extern void syscall_stub();

int cursor_x = 0;
int cursor_y = 0;
uint8_t kbd_buffer[KBD_BUFFER_SIZE];
uint32_t kbd_head = 0;
uint32_t kbd_tail = 0;

#define HEAP_START_ADDR 0xC0800000
static malloc_header_t* free_list_start = (malloc_header_t*)HEAP_START_ADDR;
static uint32_t heap_current_limit = HEAP_START_ADDR;
static int malloc_initialized = 0;

volume_t* system_volume = 0;
volume_t mounted_volumes[MAX_VOLUMES];
int volume_count = 0;

#define PAGE_PRESENT    0x1
#define PAGE_RW         0x2
#define PAGE_USER       0x4

// Выравнивание важно! Адреса должны быть кратны 4096 (4KB)
// Используем массив для Каталога Страниц (1024 записи)
extern uint32_t* page_directory;

uint32_t mem = 0;

#define BLOCK_SIZE 4096        // Размер страницы 4KB
#define BLOCKS_PER_BUCKET 32   // Мы будем использовать uint32_t для хранения битов (32 бита в числе)

// Глобальный массив битовой карты
// В реальной ОС вы будете вычислять размер динамически, но пока зарезервируем статически
// Допустим, макс 128 МБ RAM = 32768 фреймов = 1024 элемента массива uint32_t
uint32_t bitmap[1024]; 
uint32_t max_blocks = 32768; // Всего блоков
uint32_t used_blocks = 0;    // Сколько занято

#define PTE_PRESENT 0x1
#define PTE_RW      0x2
#define PTE_USER    0x4
#define PTE_FRAME   0xFFFFF000 // Маска для получения адреса (отбрасываем флаги)
#define PAGETABLES_BASE 0xFFC00000
#define PAGEDIRECTORY_BASE 0xFFFFF000
#define TEMP_PAGE_VIRT 0xFFBFF000

st_flags_t state;
symbol_t* global_symbol_table = NULL;


// --------------------------
//           ASM
// --------------------------

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ( "outb %b0, %w1" : : "a"(val), "Nd"(port) : "memory");
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ( "inb %w1, %b0" : "=a"(ret) : "Nd"(port) : "memory");
    return ret;
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile ( "inw %w1, %w0" : "=a"(ret) : "Nd"(port) : "memory");
    return ret;
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ( "outw %w0, %w1" : : "a"(val), "Nd"(port) : "memory");
}

static inline void insw(uint16_t port, void* addr, uint32_t count) {
    __asm__ volatile (
        "cld; rep insw"
        : "+D" (addr), "+c" (count)
        : "d" (port)
        : "memory"
    );
}


// --------------------------
//        Data Utils
// --------------------------

int expand_heap(uint32_t size) {
    uint32_t needed_pages = (size + sizeof(malloc_header_t) + 4095) / 4096;
    
    for (uint32_t i = 0; i < needed_pages; i++) {
        uint32_t phys = pmm_alloc_block();
        if (!phys) return 0; // Память в PMM закончилась
        
        vmm_map_page(phys, heap_current_limit, PAGE_PRESENT | PAGE_RW);
        heap_current_limit += 4096;
    }
    return 1;
}

int kernel_strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

void kernel_memcpy(void* dest, const void* src, uint32_t n) {
    uint8_t* d = dest;
    const uint8_t* s = src;
    while (n--) *d++ = *s++;
}

char *kernel_strncpy(char *dest, const char *src, uint32_t n) {
    uint32_t i;

    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }

    for ( ; i < n; i++) {
        dest[i] = '\0';
    }

    return dest;
}

void* kernel_malloc(uint32_t size) {
    // Выравниваем размер до 4 байт для корректности работы процессора
    size = (size + 3) & ~3;

    if (!malloc_initialized) {
        if (!expand_heap(size)) return 0;
        free_list_start->size = (heap_current_limit - HEAP_START_ADDR) - sizeof(malloc_header_t);
        free_list_start->is_free = 1;
        free_list_start->next = 0;
        malloc_initialized = 1;
    }

    malloc_header_t* current = free_list_start;
    malloc_header_t* last = 0;

    while (current) {
        if (current->is_free && current->size >= size) {
            // Split: если блок сильно больше, чем нужно, дробим его
            if (current->size > size + sizeof(malloc_header_t) + 4) {
                malloc_header_t* next_block = (malloc_header_t*)((uint8_t*)current + sizeof(malloc_header_t) + size);
                next_block->size = current->size - size - sizeof(malloc_header_t);
                next_block->is_free = 1;
                next_block->next = current->next;

                current->size = size;
                current->next = next_block;
            }
            
            current->is_free = 0;
            return (void*)((uint8_t*)current + sizeof(malloc_header_t));
        }
        last = current;
        current = current->next;
    }

    // Если мы здесь, значит подходящего блока не нашлось — расширяем кучу
    if (expand_heap(size)) {
        // Создаем новый блок в только что выделенной памяти
        malloc_header_t* new_block = (malloc_header_t*)((uint8_t*)last + sizeof(malloc_header_t) + last->size);
        new_block->size = (heap_current_limit - (uint32_t)new_block) - sizeof(malloc_header_t);
        new_block->is_free = 1;
        new_block->next = 0;
        last->next = new_block;

        // Повторяем поиск (рекурсивно один раз)
        return kernel_malloc(size);
    }

    return 0;
}

void kernel_free(void* ptr) {
    if (!ptr) return;

    malloc_header_t* header = (malloc_header_t*)((uint8_t*)ptr - sizeof(malloc_header_t));
    header->is_free = 1;

    // Проход по списку для склейки соседних свободных блоков
    malloc_header_t* current = free_list_start;
    while (current && current->next) {
        if (current->is_free && current->next->is_free) {
            // Поглощаем следующий блок
            current->size += current->next->size + sizeof(malloc_header_t);
            current->next = current->next->next;
            // Не двигаемся вперед, чтобы проверить, нельзя ли приклеить следующий
            continue;
        }
        current = current->next;
    }
}

void* kernel_realloc(void* ptr, uint32_t new_size) {
    if (!ptr) return kernel_malloc(new_size);
    
    malloc_header_t* header = (malloc_header_t*)((uint8_t*)ptr - sizeof(malloc_header_t));
    if (header->size >= new_size) return ptr;

    void* new_ptr = kernel_malloc(new_size);
    if (new_ptr) {
        kernel_memcpy(new_ptr, ptr, header->size);
        kernel_free(ptr);
    }
    return new_ptr;
}

void* kernel_malloc_aligned(uint32_t size, uint32_t alignment) {
    // 1. Запрашиваем память с запасом, чтобы точно влез выровненный блок
    // Нам нужно: размер + (выравнивание - 1)
    void* ptr = kernel_malloc(size + alignment - 1);
    if (!ptr) return 0;

    // 2. Считаем выровненный адрес
    uint32_t addr = (uint32_t)ptr;
    uint32_t aligned_addr = (addr + (alignment - 1)) & ~(alignment - 1);

    return (void*)aligned_addr;
}

void* kernel_memset(void* ptr, int value, uint32_t num) {
    uint8_t* p = (uint8_t*)ptr;
    while (num--) {
        *p++ = (uint8_t)value;
    }
    return ptr;
}

void kernel_to_upper(char* s) {
    while (*s) {
        if (*s >= 'a' && *s <= 'z') *s -= 32;
        s++;
    }
}


// -------------------------
//     Print Functions
// -------------------------

void kclear() {
	if (!(state.system_flags & CAN_PRINT)) return;
    volatile char* video_memory = (volatile char*)0xB8000;
    for (int i = 0; i < VIDEO_WIDTH * VIDEO_HEIGHT; i++) {
        video_memory[i * 2] = ' ';
        video_memory[i * 2 + 1] = 0x07;
    }
    cursor_x = 0;
    cursor_y = 0;
}

void kprint_char(char c, uint8_t color) {
	if (!(state.system_flags & CAN_PRINT)) return;
    volatile char* video_memory = (volatile char*)0xB8000;
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else {
        int offset = (cursor_y * VIDEO_WIDTH + cursor_x) * 2;
        video_memory[offset] = c;
        video_memory[offset + 1] = color;
        cursor_x++;
    }
    if (cursor_x >= VIDEO_WIDTH) {
        cursor_x = 0;
        cursor_y++;
    }
    if (cursor_y >= VIDEO_HEIGHT) {
        kclear();
    }
}

void kprint(const char* str) {
	if (!(state.system_flags & CAN_PRINT)) return;
    for (int i = 0; str[i] != 0; i++) {
        kprint_char(str[i], 0x0B);
    }
}

void kprint_error(const char* str) {
	if (!(state.system_flags & CAN_PRINT)) return;
    for (int i = 0; str[i] != 0; i++) {
        kprint_char(str[i], 0x0C);
    }
}

void uint32_to_hex(uint32_t value, char* out_buffer) {
    const char *hex_digits = "0123456789ABCDEF";
    out_buffer[0] = hex_digits[(value >> 28) & 0x0F];
    out_buffer[1] = hex_digits[(value >> 24) & 0x0F];
    out_buffer[2] = hex_digits[(value >> 20) & 0x0F];
    out_buffer[3] = hex_digits[(value >> 16) & 0x0F];
    out_buffer[4] = hex_digits[(value >> 12) & 0x0F];
    out_buffer[5] = hex_digits[(value >> 8) & 0x0F];
    out_buffer[6] = hex_digits[(value >> 4) & 0x0F];
    out_buffer[7] = hex_digits[value & 0x0F];
	out_buffer[8] = 0;
}

void uint32_to_dec(uint32_t value, char* out_buffer) {
    char temp[11];
    int i = 0;

    if (value == 0) {
        out_buffer[0] = '0';
        out_buffer[1] = '\0';
        return;
    }

    while (value > 0) {
        temp[i++] = (value % 10) + '0';
        value /= 10;
    }

    int j = 0;
    while (i > 0) {
        out_buffer[j++] = temp[--i];
    }
    
    out_buffer[j] = '\0';
}

void kernel_error(uint32_t code, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4) {
	//kclear();
    kprint_error("KERNEL STOP: 0x");
	char buff[9];
	uint32_to_hex(code, buff);
	kprint_error(buff);
	kprint_error(" (");
	kprint_error(kernel_messages[code]);
    kprint_error(")\nARGS: 0x");
	uint32_to_hex(arg1, buff);
	kprint_error(buff);
	kprint_error("; 0x");
	uint32_to_hex(arg2, buff);
	kprint_error(buff);
	kprint_error("; 0x");
	uint32_to_hex(arg3, buff);
	kprint_error(buff);
	kprint_error("; 0x");
	uint32_to_hex(arg4, buff);
	kprint_error(buff);
	kprint_error("\nThe system has been halted!");
    for(;;);
}


// -------------------
//      IDT & PIC
// -------------------

void pic_remap() {
    uint8_t a1, a2;

    a1 = inb(0x21);
    a2 = inb(0xA1);

    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20);
    outb(0xA1, 0x28);
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);

    outb(0x21, a1);
    outb(0xA1, a2);
}

void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low = (base & 0xFFFF);
    idt[num].base_high = (base >> 16) & 0xFFFF;
    idt[num].sel = sel;
    idt[num].always0 = 0;
    idt[num].flags = flags;
}

void idt_install() {
    idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
    idtp.base = (uint32_t)&idt;

    for(int i = 0; i < 256; i++) {
        idt_set_gate(i, 0, 0, 0);
    }

    #define X(n) idt_set_gate(n, (uint32_t)isr##n, 0x08, 0x8E);
    IDT_INTERRUPTS
    #undef X
    
    __asm__ __volatile__("lidt (%0)" : : "r" (&idtp));
}

void timer_init(uint32_t frequency) {
    uint32_t divisor = 1193180 / frequency;
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}


// --------------------
//       Keyboard
// --------------------

void kbd_push(uint8_t scancode) {
    uint32_t next = (kbd_head + 1) % KBD_BUFFER_SIZE;
    if (next != kbd_tail) {
        kbd_buffer[kbd_head] = scancode;
        kbd_head = next;
    }
}

uint8_t kbd_pop() {
    if (kbd_head == kbd_tail) return 0;
    uint8_t scancode = kbd_buffer[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUFFER_SIZE;
    return scancode;
}

char kbd_get_char() {
    uint8_t scancode;
    while (1) {
        scancode = kbd_pop();
        if (scancode != 0 && !(scancode & 0x80)) {
            if (kbd_us[scancode] != 0) {
                return kbd_us[scancode];
            }
        }
        __asm__ __volatile__("hlt"); 
    }
}


// ------------------------
//          IDE
// ------------------------

void get_ide_device_name(ide_device_t* device, char* buff) { // buff = 14
	buff[0] = 'i';
	buff[1] = 'd';
	buff[2] = 'e';
	char buf[11];
	uint32_to_dec(device->id, buf);
	for (int i = 0; i < 11; i++) {
		buff[3+i] = buf[i];
		if (buf[i] == 0) {
			break;
		}
	}
}

void get_volume_name(volume_t* v, char* buff) { // buff = 12
	buff[0] = 'v';
	char buf[11];
	uint32_to_dec(v->id, buf);
	for (int i = 0; i < 11; i++) {
		buff[1+i] = buf[i];
		if (buf[i] == 0) {
			break;
		}
	}
}

int ide_identify(ide_device_t dev) {
    outb(dev.io_base + 6, dev.drive_select);
    
    outb(dev.io_base + 2, 0);
    outb(dev.io_base + 3, 0);
    outb(dev.io_base + 4, 0);
    outb(dev.io_base + 5, 0);
    
    outb(dev.io_base + 7, 0xEC);

    uint8_t status = inb(dev.io_base + 7);
    if (status == 0) return 0;

    while (inb(dev.io_base + 7) & 0x80);

    if (inb(dev.io_base + 4) != 0 || inb(dev.io_base + 5) != 0) return 0;

    while (!(inb(dev.io_base + 7) & 0x09));
    
    if (inb(dev.io_base + 7) & 0x01) return 0;

    for (int i = 0; i < 256; i++) {
        inw(dev.io_base);
    }

    return 1; 
}

void ide_wait_ready(ide_device_t* dev) {
    while (inb(dev->io_base + 7) & 0x80);
}

int ide_wait_drq(ide_device_t* dev) {
    while (1) {
        uint8_t status = inb(dev->io_base + 7);
        if (!(status & 0x80) && (status & 0x08)) return 1; // Готов отдавать
        if (status & 0x01) return 0; // Ошибка (ERR)
    }
}

void ide_read_sectors(ide_device_t* dev, uint32_t lba, uint8_t count, uint8_t* buffer) {
    ide_wait_ready(dev);
    
	uint8_t select = dev->drive_select | 0x40 | ((lba >> 24) & 0x0F);
    outb(dev->io_base + 6, select);

    for(int i = 0; i < 4; i++) inb(dev->io_base + 7);
	
    outb(dev->io_base + 2, count);
    outb(dev->io_base + 3, (uint8_t)lba);
    outb(dev->io_base + 4, (uint8_t)(lba >> 8));
    outb(dev->io_base + 5, (uint8_t)(lba >> 16));
    outb(dev->io_base + 7, 0x20);

    for (int s = 0; s < count; s++) {
        ide_wait_drq(dev);
        insw(dev->io_base, buffer + (s * 512), 256);
    }
}

void ide_read_sector(ide_device_t* dev, uint32_t lba, uint8_t* buffer) {
	ide_read_sectors(dev, lba, 1, buffer);
}


// ----------------------------
//         File System
// ----------------------------

void mount_all_partitions(ide_device_t* dev) {
    uint8_t sector[512];
    
    ide_read_sector(dev, 0, sector);

    for (int i = 0; i < 4; i++) {
        uint32_t entry_offset = 0x1BE + (i * 16);
        uint32_t lba_start = *(uint32_t*)&sector[entry_offset + 8];
        uint32_t total_sectors = *(uint32_t*)&sector[entry_offset + 12];

        if (total_sectors == 0) continue;

        uint8_t vbr[512];
        ide_read_sector(dev, lba_start, vbr);
        struct fat32_bpb* bpb = (struct fat32_bpb*)vbr;

        if (vbr[0x52] == 'F' && vbr[0x53] == 'A' && vbr[0x54] == 'T') {
            if (volume_count < MAX_VOLUMES) {
                volume_t* v = &mounted_volumes[volume_count];
                v->device = *dev;
                v->partition_lba = lba_start;
                v->root_cluster = bpb->root_clus;
                v->sec_per_clus = bpb->sec_per_clus;
                v->fat_lba = lba_start + bpb->reserved_sec_cnt;
                v->data_lba = v->fat_lba + (bpb->fat_sz_32 * bpb->num_fats);
                v->active = 1;
				v->id = volume_count;
                
                volume_count++;
                kprint("Mounted volume ");
				char buff[12];
				get_volume_name(v, buff);
				kprint(buff);
				kprint("\n");
            }
        }
    }
}

void storage_init() {
    uint16_t ide_ports[] = { 0x1F0, 0x170 };
    uint8_t drive_types[] = { 0xA0, 0xB0 };
	uint8_t id = 0;

    for (int p = 0; p < 2; p++) {
        for (int d = 0; d < 2; d++) {
            ide_device_t dev;
            dev.io_base = ide_ports[p];
            dev.drive_select = drive_types[d];
			dev.id = id;

            if (ide_identify(dev)) {
                kprint("IDE Device found. Searching for partitions...\n");
                mount_all_partitions(&dev);
				id++;
            }
        }
    }
}

uint32_t cluster_to_lba(volume_t* v, uint32_t cluster) {
    return v->data_lba + (cluster - 2) * v->sec_per_clus;
}

uint32_t get_next_cluster(volume_t* v, uint32_t cluster) {
    uint32_t fat_sector = v->fat_lba + (cluster * 4 / 512);
    uint32_t fat_offset = (cluster * 4) % 512;
    uint8_t buffer[512];
    ide_read_sector(&v->device, fat_sector, buffer);
    return (*(uint32_t*)&buffer[fat_offset]) & 0x0FFFFFFF;
}

void identify_system_volume() {
    aos_dirent_t ldr_info;
    for (int i = 0; i < volume_count; i++) {
        if (fat32_find_in_dir(&mounted_volumes[i], 0, "AOSLDR.BIN", &ldr_info)) {
            system_volume = &mounted_volumes[i];
            return;
        }
    }
}


// --------------------
//        FAT32
// --------------------

void fat32_entry_to_aos_entry(struct fat32_dir_entry* raw, aos_dirent_t* out) {
    kernel_memset(out->name, 0, 256);
	kernel_memcpy(out->name, raw->name, 11);

    out->cluster = ((uint32_t)raw->cluster_high << 16) | (uint32_t)raw->cluster_low;
    
    out->size = raw->file_size;
    out->attr = raw->attr;

    out->is_directory = (raw->attr & 0x10) ? 1 : 0;
    out->is_system    = (raw->attr & 0x04) ? 1 : 0;
    out->is_readonly  = (raw->attr & 0x01) ? 1 : 0;

    out->write_date   = raw->wrt_date;
	out->write_time   = raw->wrt_time;
}

void fat32_collect_lfn_chars(struct fat32_lfn_entry* lfn, char* lfn_buffer) {
    int order = lfn->order & 0x1F;
    
    // 1. Защита от некорректных значений order (0 или >20, что больше 255 символов)
    if (order < 1 || order > 20) return; 

    int index = (order - 1) * 13;
    
    // 2. Дополнительная защита от переполнения буфера
    if (index < 0 || index + 13 > 255) return;

    for (int i = 0; i < 5; i++)  lfn_buffer[index++] = (char)lfn->name1[i];
    for (int i = 0; i < 6; i++)  lfn_buffer[index++] = (char)lfn->name2[i];
    for (int i = 0; i < 2; i++)  lfn_buffer[index++] = (char)lfn->name3[i];
}

void far32_format_sfn(char* dest, const char* sfn_name) {
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
    
    // 4. Обязательно ставим нулевой символ в конце
    dest[p] = '\0';
}

unsigned char fat32_checksum(unsigned char *pName) {
    unsigned char sum = 0;
    for (int i = 11; i; i--) {
        sum = ((sum & 1) << 7) + (sum >> 1) + *pName++;
    }
    return sum;
}

aos_dirent_t* fat32_read_dir(volume_t* v, aos_dirent_t* dir_entry, int* out_count) {
	char lfn_temp[256];
	uint8_t lfn_checksum = 0;
    uint8_t buffer[512];
    uint32_t start_cluster = (dir_entry == 0) ? v->root_cluster : dir_entry->cluster;
    uint32_t current_cluster = start_cluster;
    int total_found = 0;

    while (current_cluster < 0x0FFFFFF8 && current_cluster >= 2) {
        uint32_t lba = cluster_to_lba(v, current_cluster);
        for (uint32_t s = 0; s < v->sec_per_clus; s++) {
            ide_read_sector(&v->device, lba + s, buffer);
            struct fat32_dir_entry* dir = (struct fat32_dir_entry*)buffer;
            for (int i = 0; i < 16; i++) {
                if (dir[i].name[0] == 0x00) goto counting_done; // Конец списка
                if (dir[i].name[0] == 0xE5 || dir[i].attr == 0x0F) continue;
                total_found++;
            }
        }
        current_cluster = get_next_cluster(v, current_cluster);
    }

counting_done:
    if (total_found == 0) {
        *out_count = 0;
        return 0;
    }

    aos_dirent_t* result_array = (aos_dirent_t*)kernel_malloc(sizeof(aos_dirent_t) * total_found);
    if (!result_array) return 0;

    current_cluster = start_cluster;
    int current_index = 0;

    while (current_cluster < 0x0FFFFFF8 && current_cluster >= 2) {
        uint32_t lba = cluster_to_lba(v, current_cluster);
        for (uint32_t s = 0; s < v->sec_per_clus; s++) {
            ide_read_sector(&v->device, lba + s, buffer);
            struct fat32_dir_entry* dir = (struct fat32_dir_entry*)buffer;
            for (int i = 0; i < 16; i++) {
                if (dir[i].name[0] == 0x00) {
                    *out_count = current_index;
                    return result_array;
                }
				if (dir[i].attr == 0x0F) {
					struct fat32_lfn_entry* lfn = (struct fat32_lfn_entry*)&dir[i];
    
					// ПУНКТ 4: Если это первая встреченная запись LFN (флаг 0x40), 
					// сбрасываем буфер и запоминаем чексумму
					if (lfn->order & 0x40) {
						kernel_memset(lfn_temp, 0, 256);
						lfn_checksum = lfn->checksum;
					}
					
					// ПУНКТ 2: Игнорируем запись, если её чексумма не совпадает с началом цепочки LFN
					if (lfn->checksum == lfn_checksum) {
						fat32_collect_lfn_chars(lfn, lfn_temp);
					}
					continue;
				}
                if (dir[i].name[0] == 0xE5) {
					kernel_memset(lfn_temp, 0, 256);
					continue;
				}

                fat32_entry_to_aos_entry(&dir[i], &result_array[current_index]);
				
				// ПУНКТ 2: Перед тем как принять LFN, сверяем его чексумму с SFN (коротким именем)
				if (lfn_temp[0] != 0) {
					if (fat32_checksum((unsigned char*)dir[i].name) == lfn_checksum) {
						kernel_memcpy(result_array[current_index].name, lfn_temp, 256);
					}
					kernel_memset(lfn_temp, 0, 256);
				} else {
					far32_format_sfn(result_array[current_index].name, dir[i].name);
				}
                current_index++;
            }
        }
        current_cluster = get_next_cluster(v, current_cluster);
    }

    *out_count = current_index;
    return result_array;
}

int fat32_find_in_dir(volume_t* v, aos_dirent_t* dir_entry, const char* search_name, aos_dirent_t* result) {
    if (dir_entry != 0 && !dir_entry->is_directory) return 0;
	char name[256];
	kernel_memcpy(name, search_name, 255);
	name[255] = 0;
	kernel_to_upper(name);

    int count = 0;
    aos_dirent_t* file_list = fat32_read_dir(v, dir_entry, &count);

    if (file_list == 0) return 0;

    int found = 0;
    for (int i = 0; i < count; i++) {
        if (kernel_strcmp(file_list[i].name, name) == 0) {
            *result = file_list[i];
            found = 1;
            break; 
        }
    }

    kernel_free(file_list);

    return found;
}

void* fat32_read_file(volume_t* v, aos_dirent_t* file, uint32_t* out_size) {
    if (file == 0 || file->is_directory) return 0;

    uint8_t* destination = (uint8_t*)kernel_malloc(file->size);
    
    if (destination == 0) {
        kernel_error(0x5, 0, 0, 0, 0);
    }

    if (out_size != 0) {
        *out_size = file->size;
    }

    uint32_t cluster = file->cluster;
    uint32_t bytes_left = file->size;
    uint32_t offset = 0;
    uint8_t temp_sector[512];

    while (bytes_left > 0 && cluster < 0x0FFFFFF8 && cluster >= 2) {
        uint32_t lba = cluster_to_lba(v, cluster);
        uint32_t cluster_size = v->sec_per_clus * 512;

        if (bytes_left >= cluster_size) {
            ide_read_sectors(&v->device, lba, v->sec_per_clus, destination + offset);
            offset += cluster_size;
            bytes_left -= cluster_size;
        } 
        else {
            for (uint32_t i = 0; i < v->sec_per_clus && bytes_left > 0; i++) {
                if (bytes_left >= 512) {
                    ide_read_sector(&v->device, lba + i, destination + offset);
                    offset += 512;
                    bytes_left -= 512;
                } else {
                    ide_read_sector(&v->device, lba + i, temp_sector);
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


// -----------------------------
//           Paging
// -----------------------------

void load_page_directory(uint32_t* directory) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(directory) : "memory");
}

// Установить бит (пометить фрейм как занятый)
void mmap_set(int bit) {
    bitmap[bit / 32] |= (1 << (bit % 32));
}

// Сбросить бит (пометить фрейм как свободный)
void mmap_unset(int bit) {
    bitmap[bit / 32] &= ~(1 << (bit % 32));
}

// Проверить бит (1 - занят, 0 - свободен)
int mmap_test(int bit) {
    return bitmap[bit / 32] & (1 << (bit % 32));
}

int mmap_first_free() {
    // Проходим по каждому 32-битному числу в массиве
    for (uint32_t i = 0; i < max_blocks / 32; i++) {
        if (bitmap[i] != 0xFFFFFFFF) { // Если число не состоит целиком из единиц, значит есть свободный бит!
            // Проверяем каждый бит внутри этого числа
            for (int j = 0; j < 32; j++) {
                int bit = 1 << j;
                if (!(bitmap[i] & bit)) {
                    return i * 32 + j; // Возвращаем глобальный индекс фрейма
                }
            }
        }
    }
    return -1; // Память закончилась (Out of Memory)
}

void init_pmm(uint32_t mem_size) {
    // Вычисляем количество блоков на основе размера памяти (mem_size берем из Multiboot info)
    max_blocks = mem_size / BLOCK_SIZE;
    used_blocks = 0; // Изначально помечаем всё как "занято" для безопасности (или наоборот, 0)
    
    // Обнуляем карту (всё свободно)
    kernel_memset(bitmap, 0, sizeof(bitmap));
}

// Выделить 1 страницу физической памяти
uint32_t pmm_alloc_block() {
    if (max_blocks <= used_blocks) return 0; // Нет памяти

    int frame = mmap_first_free();
    if (frame == -1) return 0; // Ошибка

    mmap_set(frame); // Помечаем занятым
    used_blocks++;

    // Превращаем индекс в физический адрес
    uint32_t addr = frame * BLOCK_SIZE;
    return addr;
}

// Освободить страницу
void pmm_free_block(uint32_t p_addr) {
    int frame = p_addr / BLOCK_SIZE;
    mmap_unset(frame);
    used_blocks--;
}

void pmm_init_region(uint32_t base, uint32_t size) {
    int align = base / BLOCK_SIZE;
    int blocks = size / BLOCK_SIZE;

    for (; blocks > 0; blocks--) {
        mmap_unset(align++); // Помечаем как СВОБОДНЫЕ (или наоборот, логика зависит от init)
        used_blocks--;
    }
    // Обычно делают наоборот: сначала memset(0), а потом циклом "занимают" (set) регион ядра
}

void pmm_deinit_region(uint32_t base, uint32_t size) {
    int align = base / BLOCK_SIZE;
    int blocks = size / BLOCK_SIZE;

    for (; blocks > 0; blocks--) {
        mmap_set(align++); // Помечаем как ЗАНЯТЫЕ
        used_blocks++;
    }
}

void vmm_map_page(uint32_t phys, uint32_t virt, int flags) {
    // Получаем указатель на запись таблицы
    uint32_t* pte = vmm_get_page(virt);
    
    if (!pte) {
        // Паника: не удалось выделить память под таблицу страниц
        return; 
    }

    // Записываем физический адрес и флаги
    // Если страница уже была, мы её перезапишем
    *pte = (phys & PTE_FRAME) | flags;

    // Сбрасываем TLB, чтобы процессор увидел изменения
    asm volatile("invlpg (%0)" :: "r" (virt) : "memory");
}

void vmm_unmap_page(uint32_t virt) {
    uint32_t* pte = vmm_get_page(virt);
    
    if (pte) {
        // Просто обнуляем запись (убираем бит Present)
        // Физический фрейм нужно освободить через pmm_free_block, если он вам больше не нужен
        uint32_t phys = *pte & PTE_FRAME;
        if (phys) pmm_free_block(phys); // Вернуть фрейм в PMM

        *pte = 0;
        asm volatile("invlpg (%0)" :: "r" (virt) : "memory");
    }
}

uint32_t* vmm_get_page(uint32_t virt) {
    uint32_t pd_idx = virt >> 22;
    uint32_t pt_idx = (virt >> 12) & 0x3FF;

    uint32_t* pd = (uint32_t*)PAGEDIRECTORY_BASE;

    // Проверяем, существует ли таблица страниц
    if (!(pd[pd_idx] & PAGE_PRESENT)) {
        // Если нет — выделяем физ. блок и мапим его
        uint32_t new_pt_phys = pmm_alloc_block();
        pd[pd_idx] = new_pt_phys | PAGE_PRESENT | PAGE_RW | PAGE_USER;
        
        // Сбрасываем TLB для этой области таблиц страниц
        uint32_t* pt_virt_addr = (uint32_t*)(PAGETABLES_BASE + (pd_idx * 4096));
        kernel_memset(pt_virt_addr, 0, 4096);
    }

    // Возвращаем виртуальный адрес нужной записи в таблице страниц
    uint32_t* table = (uint32_t*)(PAGETABLES_BASE + (pd_idx * 4096));
    return &table[pt_idx];
}


// -----------------------
//         Process
// -----------------------

uint32_t get_current_pd() {
    uint32_t pd;
    asm volatile("mov %%cr3, %0" : "=r"(pd));
    return pd;
}

void set_current_pd(uint32_t phys_addr) {
    asm volatile("mov %0, %%cr3" :: "r"(phys_addr));
}

void* temp_map(uint32_t phys_addr) {
    // 1. Находим запись в таблице страниц для нашего TEMP_PAGE_VIRT.
    // Используем функцию, которую мы писали в прошлом шаге (vmm_get_page).
    // Она через магию рекурсии найдет нужный PTE.
    uint32_t* pte = vmm_get_page(TEMP_PAGE_VIRT);

    // 2. Направляем этот PTE на нужный нам физический адрес
    *pte = (phys_addr & 0xFFFFF000) | PAGE_PRESENT | PAGE_RW;

    // 3. ОБЯЗАТЕЛЬНО сбрасываем TLB для этого адреса, 
    // иначе процессор будет думать, что там всё еще старые данные
    asm volatile("invlpg (%0)" :: "r"(TEMP_PAGE_VIRT) : "memory");

    // 4. Теперь чтение/запись по TEMP_PAGE_VIRT будет менять данные в phys_addr
    return (void*)TEMP_PAGE_VIRT;
}

void temp_unmap() {
    uint32_t* pte = vmm_get_page(TEMP_PAGE_VIRT);
    *pte = 0;
    asm volatile("invlpg (%0)" :: "r"(TEMP_PAGE_VIRT) : "memory");
}

process_t* create_process(const char* name) {
    process_t* new_proc = (process_t*)kernel_malloc(sizeof(process_t));
    kernel_memset(new_proc, 0, sizeof(process_t));

    // Заполняем поля "на будущее"
    static uint32_t next_pid = 1;
    new_proc->id = next_pid++;
    if (name) kernel_memcpy(new_proc->name, name, 31);
    new_proc->state = PROC_STATE_READY;

    // Работа с памятью
    uint32_t pd_phys = pmm_alloc_block();
    new_proc->page_directory = (uint32_t*)pd_phys;

    uint32_t* pd_virt = (uint32_t*)temp_map(pd_phys); 
	
	kernel_memset(pd_virt, 0, 4096);

    //uint32_t kernel_pt_phys = page_directory[768] & 0xFFFFF000;
    
    //pd_virt[768] = kernel_pt_phys | PAGE_PRESENT | PAGE_RW; 

    // Recursive Mapping (чтобы процесс мог сам менять свои таблицы через 0xFFC00000)
    pd_virt[1023] = pd_phys | PAGE_PRESENT | PAGE_RW;
	
    temp_unmap();
    return new_proc;
}

void process_map_memory(process_t* proc, uint32_t virt, uint32_t size) {
    uint32_t old_pd = get_current_pd(); // Сохраняем текущий CR3
    
    set_current_pd((uint32_t)proc->page_directory); // Переключаемся на PD процесса

    for (uint32_t i = 0; i < size; i += 4096) {
        uint32_t phys = pmm_alloc_block();
        // Мапим с флагом USER, чтобы программа могла туда писать
        vmm_map_page(phys, virt + i, PAGE_PRESENT | PAGE_RW | PAGE_USER);
    }

    set_current_pd(old_pd); // Возвращаемся обратно
}

void map_to_other_pd(uint32_t* pd_phys, uint32_t phys, uint32_t virt, int flags) {
    uint32_t pd_idx = virt >> 22;
    uint32_t pt_idx = (virt >> 12) & 0x3FF;

    // Мапим чужой каталог страниц, чтобы отредактировать его
    uint32_t* pd_virt = (uint32_t*)temp_map((uint32_t)pd_phys);
    
    if (!(pd_virt[pd_idx] & PAGE_PRESENT)) {
        uint32_t new_pt_phys = pmm_alloc_block();
        pd_virt[pd_idx] = new_pt_phys | flags;
        
        // Обнуляем новую таблицу страниц
        uint32_t* pt_virt = (uint32_t*)temp_map(new_pt_phys);
        kernel_memset(pt_virt, 0, 4096);
        temp_unmap();
        
        // Снова мапим PD, так как temp_map перетер старый маппинг
        pd_virt = (uint32_t*)temp_map((uint32_t)pd_phys);
    }

    uint32_t pt_phys = pd_virt[pd_idx] & 0xFFFFF000;
    temp_unmap();

    // Мапим таблицу страниц и вставляем запись
    uint32_t* pt_virt = (uint32_t*)temp_map(pt_phys);
    pt_virt[pt_idx] = (phys & 0xFFFFF000) | flags;
    temp_unmap();
}

// ------------------
//       Other
// ------------------

void isr_handler(registers_t *r) {
    if (r->int_no == 14){
		uint32_t faulting_address;
		__asm__ volatile("mov %%cr2, %0" : "=r" (faulting_address));

		// Если же адрес реально мусорный (выше RAM или 0x0) - тогда падаем
		kprint("PAGE FAULT! Reason: ");
        if (!(r->err_code & 0x1)) kprint("NOT_PRESENT ");
        if (r->err_code & 0x2)    kprint("WRITE_VIOLATION ");
        if (r->err_code & 0x4)    kprint("USER_MODE ");
        if (r->err_code & 0x10)   kprint("INSTRUCTION_FETCH ");
		kernel_error(0x7, r->int_no, faulting_address, 0, 0);
	} else if (r->int_no == 6) {
		uint32_t faulting_address = r->rip;
		kernel_error(0x7, r->int_no, faulting_address, r->err_code, 0);
    } else if (r->int_no < 32) {
		uint32_t faulting_address;
        __asm__ volatile("mov %%cr2, %0" : "=r" (faulting_address));
		kernel_error(0x7, r->int_no, faulting_address, r->err_code, 0);
    }
	else if (r->int_no == 33) {
        uint8_t scancode = inb(0x60);
        kbd_push(scancode);
        outb(0x20, 0x20);
    }
	else if (r->int_no == 32) {
        static int ticks = 0;
        ticks++;
		if (r->int_no >= 40) outb(0xA0, 0x20);
        outb(0x20, 0x20);
    }
}

void __stack_chk_fail(void) {
	uint32_t addr = (uint32_t)__builtin_return_address(0);
    kernel_error(0x1, addr, 0, 0, 0);
}

uint32_t detect_memory_size() {
    uint32_t count = 0;
    for (uint32_t addr = 0x100000; addr < 0xFFFFFFFF; addr += 0x100000) {
        volatile uint32_t* mem = (uint32_t*)addr;
        uint32_t old_val = *mem;
        
        *mem = 0xABCDEF12;
        if (*mem != 0xABCDEF12) {
            break;
        }
        
        *mem = old_val;
        count = addr + 0x100000;
    }
    return count;
}

void breakpoint(){
	kprint("Breakpoint :-)");
	while(1);
}

void pausepoint(){
	kprint("Pausepoint. Press any key to continue :3\n");
	kbd_get_char();
}

void reset_state() {
	state.system_flags = CAN_REGISTER_KERNEL_DRIVERS | CAN_PRINT;
}


// ----------------------
//         Export
// ----------------------
void register_symbol(const char* name, uint32_t address) {
    // 1. Проверяем, не зарегистрирован ли уже такой символ
    symbol_t* curr = global_symbol_table;
    while (curr) {
        if (kernel_strcmp(curr->name, name) == 0) {
            kprint("Warning: Symbol already registered: ");
            kprint(name);
            kprint("\n");
            return;
        }
        curr = curr->next;
    }

    // 2. Аллоцируем память под новую запись (используйте вашу kmalloc)
    symbol_t* new_sym = (symbol_t*)kernel_malloc(sizeof(symbol_t));
    if (!new_sym) return;

    kernel_strncpy(new_sym->name, name, 15);
    new_sym->name[15] = '\0';
    new_sym->address = address;

    // 3. Вставляем в начало списка
    new_sym->next = global_symbol_table;
    global_symbol_table = new_sym;

    kprint("Symbol registered: ");
    kprint(name);
    kprint("\n");
}

uint32_t find_kernel_symbol(const char* name) {
    symbol_t* curr = global_symbol_table;
    while (curr) {
        if (kernel_strcmp(curr->name, name) == 0) {
            return curr->address;
        }
        curr = curr->next;
    }

    return 0; // Не найдено
}

// ---------------------
//         ELF
// ---------------------


void load_elf(volume_t* v, aos_dirent_t* file, elf_load_result_t* result) {
    uint32_t file_size = 0;
    
    // Читаем весь файл в буфер
    uint8_t* raw_data = (uint8_t*)fat32_read_file(v, file, &file_size);
    if (!raw_data) {
        result->result = ELF_RESULT_INVALID;
        return;
    }

    Elf32_Ehdr* hdr = (Elf32_Ehdr*)raw_data;

    // Проверка сигнатуры ELF (\x7F E L F)
    if (hdr->e_ident[0] != 0x7F || hdr->e_ident[1] != 'E' || 
        hdr->e_ident[2] != 'L' || hdr->e_ident[3] != 'F') {
        kernel_free(raw_data);
        result->result = ELF_RESULT_INVALID;
        return;
    }

    result->entry_point = hdr->e_entry;

    // Подготовка процесса / PD
    process_t* proc = NULL;
    uint32_t old_pd = get_current_pd();

	proc = create_process(file->name);
	result->proc = proc;

    // Загрузка сегментов (Program Headers)
    Elf32_Phdr* phdr = (Elf32_Phdr*)(raw_data + hdr->e_phoff);

    for (int i = 0; i < hdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD) {
            uint32_t vaddr = phdr[i].p_vaddr;
            uint32_t memsz = phdr[i].p_memsz;
            uint32_t filesz = phdr[i].p_filesz;
            uint32_t offset = phdr[i].p_offset;

            // Выравнивание по границе страниц для начала маппинга
            uint32_t start_page = vaddr & 0xFFFFF000;
            uint32_t end_page = (vaddr + memsz + 4095) & 0xFFFFF000;
            uint32_t page_count = (end_page - start_page) / 4096;

            for (uint32_t p = 0; p < page_count; p++) {
                uint32_t curr_virt = start_page + (p * 4096);
                
                // Проверяем, замаплена ли уже страница (сегменты могут пересекаться по страницам)
                // Для упрощения просто аллоцируем, если нужно (vmm_map_page перезапишет, если что)
                uint32_t phys = pmm_alloc_block();
                
				map_to_other_pd(proc->page_directory, phys, curr_virt, PAGE_PRESENT | PAGE_RW | PAGE_USER);
				
				// Очистка через временный маппинг
				void* ptr = temp_map(phys);
				kernel_memset(ptr, 0, 4096);
				uint32_t file_data_start = vaddr;
				uint32_t file_data_end = vaddr + filesz;
				
				uint32_t page_start = curr_virt;
				uint32_t page_end = curr_virt + 4096;
				
				// Ищем пересечение отрезков [file_data] и [page]
				uint32_t copy_start = (file_data_start > page_start) ? file_data_start : page_start;
				uint32_t copy_end   = (file_data_end < page_end)     ? file_data_end   : page_end;
				
				if (copy_start < copy_end) {
					// Данные есть, копируем нужный кусок
					uint32_t bytes_to_copy = copy_end - copy_start;
					uint32_t offset_in_page = copy_start - page_start;  // Смещение внутри kptr
					uint32_t offset_in_file = copy_start - vaddr;       // Смещение от начала сегмента данных
					
					// raw_data + offset — начало сегмента в буфере с файлом
					kernel_memcpy(
						(uint8_t*)ptr + offset_in_page, 
						raw_data + offset + offset_in_file, 
						bytes_to_copy
					);
				}
				
				// 6. Убираем временный маппинг
				temp_unmap();
			}
        }
    }

    // Обработка символов (Exports)
    // Ищем таблицу строк и таблицу символов
    Elf32_Shdr* shdr = (Elf32_Shdr*)(raw_data + hdr->e_shoff);
    char* strtab = NULL;
    Elf32_Sym* symtab = NULL;
    uint32_t sym_count = 0;

    // Сначала найдем String Table
    for (int i = 0; i < hdr->e_shnum; i++) {
        if (shdr[i].sh_type == SHT_STRTAB && i != hdr->e_shstrndx) {
             // Обычно последняя strtab - это то, что нужно для symtab, 
             // но лучше проверять sh_link у symtab
             strtab = (char*)(raw_data + shdr[i].sh_offset);
        }
    }

    // Теперь ищем символы
    for (int i = 0; i < hdr->e_shnum; i++) {
        if (shdr[i].sh_type == SHT_SYMTAB) {
            symtab = (Elf32_Sym*)(raw_data + shdr[i].sh_offset);
            sym_count = shdr[i].sh_size / sizeof(Elf32_Sym);
            
            // Если есть специфичная strtab для этой секции
            if (shdr[i].sh_link != 0) {
                strtab = (char*)(raw_data + shdr[shdr[i].sh_link].sh_offset);
            }
            break;
        }
    }

    if (symtab && strtab) {
        for (uint32_t i = 0; i < sym_count; i++) {
            // STB_GLOBAL = 1, STT_FUNC = 2 или STT_OBJECT = 1
            // st_info >> 4 == bind, st_info & 0xF == type
            uint8_t bind = symtab[i].st_info >> 4;
            // uint8_t type = symtab[i].st_info & 0xF;

            if (bind == 1 && symtab[i].st_name != 0 && symtab[i].st_value != 0) {
                char* name = strtab + symtab[i].st_name;
                // Регистрируем глобальные символы
                register_symbol(name, symtab[i].st_value);
            }
        }
    }

    kernel_free(raw_data);
    result->result = ELF_RESULT_OK;
}

void start_elf_process(elf_load_result_t* res) {
	uint32_t old_pd = get_current_pd();
	set_current_pd((uint32_t)res->proc->page_directory);

	// 1. Выделяем стек (например, 0xBFFFF000)
	// В ELF стек не описывается, выделяем вручную
	uint32_t stack_phys = pmm_alloc_block();
	uint32_t stack_virt = 0xBFFFF000;
	vmm_map_page(stack_phys, stack_virt, PAGE_PRESENT | PAGE_RW | PAGE_USER);

	uint32_t user_esp = stack_virt + 4096;
	
	asm volatile (
		"mov %0, %%ds\n"
		"mov %0, %%es\n"
		"mov %0, %%fs\n"
		"mov %0, %%gs\n"
		
		"pushl %0\n"          // SS (User Data)
		"pushl %1\n"          // ESP
		"pushf\n"             // EFLAGS
		"popl %%eax\n"
		"orl $0x200, %%eax\n" // Включаем прерывания (IF = 1)
		"pushl %%eax\n"
		"pushl %2\n"          // CS (User Code)
		"pushl %3\n"          // EIP (Entry Point)
		"iret\n"
		: 
		: "r"(GDT_USER_DATA), "r"(user_esp), "r"(GDT_USER_CODE), "r"(res->entry_point)
		: "ax", "memory"
	);
	
	set_current_pd(old_pd);
}


// ---------------------------------------------------------
//                     KERNEL MAIN
// ---------------------------------------------------------

void kernel_main(uint64_t boot_drive_id) {
	while(1);
	kclear();
    kprint("AOSLDR, hello from protected mode...\n");
	char buff[11];
	mem = detect_memory_size();
	uint32_to_dec(mem / 1024 / 1024, buff);
	kprint("Total RAM: ");
	kprint(buff);
	kprint(" MB\n");
	init_pmm(mem);
	pmm_deinit_region(0, 0x400000);
	kprint("Paging enabled!\n");
	idt_install();
	pic_remap();
	timer_init(100);
	outb(0x21, 0xFC);
    outb(0xA1, 0xFF);
	kprint("IDT is set! We're safe\n");
	idt_set_gate(0x80, (uint32_t)syscall_stub, 0x8, 0xEE);
	__asm__ __volatile__("sti");
	reset_state();
	storage_init();
	kprint("Storage initialized...\n");
	identify_system_volume();
	if (volume_count == 0 || system_volume == 0) {
		kernel_error(0x2, 0, 0, 0, 0);
	}
	kprint("System volume is ");
	get_volume_name(system_volume, buff);
	kprint(buff);
	kprint("\n");
	
	int entries = 0;
	aos_dirent_t* files = fat32_read_dir(system_volume, 0, &entries);
	for (int i = 0; i < entries; i++) {
		kprint(" - ");
		kprint(files[i].name);
		if (files[i].is_directory == 1) {
			kprint(" [D]");
		}
		kprint("\n");
	}
	kprint("Load drivers...\n");
	aos_dirent_t file, drivers_dir;
	if (!fat32_find_in_dir(system_volume, 0, "DRIVERS", &drivers_dir)){
		kernel_error(0x4, system_volume->id, 0, 0, 0);
	}
	kprint("Drivers folder found!\n");
	files = fat32_read_dir(system_volume, &drivers_dir, &entries);
	for (int i = 0; i < entries; i++) {
		kprint(" - ");
		kprint(files[i].name);
		if (files[i].is_directory == 1) {
			kprint(" [D]");
		}
		kprint("\n");
	}
	kprint("VFS driver..\n");
	if (!fat32_find_in_dir(system_volume, &drivers_dir, "VFSDRIVER.ELF", &file)){
		kernel_error(0x4, system_volume->id, drivers_dir.cluster, 0, 0);
	}
	kprint("Found! Start driver...\n");
	elf_load_result_t* driver = (elf_load_result_t*)kernel_malloc(sizeof(elf_load_result_t));
	kernel_memset(driver, 0, sizeof(elf_load_result_t));
	load_elf(system_volume, &file, driver);
	if (driver->result != ELF_RESULT_OK) kernel_error(0x6, 0, driver->result, driver->entry_point, 0);
    start_elf_process(driver);
	
    while(1) {
        char c = kbd_get_char();
        kprint_char(c, 0x0D);
    }
}