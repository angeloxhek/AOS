#include "include/aosldr.h"
#include "include/fonts.h"

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

extern uint32_t boot_ver;
extern void syscall_entry(void);
extern void switch_to_task(thread_t* current, thread_t* next);
extern void trampoline_enter_user();
extern void trampoline_enter_kernel();

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
	"MALLOC_ERROR",
	"DRIVER_ERROR",
	"ISR_ERROR",
	"BOOT_INFO_INVALID"
};

#define BLOCK_SIZE 4096
// Статический массив для примера. 
// 32768 элементов * 64 бита * 4096 байт = покрытие 8 ГБ ОЗУ.
// В реальной ОС размер вычисляется динамически при загрузке.
uint64_t* bitmap = 0; 
uint64_t max_blocks = 0;    // Всего блоков
uint64_t used_blocks = 0;   // Сколько занято
uint64_t bitmap_size = 0;

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
#define PHYS_ASM_PDPT   0x81000
#define PHYS_ASM_PD     0x82000
#define PHYS_HHDM_PDPT  0x84000 
#define PHYS_HHDM_PD    0x85000 
uint64_t* pml4_table_virt = 0; 

const unsigned char const kbd_us[128] =
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

struct idt_entry idt[256];
struct idt_ptr   idtp;
struct gdt_entry gdt[7]; 
struct gdt_ptr   gp;
struct tss_entry_t tss;

ide_device_t* system_ide = 0;
ide_device_t mounted_ides[MAX_VOLUMES];
int ide_count = 0;
volume_t* system_volume = 0;
volume_t mounted_volumes[MAX_VOLUMES];
int volume_count = 0;

#define HEAP_START_ADDR 0xFFFF800040000000
malloc_header_t* free_list_start = (malloc_header_t*)HEAP_START_ADDR;
uint64_t heap_current_limit = HEAP_START_ADDR;
int malloc_initialized = 0;

__attribute__((aligned(16)))
uint8_t kernel_stack[16384]; 

__attribute__((aligned(16))) 
kernel_tcb_t kernel_tcb = {
    .canary = 0xDEADBEEF 
};

#define TEMP_PAGE_VIRT 0xFFFFFFFFFFE00000

#define MSR_EFER    0xC0000080
#define MSR_STAR    0xC0000081
#define MSR_LSTAR   0xC0000082
#define MSR_SFMASK  0xC0000084

#define KERNEL_CODE_SEG 0x08
#define USER_DATA_SEG   0x18 
#define USER_SEG_BASE   0x10

#define KERNEL_STACK_SIZE 4096
process_t kernel_process;

thread_t* current_thread;
thread_t* ready_queue;
uint64_t thread_count = 0;

spinlock_t kprint_lock = 0;
spinlock_t heap_lock = 0;
volatile uint64_t ticks = 0;

driver_info_t* drivers_list_head;
uint64_t keyboard_driver_tid = 0;

thread_t* zombies_list = 0;

uint8_t default_fpu_state[512] __attribute__((aligned(16)));

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

void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t low = value & 0xFFFFFFFF;
    uint32_t high = value >> 32;
    asm volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

uint64_t rdmsr(uint32_t msr) {
    uint32_t low, high;
    asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}


// -------------------------
//     Print Functions
// -------------------------

void put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x >= video->width || y >= video->height) return;
    // offset = (y * pitch) + (x * (bpp / 8))
    uint64_t offset = (y * video->pitch) + (x * (video->bpp / 8));
    uint32_t* pixel = (uint32_t*)(video->framebuffer_addr + offset);
    *pixel = color;
}

void _kclear() {
    uint32_t* fb = (uint32_t*)video->framebuffer_addr;
    for (uint32_t y = 0; y < video->height; y++) {
        uint32_t* row = (uint32_t*)(video->framebuffer_addr + y * video->pitch);
        for (uint32_t x = 0; x < video->width; x++) {
             row[x] = bg_color;
        }
    }

	if (!(state.system_flags & CAN_PRINT)) return;
    cursor_x = 0;
    cursor_y = 0;
}

void kclear() {
    if (!(state.system_flags & CAN_PRINT)) return;
	uint64_t irq_state = spinlock_irq_save();
    spinlock_acquire(&kprint_lock);
	_kclear();
	spinlock_release(&kprint_lock);
    spinlock_irq_restore(irq_state);
}

void kprint_scroll() {
    if (!(state.system_flags & CAN_PRINT)) return;
    uint32_t font_h = 16;
    uint64_t bytes_to_move = (uint64_t)video->pitch * (video->height - font_h);
	uint8_t* fb = (uint8_t*)video->framebuffer_addr;
    kernel_memcpy(fb, fb + (font_h * video->pitch), bytes_to_move);
    uint8_t* bottom_part = fb + bytes_to_move;
    uint64_t bottom_size = (uint64_t)font_h * video->pitch;
    kernel_memset64(bottom_part, ((uint64_t)bg_color << 32) | bg_color, bottom_size);
}

void _kprint_char(int x_pos, int y_pos, char c, uint32_t fg, uint32_t bg) {
    const uint8_t* glyph = (*font)[(unsigned char)c];
    for (int y = 0; y < 16; y++) {
        const uint8_t line = glyph[y];
        for (int x = 0; x < 8; x++) {
            if ((line >> (7 - x)) & 1) {
                put_pixel(x_pos + x, y_pos + y, fg);
            } else {
                put_pixel(x_pos + x, y_pos + y, bg);
            }
        }
    }
}

void kprint_char(char c, uint32_t color) {
    if (!(state.system_flags & CAN_PRINT)) return;
    const int font_w = 8;
    const int font_h = 16;
    int max_cols = video->width / font_w;
    int max_rows = video->height / font_h;
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\r') {
        cursor_x = 0;
    } else {
        _kprint_char(cursor_x * font_w, cursor_y * font_h, c, color, bg_color);
        cursor_x++;
    }
    if (cursor_x >= max_cols) {
        cursor_x = 0;
        cursor_y++;
    }
    if (cursor_y >= max_rows) {
        kprint_scroll();
        cursor_y = max_rows - 1;
    }
}

void _kprint(const char* str) {
    if (!(state.system_flags & CAN_PRINT)) return;
    for (int i = 0; str[i] != 0; i++) {
        kprint_char(str[i], 0x0000FFFF);
    }
}

void kprint(const char* str) {
    if (!(state.system_flags & CAN_PRINT)) return;
	uint64_t irq_state = spinlock_irq_save();
    spinlock_acquire(&kprint_lock);
    _kprint(str);
	spinlock_release(&kprint_lock);
    spinlock_irq_restore(irq_state);
}

void _kprint_error(const char* str) {
    if (!(state.system_flags & CAN_PRINT)) return;
    for (int i = 0; str[i] != 0; i++) {
        kprint_char(str[i], 0x00FF0000);
    }
}

void kprint_error(const char* str) {
    if (!(state.system_flags & CAN_PRINT)) return;
	uint64_t irq_state = spinlock_irq_save();
    spinlock_acquire(&kprint_lock);
    _kprint_error(str);
	spinlock_release(&kprint_lock);
    spinlock_irq_restore(irq_state);
}

void _kprint_error_vga(const char* str) {
    volatile uint16_t* vga_buffer = (volatile uint16_t*)0xB8000;
    for(int i = 0; i < 80 * 25; i++) {
        vga_buffer[i] = 0x1F00;
    }
    int i = 0;
    while(str[i]) {
        vga_buffer[i] = (uint16_t)str[i] | 0x4F00; 
        i++;
    }
}


// ------------------------
//      uint to text
// ------------------------

void uint32_to_hex(uint32_t value, char* out_buffer) { // buff size 9
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

void uint64_to_hex(uint64_t value, char* out_buffer) { // buff size 17
    const char *hex_digits = "0123456789ABCDEF";
    for (int i = 15; i >= 0; i--) {
        out_buffer[i] = hex_digits[value & 0x0F];
        value >>= 4;
    }
    out_buffer[16] = '\0';
}

void uint32_to_dec(uint32_t value, char* out_buffer) { // buff size 11
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

void uint64_to_dec(uint64_t value, char* out_buffer) { // buff size 21
    char temp[21]; 
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
	uint8_t* d2 = dest;
    const uint8_t* s2 = src;
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


// --------------------------
//            PMM
// --------------------------

void mmap_set(uint64_t bit) {
    bitmap[bit / 64] |= (1ULL << (bit % 64));
}

void mmap_unset(uint64_t bit) {
    bitmap[bit / 64] &= ~(1ULL << (bit % 64));
}

int mmap_test(uint64_t bit) {
    return (bitmap[bit / 64] & (1ULL << (bit % 64))) != 0;
}

int64_t mmap_first_free() {
    uint64_t limit = max_blocks / 64;
    for (uint64_t i = 0; i < limit; i++) {
        if (bitmap[i] != 0xFFFFFFFFFFFFFFFF) {
            int bit = __builtin_ctzll(~bitmap[i]);
            return i * 64 + bit;
        }
    }
    return -1;
}

void init_pmm(uint64_t mem_size, uint64_t bitmap_base) {
    max_blocks = mem_size / BLOCK_SIZE;
    used_blocks = max_blocks;
    bitmap = (uint64_t*)bitmap_base;
    bitmap_size = max_blocks / 8; 
    kernel_memset(bitmap, 0xFF, bitmap_size);
}

uint64_t pmm_alloc_block() {
    if (max_blocks <= used_blocks) return 0; // OOM
    int64_t frame = mmap_first_free();
    if (frame == -1) return 0;
    mmap_set(frame);
    used_blocks++;
    uint64_t addr = (uint64_t)frame * BLOCK_SIZE;
    return addr;
}

void pmm_free_block(uint64_t p_addr) {
    uint64_t frame = p_addr / BLOCK_SIZE;
    mmap_unset(frame);
    used_blocks--;
}

// Инициализация региона (пометить диапазон как свободный)
void pmm_init_region(uint64_t base, uint64_t size) {
    uint64_t align = base / BLOCK_SIZE;
    uint64_t blocks = size / BLOCK_SIZE;
    for (; blocks > 0; blocks--) {
        mmap_unset(align++); 
        used_blocks--;
    }
}

// Деинициализация региона (пометить диапазон как занятый/резерв)
void pmm_deinit_region(uint64_t base, uint64_t size) {
    uint64_t align = base / BLOCK_SIZE;
    uint64_t blocks = size / BLOCK_SIZE;
    for (; blocks > 0; blocks--) {
        mmap_set(align++); 
        used_blocks++;
    }
}


// -------------------------
//           VMM
// -------------------------

static inline void invlpg(uint64_t vaddr) {
    asm volatile("invlpg (%0)" :: "r" (vaddr) : "memory");
}

uint64_t* vmm_get_pte(uint64_t virt, int alloc) {
    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;
    if (!(pml4_table_virt[pml4_idx] & PAGE_PRESENT)) {
        if (!alloc) return 0;
        uint64_t new_pdpt_phys = pmm_alloc_block();
        if (!new_pdpt_phys) return 0; // OOM
        pml4_table_virt[pml4_idx] = new_pdpt_phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
        kernel_memset((void*)P2V(new_pdpt_phys), 0, 4096);
    }
    uint64_t pdpt_phys = pml4_table_virt[pml4_idx] & PAGE_FRAME;
    uint64_t* pdpt_virt = (uint64_t*)P2V(pdpt_phys);
    if (!(pdpt_virt[pdpt_idx] & PAGE_PRESENT)) {
        if (!alloc) return 0;
        uint64_t new_pd_phys = pmm_alloc_block();
        if (!new_pd_phys) return 0;
        pdpt_virt[pdpt_idx] = new_pd_phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
        kernel_memset((void*)P2V(new_pd_phys), 0, 4096);
    }
    uint64_t pd_phys = pdpt_virt[pdpt_idx] & PAGE_FRAME;
    uint64_t* pd_virt = (uint64_t*)P2V(pd_phys);
    if (!(pd_virt[pd_idx] & PAGE_PRESENT)) {
        if (!alloc) return 0;
        uint64_t new_pt_phys = pmm_alloc_block();
        if (!new_pt_phys) return 0;
        pd_virt[pd_idx] = new_pt_phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
        kernel_memset((void*)P2V(new_pt_phys), 0, 4096);
    }
    uint64_t pt_phys = pd_virt[pd_idx] & PAGE_FRAME;
    uint64_t* pt_virt = (uint64_t*)P2V(pt_phys);
    return &pt_virt[pt_idx];
}

uint64_t vmm_get_phys_from_pml4(uint64_t* pml4_phys_root, uint64_t virt) {
    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;
    uint64_t* pml4_virt = (uint64_t*)temp_map((uint64_t)pml4_phys_root);
    uint64_t pml4_entry = pml4_virt[pml4_idx];
    if (!(pml4_entry & PAGE_PRESENT)) {
        temp_unmap();
        return 0;
    }
    uint64_t pdpt_phys = pml4_entry & PAGE_FRAME;
    uint64_t* pdpt_virt = (uint64_t*)temp_map(pdpt_phys);
    uint64_t pdpt_entry = pdpt_virt[pdpt_idx];
    if (!(pdpt_entry & PAGE_PRESENT)) {
        temp_unmap();
        return 0;
    }
    if (pdpt_entry & 0x80) { 
        temp_unmap();
        return (pdpt_entry & PAGE_FRAME) + (virt & 0x3FFFFFFF); 
    }
    uint64_t pd_phys = pdpt_entry & PAGE_FRAME;
    uint64_t* pd_virt = (uint64_t*)temp_map(pd_phys);
    uint64_t pd_entry = pd_virt[pd_idx];
    if (!(pd_entry & PAGE_PRESENT)) {
        temp_unmap();
        return 0;
    }
    if (pd_entry & 0x80) { 
        temp_unmap();
        return (pd_entry & PAGE_FRAME) + (virt & 0x1FFFFF); 
    }
    uint64_t pt_phys = pd_entry & PAGE_FRAME;
    uint64_t* pt_virt = (uint64_t*)temp_map(pt_phys);
    uint64_t pt_entry = pt_virt[pt_idx];
    temp_unmap();
    if (!(pt_entry & PAGE_PRESENT)) {
        return 0;
    }
    return pt_entry & PAGE_FRAME;
}

void vmm_map_page(uint64_t phys, uint64_t virt, uint64_t flags) {
    uint64_t* pte = vmm_get_pte(virt, 1);
    if (!pte) {
        kernel_error(0x05, virt, 0, 0, 0);
        return;
    }
    *pte = (phys & PAGE_FRAME) | flags;
    invlpg(virt);
}

void vmm_unmap_page(uint64_t virt) {
    uint64_t* pte = vmm_get_pte(virt, 0);
    if (pte && (*pte & PAGE_PRESENT)) {
        *pte = 0;
        invlpg(virt);
    }
}


// -------------------------
//        PIC & IDT
// -------------------------

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

void idt_set_gate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low  = (base & 0xFFFF);
    idt[num].sel       = sel;
    idt[num].ist       = 0;
    idt[num].flags     = flags;
    idt[num].base_mid  = (base >> 16) & 0xFFFF;
    idt[num].base_high = (base >> 32) & 0xFFFFFFFF;
    idt[num].reserved  = 0;
}

void idt_install() {
    idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
    idtp.base  = (uint64_t)&idt;
    kernel_memset(&idt, 0, sizeof(struct idt_entry) * 256);
    #define X(n) idt_set_gate(n, (uint64_t)isr##n, 0x08, 0x8E);
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

void schedule() {
	thread_t* t = ready_queue;
    do {
        if (t->state == THREAD_BLOCKED && t->wake_up_time > 0) {
            if (ticks >= t->wake_up_time) {
                t->state = THREAD_READY;
                t->wake_up_time = 0;
            }
        }
        t = t->next;
    } while (t != ready_queue);
	
    thread_t* prev = current_thread;
    thread_t* next = current_thread->next;
    while (next->state > 1 && next != prev) {
        next = next->next;
    }
    if (next == prev) return;
    current_thread = next;
	if (prev->state == THREAD_RUNNING) prev->state = THREAD_READY;
    next->state = THREAD_RUNNING;
    tss.rsp0 = next->stack_base + KERNEL_STACK_SIZE;
	kernel_tcb.kernel_rsp = next->stack_base + KERNEL_STACK_SIZE;
	switch_to_task(prev, next);
}

void isr_handler(registers_t *r) {
    if (r->int_no < 32) {
		if (state.system_flags & KERNEL_PANIC) {
			__asm__ volatile("cli; hlt");
			__builtin_unreachable();
		}
		state.system_flags |= KERNEL_PANIC;
		if ((r->cs & 3) != 3) {
			if (r->int_no == 14) {
				uint64_t fault_addr;
				__asm__ volatile("mov %%cr2, %0" : "=r" (fault_addr));
				kernel_error(0x7, r->int_no, fault_addr, r->err_code, r->rip);
			} else if (r->int_no == 6) {
				kernel_error(0x7, r->int_no, r->rip, r->rsp, r->err_code);
			} else if (r->int_no == 13) {
				kernel_error(0x7, r->int_no, r->err_code, r->rip, r->cs);
			}
			kernel_error(0x7, r->int_no, r->rip, r->err_code, 0);
		}
		kprint_error("Process: ");
		kprint_error(current_thread->owner->name);
		kprint_error(" (PID: ");
		char pid_buf[32];
		uint64_to_dec(current_thread->owner->id, pid_buf);
		kprint_error(pid_buf);
		kprint_error(")\n");
		kprint_error("Thread TID: ");
		uint64_to_dec(current_thread->tid, pid_buf);
		kprint_error(pid_buf);
		kprint_error("\n");
		if (r->int_no == 14) {
			uint64_t fault_addr;
			__asm__ volatile("mov %%cr2, %0" : "=r" (fault_addr));
			kernel_error(0x7, r->int_no, fault_addr, r->err_code, r->rip);
		} else if (r->int_no == 6) {
			kernel_error(0x7, r->int_no, r->rip, r->rsp, r->err_code);
		} else if (r->int_no == 13) {
			kernel_error(0x7, r->int_no, r->err_code, r->rip, r->cs);
		}
		kernel_error(0x7, r->int_no, r->rip, r->err_code, 0);
		__builtin_unreachable();
    } else if (r->int_no == 33) {
        uint8_t scancode = inb(0x60);
        if (keyboard_driver_tid != 0) {
            message_t msg;
            msg.sender_tid = 0;
            msg.type = MSG_TYPE_KEYBOARD;
            msg.subtype = MSG_SUBTYPE_SEND;
            msg.param1 = scancode;
            msg.param2 = 0;
            msg.param3 = 0;
            ipc_send(keyboard_driver_tid, &msg);
        } else {
            // Драйвера нет? Можно вывести отладочный символ, чтобы знать, что железо работает.
            // kprint("K"); 
        }
        outb(0x20, 0x20);
    } else if (r->int_no == 32) {
        ticks++;
		outb(0x20, 0x20);
		schedule();
    }
	else if (r->int_no >= 32) {
        if (r->int_no >= 40) outb(0xA0, 0x20);
        outb(0x20, 0x20);
    }
}


// -------------------------
//           GDT
// -------------------------

void gdt_set_gate(int num, uint64_t base, uint64_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;
    gdt[num].limit_low = (limit & 0xFFFF);
    gdt[num].granularity = ((limit >> 16) & 0x0F);
    gdt[num].granularity |= (gran & 0xF0);
    gdt[num].access = access;
}

void write_tss(int32_t num, uint64_t base, uint32_t limit) {
    gdt_set_gate(num, base, limit, 0x89, 0x00);
    gdt[num + 1].limit_low = 0;
    gdt[num + 1].base_low = 0;
    gdt[num + 1].base_middle = 0;
    gdt[num + 1].access = 0;
    gdt[num + 1].granularity = 0;
    uint64_t *high_desc = (uint64_t *)&gdt[num + 1];
    *high_desc = (base >> 32); 
}

void gdt_install() {
    gp.limit = (sizeof(struct gdt_entry) * 7) - 1;
    gp.base  = (uint64_t)&gdt;
    gdt_set_gate(0, 0, 0, 0, 0);
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xAF);
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0x00);
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xF2, 0x00);
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xFA, 0xAF);
	kernel_memset(&tss, 0, sizeof(struct tss_entry_t));
    tss.rsp0 = 0xFFFFFFFF80090000;
    tss.iomap_base = sizeof(struct tss_entry_t);
    write_tss(5, (uint64_t)&tss, sizeof(tss) - 1);
    __asm__ volatile("lgdt (%0)" : : "r"(&gp));
    __asm__ volatile(
        "mov $0x10, %ax \n"
        "mov %ax, %ds \n"
        "mov %ax, %es \n"
        "mov %ax, %fs \n"
        "mov %ax, %gs \n"
        "mov %ax, %ss \n"
    );
	__asm__ volatile("ltr %%ax" :: "a" (0x28));
	wrmsr(0xC0000100, (uint64_t)&kernel_tcb); 
	wrmsr(0xC0000101, (uint64_t)&kernel_tcb); 
	wrmsr(0xC0000102, (uint64_t)&kernel_tcb);
}

void init_syscall() {
	kernel_tcb.kernel_rsp = (uint64_t)(kernel_stack + sizeof(kernel_stack));
    uint64_t efer = rdmsr(MSR_EFER);
    wrmsr(MSR_EFER, efer | 1);
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

void syscall_handler(syscall_regs_t* regs) {
    uint64_t syscall_nr = regs->rax;
    switch (syscall_nr) {
        case SYS_PRINT: {
            const char* user_msg = (const char*)regs->rdi;
			kprint(user_msg);
			regs->rax = SYS_RES_OK;
            break;
        }
        case SYS_EXIT: {
			int exit_code = (int)regs->rdi;
			uint64_t tid = (uint64_t)regs->rdi;
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
            regs->rax = ipc_send(dest, user_msg);
            break;
        }
        case SYS_IPC_RECV: {
            message_t* user_msg_buffer = (message_t*)regs->rdi;
            regs->rax = ipc_receive(user_msg_buffer);
            break;
        }
		case SYS_REGISTER_DRIVER:
            regs->rax = register_driver((driver_type_t)regs->rdi, (const char*)regs->rsi);
            break;

        case SYS_GET_DRIVER_TID:
            regs->rax = get_driver_tid((driver_type_t)regs->rdi);
            break;

        case SYS_GET_DRIVER_TID_BY_NAME:
            regs->rax = get_driver_tid_by_name((const char*)regs->rdi);
            break;
			
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
					map_to_other_pml4(proc->page_directory, phys_addr, old_page_limit + (i * PAGE_SIZE), PAGE_USER | PAGE_WRITE | PAGE_PRESENT);
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
		
		case SYS_GET_DISK_COUNT: {
			regs->rax = ide_count;
			break;
		}

		case SYS_GET_DISK_INFO: {
			uint64_t idx = regs->rdi;
			disk_info_t* user_info = (disk_info_t*)regs->rsi;
			if (idx >= ide_count) {
				regs->rax = SYS_RES_INVALID;
				break;
			}
			ide_device_t* dev = &mounted_ides[idx];
			user_info->id = idx;
			user_info->sector_size = 512;
			user_info->type = DISK_TYPE_IDE;
			// В твоей структуре ide_device_t нет total_sectors, 
			// но драйвер IDE обычно знает размер из команды IDENTIFY.
			// Предположим, ты добавишь это поле в ide_device_t.
			// user_info->total_sectors = dev->size_lba;
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
			if (idx >= volume_count) {
				regs->rax = SYS_RES_INVALID;
				break;
			}
			volume_t* vol = &mounted_volumes[idx];
			user_info->id = vol->id;
			user_info->parent_disk_id = vol->device.id;
			user_info->start_lba = vol->partition_lba;
			// ВАЖНО: Твоя структура volume_t должна иметь размер раздела!
			user_info->size_sectors = vol->sector_count;  // TODO: Добавить size в volume_t при парсинге MBR
			user_info->bootable = vol->active;
			user_info->partition_type = 0x0B; // FAT32 (или брать реальный тип из MBR)
			regs->rax = SYS_RES_OK;
			break;
		}
		
        default: {
            kprint("Unknown Syscall invoked!\n");
            regs->rax = SYS_RES_INVALID;
            break;
        }
    }
}

void fpu_init() {
	uint64_t cr0, cr4;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1 << 2); // Очищаем бит EM (Emulation)
    cr0 |= (1 << 1);  // Устанавливаем бит MP (Monitor Coprocessor)
	cr0 |= (1 << 5);  // Бит NE (Numeric Error)
    asm volatile("mov %0, %%cr0" :: "r"(cr0));
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1 << 9);  // Устанавливаем бит OSFXSR (SSE support)
    cr4 |= (1 << 10); // Устанавливаем бит OSXMMEXCPT (Unmasked Exception support)
    asm volatile("mov %0, %%cr4" :: "r"(cr4));
	uint32_t mxcsr = 0x1F80;
    asm volatile("ldmxcsr %0" :: "m"(mxcsr));
    asm volatile("fninit");
    asm volatile("fxsave %0" : "=m"(default_fpu_state));
}


// -------------------------
//           IDE
// -------------------------

void get_ide_device_name(ide_device_t* device, char* buff) { // buff >= 24 bytes
    buff[0] = 'i';
    buff[1] = 'd';
    buff[2] = 'e';
    
    char buf[21]; 
    uint64_to_dec(device->id, buf);
    
    for (int i = 0; i < 20; i++) {
        buff[3 + i] = buf[i];
        if (buf[i] == 0) {
            break;
        }
    }
}

void get_volume_name(volume_t* v, char* buff) { // buff >= 22 bytes
    buff[0] = 'p';
    
    char buf[21];
    uint64_to_dec(v->id, buf);
    
    for (int i = 0; i < 20; i++) {
        buff[1 + i] = buf[i];
        if (buf[i] == 0) {
            break;
        }
    }
}

int ide_identify(ide_device_t* dev) {
    outb(dev->io_base + 6, dev->drive_select);
    outb(dev->io_base + 2, 0);
    outb(dev->io_base + 3, 0);
    outb(dev->io_base + 4, 0);
    outb(dev->io_base + 5, 0);
    outb(dev->io_base + 7, 0xEC);
    uint8_t status = inb(dev->io_base + 7);
    if (status == 0) return 0; // Устройство не существует
    if (!sleep_while_zero(ide_wait_ready, (void*)dev, 5000, 0)) return 0;
    // Для ATA LBA Mid и High должны быть 0
    if (inb(dev->io_base + 4) != 0 || inb(dev->io_base + 5) != 0) return 0;
	int res = 0;
    if (!sleep_while_zero(ide_wait_drq, (void*)dev, 5000, &res) || res == 2) {
		return 0; 
	}
    for (int i = 0; i < 256; i++) {
        inw(dev->io_base);
    }
    return 1; 
}

int ide_wait_ready(void* dev) {
    return (inb(((ide_device_t*)dev)->io_base + 7) & 0x80) ? 0 : 1;
}

int ide_wait_drq(void* dev) {
	uint8_t status = inb(((ide_device_t*)dev)->io_base + 7);
	if (status & 0x01) return 2;
	if (!(status & 0x80) && (status & 0x08)) return 1;
	return 0;
}

int ide_read_sectors(ide_device_t* dev, uint64_t lba, uint16_t count, uint8_t* buffer) {
    if (!sleep_while_zero(ide_wait_ready, (void*)dev, 5000, 0)) return 0;
    uint16_t io = dev->io_base;
    uint8_t slave_bit = (dev->drive_select & 0x10);
    outb(io + 6, 0x40 | slave_bit); 
    outb(io + 2, (uint8_t)(count >> 8));
    outb(io + 2, (uint8_t)count);
    outb(io + 3, (uint8_t)(lba >> 24)); 
    outb(io + 3, (uint8_t)lba);
    outb(io + 4, (uint8_t)(lba >> 32));
    outb(io + 4, (uint8_t)(lba >> 8));
    outb(io + 5, (uint8_t)(lba >> 40));
    outb(io + 5, (uint8_t)(lba >> 16));
    outb(io + 7, ATA_CMD_READ_PIO_EXT);
	int res = 0;
    for (int i = 0; i < count; i++) {
        if (!sleep_while_zero(ide_wait_drq, (void*)dev, 5000, &res) || res == 2) {
            return 0; 
        }
        insw(io, buffer + (i * 512), 256);
    }
	return 1;
}

int ide_read_sector(ide_device_t* dev, uint64_t lba, uint8_t* buffer) {
	return ide_read_sectors(dev, lba, 1, buffer);
}


// ----------------------------
//         File System
// ----------------------------

void mbr_mount_all_partitions(ide_device_t* dev, uint8_t is_boot_device) {
    uint8_t sector[512];
    
    ide_read_sector(dev, 0, sector);

    for (int i = 0; i < 4; i++) {
        uint32_t entry_offset = 0x1BE + (i * 16);
		uint8_t status = sector[entry_offset];
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
                v->active = (status == 0x80);
				v->id = volume_count;
				
				if (is_boot_device && v->active) {
                    system_volume = v;
                }
                
                volume_count++;
            }
        }
    }
}

void mbr_storage_init(uint8_t boot_drive_id) {
    uint16_t ide_ports[] = { 0x1F0, 0x170 };
    uint8_t drive_types[] = { 0xA0, 0xB0 };

    for (int p = 0; p < 2; p++) {
        for (int d = 0; d < 2; d++) {
            ide_device_t* dev = &mounted_ides[ide_count];
            dev->io_base = ide_ports[p];
            dev->drive_select = drive_types[d];
			dev->id = ide_count;

            if (ide_identify(dev)) {
                uint8_t is_boot_device = (0x80 + ide_count == boot_drive_id);

                if (is_boot_device) {
                    system_ide = dev;
                }

                mbr_mount_all_partitions(dev, is_boot_device);
				ide_count++;
            }
        }
    }
	
	if (!system_volume) kernel_error(0x2, boot_drive_id, ide_count, volume_count, 0);
}

uint64_t cluster_to_lba(volume_t* vol, uint32_t cluster) {
    // LBA = Data_Start + (Cluster - 2) * Sectors_Per_Cluster
    uint64_t offset = (uint64_t)(cluster - 2) * (uint64_t)vol->sec_per_clus;
    return vol->data_lba + offset;
}

uint32_t get_next_cluster(volume_t* vol, uint32_t current_cluster) {
    uint8_t buffer[512];
    uint64_t fat_offset = (uint64_t)current_cluster * 4;
    uint64_t fat_sector = fat_offset / 512;
    uint64_t ent_offset = fat_offset % 512;
    uint64_t lba = vol->fat_lba + fat_sector;
    ide_read_sector(&vol->device, lba, buffer);
    uint32_t val = *(uint32_t*)&buffer[ent_offset];
    return val & 0x0FFFFFFF;
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
        vmm_map_page(phys, heap_current_limit, PAGE_PRESENT | PAGE_WRITE);
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
            ide_read_sector(&v->device, lba + s, buffer);
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
            ide_read_sector(&v->device, lba + s, buffer);
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
            *result = file_list[i];
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


// -------------------------
//         Process
// -------------------------

uint64_t get_current_pml4() {
    uint64_t pml4;
    asm volatile("mov %%cr3, %0" : "=r"(pml4));
    return pml4;
}

void set_current_pml4(uint64_t phys_addr) {
    asm volatile("mov %0, %%cr3" :: "r"(phys_addr));
}

void* temp_map(uint64_t phys_addr) {
    uint64_t* pte = vmm_get_pte(TEMP_PAGE_VIRT, 1);
    *pte = (phys_addr & PAGE_MASK) | PAGE_PRESENT | PAGE_WRITE;
    asm volatile("invlpg (%0)" :: "r"((uint64_t)TEMP_PAGE_VIRT) : "memory");
    return (void*)TEMP_PAGE_VIRT;
}

void temp_unmap() {
    uint64_t* pte = vmm_get_pte(TEMP_PAGE_VIRT, 1);
    *pte = 0;
    asm volatile("invlpg (%0)" :: "r"((uint64_t)TEMP_PAGE_VIRT) : "memory");
}

process_t* create_process(const char* name) {
    process_t* new_proc = (process_t*)kernel_malloc(sizeof(process_t));
    kernel_memset(new_proc, 0, sizeof(process_t));

    static uint32_t next_pid = 1;
    new_proc->id = next_pid++;
    if (name) kernel_memcpy(new_proc->name, name, 31);
    new_proc->state = THREAD_READY;

    uint64_t pml4_phys = pmm_alloc_block();
    new_proc->page_directory = (uint64_t*)pml4_phys;
    uint64_t* pml4_virt = (uint64_t*)temp_map(pml4_phys);
    kernel_memset(pml4_virt, 0, 4096);
    
    uint64_t* kernel_pml4_virt = (uint64_t*)temp_map(get_current_pml4()); 
    uint64_t kernel_entries[256];
    for (int i = 256; i < 512; i++) {
        kernel_entries[i - 256] = kernel_pml4_virt[i];
	}
    pml4_virt = (uint64_t*)temp_map(pml4_phys);
    for (int i = 256; i < 512; i++) {
        pml4_virt[i] = kernel_entries[i - 256];
    }
    pml4_virt[510] = pml4_phys | PAGE_PRESENT | PAGE_WRITE;

    temp_unmap();
    return new_proc;
}

uint64_t get_or_alloc_table(uint64_t parent_phys, int index, int flags) {
    uint64_t* parent_virt = (uint64_t*)temp_map(parent_phys);
    
    if (!(parent_virt[index] & PAGE_PRESENT)) {
        uint64_t new_table_phys = pmm_alloc_block();
        parent_virt[index] = new_table_phys | flags | PAGE_PRESENT | PAGE_WRITE;
        uint64_t* new_table_virt = (uint64_t*)temp_map(new_table_phys);
        kernel_memset(new_table_virt, 0, 4096);
    }
    
    parent_virt = (uint64_t*)temp_map(parent_phys);
    uint64_t entry = parent_virt[index];
    
    return entry & PAGE_MASK;
}

void map_to_other_pml4(uint64_t* pml4_phys, uint64_t phys, uint64_t virt, int flags) {
    int pml4_idx = PML4_INDEX(virt);
    int pdp_idx  = PDP_INDEX(virt);
    int pd_idx   = PD_INDEX(virt);
    int pt_idx   = PT_INDEX(virt);

    int table_flags = PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    
    uint64_t pdp_phys = get_or_alloc_table((uint64_t)pml4_phys, pml4_idx, table_flags);
    uint64_t pd_phys  = get_or_alloc_table(pdp_phys, pdp_idx, table_flags);
    uint64_t pt_phys  = get_or_alloc_table(pd_phys, pd_idx, table_flags);
	
    uint64_t* pt_virt = (uint64_t*)temp_map(pt_phys);
    pt_virt[pt_idx] = (phys & PAGE_MASK) | flags;
    
    temp_unmap();
}

void process_map_memory(process_t* proc, uint64_t virt, uint64_t size) {
    uint64_t old_pml4 = get_current_pml4();
    
    set_current_pml4((uint64_t)proc->page_directory);

    for (uint64_t i = 0; i < size; i += 4096) {
        uint64_t phys = pmm_alloc_block();
        vmm_map_page(phys, virt + i, PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
    }

    set_current_pml4(old_pml4);
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


// -------------------------
//        Scheduler
// -------------------------

void init_scheduler() {
	kernel_memset(&kernel_process, 0, sizeof(process_t));
    kernel_process.id = 0;
    kernel_memcpy(kernel_process.name, "KERNEL", 6);
    kernel_process.page_directory = (uint64_t*)get_current_pml4();
	kernel_process.entry_point = (uint64_t)kernel_main;
    thread_t* kthread = (thread_t*)kernel_malloc(sizeof(thread_t));
	kernel_memset(kthread, 0, sizeof(thread_t));
	kthread->stack_base = (uint64_t)kernel_stack; 
    uint64_t current_rsp;
    asm volatile("mov %%rsp, %0" : "=r"(current_rsp));
    kthread->rsp = current_rsp;
    kthread->cr3 = get_current_pml4();
    kthread->state = 1;
	kthread->owner = &kernel_process;
    kthread->next = kthread;
	kthread->tid = thread_count;
	thread_count++;
    current_thread = kthread;
    ready_queue = kthread;
}

thread_t* create_thread_core(uint64_t cr3, process_t* owner) {
    thread_t* t = (thread_t*)kernel_malloc(sizeof(thread_t));
	kernel_memset(t, 0, sizeof(thread_t));
	kernel_memcpy(t->fpu_state, default_fpu_state, 512);
    void* stack = kernel_malloc(KERNEL_STACK_SIZE);
    kernel_memset(stack, 0, KERNEL_STACK_SIZE);
    t->stack_base = (uint64_t)stack;
    t->rsp = (uint64_t)stack + KERNEL_STACK_SIZE;
    t->cr3 = cr3;
    t->state = THREAD_READY;
    t->tid = thread_count;
	thread_count++;
	t->owner = owner;
    if (ready_queue == 0) {
        ready_queue = t;
        t->next = t;
    } else {
        t->next = ready_queue->next;
        ready_queue->next = t;
    }
    return t;
}

void create_user_thread(uint64_t entry_point, uint64_t user_stack, uint64_t cr3_phys, process_t* proc) {
	asm volatile("cli");
    thread_t* t = create_thread_core(cr3_phys, proc);
	
    uint64_t tcb_size = 0x30; 
    uint64_t total_tls_size = proc->tls_mem_size + tcb_size;
    
    uint64_t alloc_pages = (total_tls_size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t tls_virt = 0x00007FFFFF000000;
    
    for (uint64_t i = 0; i < alloc_pages * PAGE_SIZE; i += PAGE_SIZE) {
        uint64_t phys = pmm_alloc_block();
        map_to_other_pml4((uint64_t*)cr3_phys, phys, tls_virt + i, PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
    }
    
    uint64_t old_cr3 = get_current_pml4();
    set_current_pml4(cr3_phys);
    
    if (proc->tls_mem_size > 0) {
        kernel_memcpy((void*)tls_virt, (void*)proc->tls_image_vaddr, proc->tls_file_size);
        kernel_memset((void*)(tls_virt + proc->tls_file_size), 0, proc->tls_mem_size - proc->tls_file_size);
    }
	
	uint64_t fs_base = tls_virt + proc->tls_mem_size;
    
    aos_tcb_t* tcb = (aos_tcb_t*)fs_base;
    
    tcb->tcb_self     = (void*)fs_base;
    tcb->tid          = t->tid;
    tcb->pid          = t->owner->id;
    tcb->thread_errno = 0;
    tcb->pending_msgs = 0;
    tcb->local_heap   = (void*)0;
    tcb->stack_canary = 0x595e9fbd94fda766ULL;
	
    set_current_pml4(old_cr3);
    t->fs_base = fs_base;
	
    uint64_t* sp = (uint64_t*)t->rsp;
    *(--sp) = GDT_USER_DATA;   // SS
    *(--sp) = user_stack;      // RSP (User)
    *(--sp) = 0x202;           // RFLAGS (IF=1)
    *(--sp) = GDT_USER_CODE;   // CS
    *(--sp) = entry_point;     // RIP
    *(--sp) = (uint64_t)trampoline_enter_user;
    *(--sp) = 0x202; // RFLAGS
    *(--sp) = 0;     // R15
    *(--sp) = 0;     // R14
    *(--sp) = 0;     // R13
    *(--sp) = 0;     // R12
    *(--sp) = 0;     // RBP
    *(--sp) = 0;     // RBX
    
    t->rsp = (uint64_t)sp;
	asm volatile("sti");
}

void create_kernel_thread(void (*entry)(void)) {
	asm volatile("cli");
    thread_t* t = create_thread_core(get_current_pml4(), &kernel_process);
    
    uint64_t* sp = (uint64_t*)t->rsp;

    *(--sp) = (uint64_t)entry;

    *(--sp) = (uint64_t)trampoline_enter_kernel;

    *(--sp) = 0x202; // RFLAGS
    *(--sp) = 0; // R15 ...
    *(--sp) = 0;
    *(--sp) = 0;
    *(--sp) = 0;
    *(--sp) = 0; // RBP
    *(--sp) = 0; // RBX
    
    t->rsp = (uint64_t)sp;
	asm volatile("sti");
}

int kill_thread(thread_t* target, int exit_code) {
	if (target->tid == 1) return SYS_RES_NO_PERM;
    asm volatile("cli");
    target->state = THREAD_ZOMBIE;
	target->exit_code = exit_code;
    /*mutex_t* m = target->owned_mutexes;
    while (m) {
        mutex_t* next_m = m->next_owned;
        if (m->wait_queue) {
            thread_t* waiter = m->wait_queue;
            thread_t* prev_waiter = 0;
            while (waiter && waiter->state == THREAD_ZOMBIE) {
                break; 
            }
            if (waiter) {
                m->wait_queue = waiter->next_waiter;
                waiter->state = THREAD_READY;
                m->owner = waiter;
                m->locked = 0;
                m->owner = 0;
            } else {
                m->locked = 0;
                m->owner = 0;
            }
        } else {
            m->locked = 0;
            m->owner = 0;
        }
        m->next_owned = 0;
        m = next_m;
    }
    target->owned_mutexes = 0;*/
    thread_t* prev = target;
    while (prev->next != target) {
        prev = prev->next;
        if (prev == target) break;
    }
    if (prev == target) return SYS_RES_NO_PERM;
    prev->next = target->next;
    if (ready_queue == target) {
        ready_queue = target->next;
    }
	target->next = zombies_list;
	zombies_list = target;
	asm volatile("sti");
	return SYS_RES_OK;
}

thread_t* get_thread_by_id(uint64_t tid) {
    thread_t* t = ready_queue;
    if (!t) return 0;
    do {
        if (t->tid == tid) return t;
        t = t->next;
    } while (t != ready_queue);
    return 0;
}

int64_t register_driver(driver_type_t type, const char* user_name) {
    char name_buf[DRIVER_NAME_MAX];
    kernel_memset(name_buf, 0, DRIVER_NAME_MAX);
    if (user_name != 0 && !copy_string_from_user(user_name, name_buf, DRIVER_NAME_MAX)) {
        return -1;
    }
    asm volatile("cli");
    driver_info_t* cur = drivers_list_head;
    while (cur) {
        if (type != DT_NONE && type != DT_USER && cur->type == type) {
            asm volatile("sti");
            return -2;
        }
        if (name_buf[0] != 0 && kernel_strcmp(cur->name, name_buf) == 0) {
            asm volatile("sti");
            return -3;
        }
        cur = cur->next;
    }
    driver_info_t* new_driver = (driver_info_t*)kernel_malloc(sizeof(driver_info_t));
    if (!new_driver) {
        asm volatile("sti");
        return -4;
    }
    new_driver->thread = current_thread;
    new_driver->tid = current_thread->tid;
    new_driver->type = type;
    kernel_memcpy(new_driver->name, name_buf, DRIVER_NAME_MAX);
    new_driver->next = drivers_list_head;
    drivers_list_head = new_driver;
    asm volatile("sti");
    return 0; 
}

int get_driver_tid_sleep_wrapper(void* arg) {
    return get_driver_tid(*(driver_type_t*)arg);
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
//           ELF
// -------------------------

void load_elf_raw_fat32(volume_t* v, fat32_dirent_t* file, elf_load_result_t* result) {
    uint64_t file_size = 0;
    uint8_t* raw_data = (uint8_t*)fat32_read_file(v, file, &file_size);
    if (!raw_data) {
        result->result = ELF_RESULT_INVALID;
        return;
    }

    Elf64_Ehdr* hdr = (Elf64_Ehdr*)raw_data;

    if (hdr->e_ident[0] != 0x7F || hdr->e_ident[1] != 'E' || 
        hdr->e_ident[2] != 'L' || hdr->e_ident[3] != 'F' ||
        hdr->e_ident[4] != 2) { // 2 = ELFCLASS64
        
        kernel_free(raw_data);
        result->result = ELF_RESULT_INVALID;
        return;
    }

    result->entry_point = hdr->e_entry;

    process_t* proc = create_process(file->name);
    result->proc = proc;

    Elf64_Phdr* phdr = (Elf64_Phdr*)(raw_data + hdr->e_phoff);
	uint64_t max_vaddr = 0; 

    for (int i = 0; i < hdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD) {
            uint64_t vaddr  = phdr[i].p_vaddr;
            uint64_t memsz  = phdr[i].p_memsz;
            uint64_t filesz = phdr[i].p_filesz;
            uint64_t offset = phdr[i].p_offset;
            uint64_t start_page = vaddr & PAGE_MASK;
            uint64_t end_page   = (vaddr + memsz + 4095) & PAGE_MASK;
            uint64_t page_count = (end_page - start_page) / 4096;
			
			if (vaddr + memsz > max_vaddr) {
                max_vaddr = vaddr + memsz;
            }

            for (uint64_t p = 0; p < page_count; p++) {
                uint64_t curr_virt = start_page + (p * 4096);
                uint64_t phys;
                uint64_t existing_phys = vmm_get_phys_from_pml4(proc->page_directory, curr_virt);
                if (existing_phys != 0) {
                    phys = existing_phys;
                } else {
                    phys = pmm_alloc_block();
                    map_to_other_pml4(proc->page_directory, phys, curr_virt, 
                                      PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
                }
                void* ptr = temp_map(phys);
                kernel_memset(ptr, 0, 4096);
				
                uint64_t file_data_start = vaddr;
                uint64_t file_data_end   = vaddr + filesz;
                uint64_t page_start = curr_virt;
                uint64_t page_end   = curr_virt + 4096;
                uint64_t copy_start = (file_data_start > page_start) ? file_data_start : page_start;
                uint64_t copy_end   = (file_data_end < page_end)     ? file_data_end   : page_end;
                
                if (copy_start < copy_end) {
                    uint64_t bytes_to_copy  = copy_end - copy_start;
                    uint64_t offset_in_page = copy_start - page_start;
                    uint64_t offset_in_file = copy_start - vaddr;
                    
                    kernel_memcpy(
                        (uint8_t*)ptr + offset_in_page, 
                        raw_data + offset + offset_in_file, 
                        bytes_to_copy
                    );
                }
                
                temp_unmap();
            }
        }
		if (phdr[i].p_type == PT_TLS) {
			proc->tls_image_vaddr = phdr[i].p_vaddr;
            proc->tls_file_size   = phdr[i].p_filesz;
            proc->tls_mem_size    = phdr[i].p_memsz;
            proc->tls_align       = phdr[i].p_align;
		}
    }
	
	if (max_vaddr > 0) {
        proc->heap_limit = (max_vaddr + 4095) & ~((uint64_t)4095);
    } else {
        proc->heap_limit = 0x40000000; 
    }

    Elf64_Shdr* shdr = (Elf64_Shdr*)(raw_data + hdr->e_shoff);
    char* strtab = 0;
    Elf64_Sym* symtab = 0;
    uint64_t sym_count = 0;

    for (int i = 0; i < hdr->e_shnum; i++) {
        if (shdr[i].sh_type == SHT_STRTAB && i != hdr->e_shstrndx) {
             strtab = (char*)(raw_data + shdr[i].sh_offset);
        }
    }

    for (int i = 0; i < hdr->e_shnum; i++) {
        if (shdr[i].sh_type == SHT_SYMTAB) {
            symtab = (Elf64_Sym*)(raw_data + shdr[i].sh_offset);
            sym_count = shdr[i].sh_size / sizeof(Elf64_Sym);
            
            if (shdr[i].sh_link != 0) {
                strtab = (char*)(raw_data + shdr[shdr[i].sh_link].sh_offset);
            }
            break;
        }
    }
    
    if (symtab && strtab) {
        for (uint64_t i = 0; i < sym_count; i++) {
            uint8_t bind = symtab[i].st_info >> 4;
            if (bind == 1 && symtab[i].st_name != 0 && symtab[i].st_value != 0) {
                // register_symbol((char*)(strtab + symtab[i].st_name), symtab[i].st_value);
            }
        }
    }

    kernel_free(raw_data);
    result->result = ELF_RESULT_OK;
}

void start_elf_process(elf_load_result_t* res) {
    uint64_t user_stack_virt = 0x0000700000000000;
    uint64_t stack_pages = 8;
    
    for (uint64_t i = 0; i < stack_pages; i++) {
        uint64_t phys_page = pmm_alloc_block();
        map_to_other_pml4(res->proc->page_directory, 
                          phys_page, 
                          user_stack_virt - (i * PAGE_SIZE), 
                          PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
        kernel_memset((void*)P2V(phys_page), 0, PAGE_SIZE);
    }
    uint64_t user_rsp = user_stack_virt + PAGE_SIZE;
    user_rsp = (user_rsp & ~0xFULL) - 8;
    create_user_thread(res->entry_point, user_rsp, (uint64_t)res->proc->page_directory, res->proc);
}


/*
// -------------------------
//          Mutex
// -------------------------

void mutex_init(mutex_t* m) {
    m->locked = 0;
    m->owner = 0;
    m->wait_queue = 0;
}

void mutex_lock(mutex_t* m) {
    asm volatile("cli");
    
    if (m->locked == 0) {
        m->locked = 1;
        m->owner = current_thread;
		m->next_owned = current_thread->owned_mutexes;
        current_thread->owned_mutexes = m;
        asm volatile("sti");
    } else {
        current_thread->state = THREAD_BLOCKED;
        current_thread->next_waiter = m->wait_queue;
        m->wait_queue = current_thread;
        asm volatile("sti");
        schedule();
    }
}

void mutex_unlock(mutex_t* m) {
    asm volatile("cli");
    if (m->wait_queue != 0) {
        thread_t* t = m->wait_queue;
        m->wait_queue = t->next_waiter;
        t->state = THREAD_READY;
        m->owner = t;
    } else {
        m->locked = 0;
        m->owner = 0;
    }
    asm volatile("sti");
}
*/


// -------------------------
//         Spinlock
// -------------------------

uint64_t spinlock_irq_save(void) {
    uint64_t flags;
    asm volatile("pushfq; pop %0; cli" : "=r"(flags) :: "memory");
    return flags;
}

void spinlock_irq_restore(uint64_t flags) {
    if (flags & 0x200) {
        asm volatile("sti" ::: "memory");
    }
}

void spinlock_acquire(spinlock_t* lock) {
    while (__sync_lock_test_and_set(lock, 1)) {
        asm volatile("pause"); 
    }
}

void spinlock_release(spinlock_t* lock) {
    __sync_lock_release(lock);
}


// -------------------------
//          Timers
// -------------------------

void sleep(uint64_t ms) {
    uint64_t ticks_to_wait = ms / 10; 
    if (ticks_to_wait == 0 && ms > 0) ticks_to_wait = 1;

    asm volatile("cli");
    current_thread->wake_up_time = ticks + ticks_to_wait;
    current_thread->state = THREAD_BLOCKED;
    asm volatile("sti");

    schedule();
}

int sleep_while_zero(int (*func)(void*), void* arg, uint64_t timeout_ms, int* out_result) {
    int attempts = 0;
    int res = func(arg);
    uint64_t start_tick = ticks;
    uint64_t timeout_ticks = timeout_ms / 10;
    if (timeout_ticks == 0 && timeout_ms > 0) timeout_ticks = 1;
    while (res == 0) {
        if (attempts < 100) {
            attempts++;
            asm volatile("pause");
            res = func(arg);
            if (res != 0) break;
            continue;
        }
        if (timeout_ms != 0) {
            uint64_t current = ticks;
            uint64_t elapsed = (current >= start_tick) ? (current - start_tick) : (0xFFFFFFFFFFFFFFFF - start_tick + current);
            if (elapsed >= timeout_ticks) {
                if (out_result) *out_result = 0;
                return 0;
            }
        }
        sleep(10);
        res = func(arg);
    }
    if (out_result) *out_result = res;
    return 1;
}


// -------------------------
//           IPC
// -------------------------

int64_t ipc_send(uint64_t dest_tid, message_t* user_msg) {
    asm volatile("cli");
    thread_t* target = get_thread_by_id(dest_tid);
    if (!target) { asm volatile("sti"); return SYS_RES_INVALID; }
    msg_node_t* node = (msg_node_t*)kernel_malloc(sizeof(msg_node_t));
	if (!node) { asm volatile("sti"); return SYS_RES_KERNEL_ERR; }
	kernel_memset(node, 0, sizeof(msg_node_t));
    node->msg = *user_msg;
    node->msg.sender_tid = current_thread->tid;
	if (user_msg->payload_size > 0 && user_msg->payload_ptr != 0) {
        void* kbuffer = kernel_malloc(user_msg->payload_size);
        if (!kbuffer) {
            kernel_free(node);
            asm volatile("sti");
            return SYS_RES_KERNEL_ERR;
        }
        kernel_memcpy(kbuffer, user_msg->payload_ptr, user_msg->payload_size);
        node->msg.payload_ptr = (uint8_t*)kbuffer;
    } else {
        node->msg.payload_ptr = 0;
        node->msg.payload_size = 0;
    }
    node->next = 0;
    if (target->msg_queue_tail) {
        target->msg_queue_tail->next = node;
    } else {
        target->msg_queue_head = node;
    }
    target->msg_queue_tail = node;
	if (target->fs_base != 0) {
        uint64_t old_cr3 = get_current_pml4();
        if (old_cr3 != target->cr3) {
            set_current_pml4(target->cr3);
        }
        aos_tcb_t* tcb = (aos_tcb_t*)target->fs_base;
        tcb->pending_msgs++;
        if (old_cr3 != target->cr3) {
            set_current_pml4(old_cr3);
        }
    }
    if (target->state == THREAD_BLOCKED && target->waiting_for_msg) {
        target->state = THREAD_READY;
        target->waiting_for_msg = 0;
    }
    asm volatile("sti");
    return SYS_RES_OK;
}

int64_t ipc_receive(message_t* user_msg_out) {
    while (1) {
        asm volatile("cli");
        if (current_thread->msg_queue_head) {
            msg_node_t* node = current_thread->msg_queue_head;
            uint8_t* user_dest_ptr = user_msg_out->payload_ptr;
            uint64_t user_max_size = user_msg_out->payload_size;
            void* kernel_ptr = node->msg.payload_ptr;
            uint64_t actual_size = 0;
            if (kernel_ptr != 0) {
                 actual_size = node->msg.payload_size;
            }
            if (actual_size > user_max_size) {
                user_msg_out->payload_size = actual_size;
                user_msg_out->type = node->msg.type;
                user_msg_out->subtype = node->msg.subtype;
                user_msg_out->sender_tid = node->msg.sender_tid;
                asm volatile("sti");
                return SYS_RES_BUFFER_TOO_SMALL;
            }
            current_thread->msg_queue_head = node->next;
            if (!current_thread->msg_queue_head) {
                current_thread->msg_queue_tail = 0;
            }
			if (current_thread->fs_base != 0) {
                aos_tcb_t* tcb = (aos_tcb_t*)current_thread->fs_base;
                if (tcb->pending_msgs > 0) {
                    tcb->pending_msgs--;
                }
            }
            *user_msg_out = node->msg;
            if (actual_size > 0 && kernel_ptr != 0) {
                if (user_dest_ptr != 0) {
                    kernel_memcpy(user_dest_ptr, kernel_ptr, actual_size);
                    user_msg_out->payload_ptr = user_dest_ptr;
                } else {
                    user_msg_out->payload_ptr = 0;
                }
                kernel_free(kernel_ptr);
            } else {
                user_msg_out->payload_ptr = 0;
                user_msg_out->payload_size = 0;
            }
            kernel_free(node);
            asm volatile("sti");
            return SYS_RES_OK;
        }
        current_thread->waiting_for_msg = 1;
        current_thread->state = THREAD_BLOCKED;
        schedule();
		asm volatile("sti");
    }
}


// -------------------------
//          Debug
// -------------------------

__attribute__((noreturn)) void kernel_error(uint64_t code, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4) {
	asm volatile("cli");
	//kclear();
    _kprint_error("KERNEL STOP: 0x");
	char buff[17];
	uint64_to_hex(code, buff);
	_kprint_error(buff);
	_kprint_error(" (");
	_kprint_error(kernel_messages[code]);
    _kprint_error(")\nARGS: 0x");
	uint64_to_hex(arg1, buff);
	_kprint_error(buff);
	_kprint_error("; 0x");
	uint64_to_hex(arg2, buff);
	_kprint_error(buff);
	_kprint_error("; 0x");
	uint64_to_hex(arg3, buff);
	_kprint_error(buff);
	_kprint_error("; 0x");
	uint64_to_hex(arg4, buff);
	_kprint_error(buff);
	_kprint_error("\nThe system has been halted!");
    while (1) {
        asm volatile("hlt");
    }
	__builtin_unreachable();
}

__attribute__((noreturn)) void __stack_chk_fail(void) {
    kernel_error(0x1, (uint64_t)__builtin_return_address(0), 0, 0, 0);
	__builtin_unreachable();
}

__attribute__((noreturn)) void breakpoint(){
	kprint("Breakpoint :-)");
	while (1) {
        asm volatile("cli; hlt");
    }
	__builtin_unreachable();
}

void pausepoint(){
	kprint("Pausepoint. Press any key to continue :3\n");
	while ((inb(0x64) & 1) == 0);
    inb(0x60);
}


// ------------------------
//         Other
// ------------------------

void reset_state() {
	state.system_flags = CAN_REGISTER_KERNEL_DRIVERS | CAN_PRINT;
}


// ------------------------
//      MAIN THREADS
// ------------------------
void kernel_main(boot_info_t* boot_info){
	reset_state();
	if (!(boot_info->flags & BOOT_FLAG_VIDEO_PRESENT)) {
		_kprint_error_vga("VIDEO IS NOT SUPPORTED!");
		__asm__ __volatile__("cli; hlt");
	}
	font = &fontvgasys;
	video = &boot_info->video;
	_kclear();
	_kprint("AOSLDR, hello from long mode...\n");
	char buff[32];
	kernel_memset(buff, 0, 32);
	if (boot_info->magic != BOOT_MAGIC) kernel_error(0x8, 0, boot_info->magic, BOOT_MAGIC, 0);
	if (boot_info->version != boot_ver) kernel_error(0x8, 1, boot_info->version, boot_ver, 0); // Только для UEFI
	_kprint("Loader type: 0x");
	uint64_to_hex(boot_info->type, buff);
	_kprint(buff);
	_kprint("\n");
	if (boot_info->type == BOOT_TYPE_UNKNOWN || boot_info->type == BOOT_TYPE_UEFI /* Временно, удалить */) kernel_error(0x8, 2, boot_info->type, 0, 0);
	gdt_install();
	init_syscall();
	fpu_init();
	uint32_t entry_count = boot_info->mmap.map_size/sizeof(e820_entry_t);
    e820_entry_t* e820_entries = (e820_entry_t*)boot_info->mmap.map_addr;
    uint64_t total_ram = 0;
    for (uint32_t i = 0; i < entry_count; i++) {
        if (e820_entries[i].type == E820_RAM) {
            total_ram += e820_entries[i].length;
        }
    }
	kernel_memset(buff, 0, 32);
	uint64_to_dec(total_ram / 1024 / 1024, buff);
	_kprint("Total RAM: ");
	_kprint(buff);
	_kprint(" MB\n");
	uint64_t *pml4 = (uint64_t *)PHYS_PML4;
    uint64_t *asm_pd = (uint64_t *)PHYS_ASM_PD;
    uint64_t phys_addr = 0x200000;
    for (int i = 1; i < 512; i++) {
        // 0x83 = Present | RW | HugePage (2MB)
        asm_pd[i] = phys_addr | 0x83; 
        phys_addr += 0x200000;
    }
    uint64_t *hhdm_pdpt = (uint64_t *)PHYS_HHDM_PDPT;
    uint64_t *hhdm_pd   = (uint64_t *)PHYS_HHDM_PD;
    kernel_memset(hhdm_pdpt, 0, 4096);
    kernel_memset(hhdm_pd, 0, 4096);
    pml4[256] = PHYS_HHDM_PDPT | 0x3;
    hhdm_pdpt[0] = PHYS_HHDM_PD | 0x3;
    phys_addr = 0;
    for (int i = 0; i < 512; i++) {
        hhdm_pd[i] = phys_addr | 0x83; // 2MB страницы
        phys_addr += 0x200000;
    }
    pml4[510] = PHYS_PML4 | 0x3;
    asm volatile ("mov %0, %%cr3" :: "r"((uint64_t)PHYS_PML4));
	uint64_t bitmap_addr = KERNEL_BASE + 0x100000;
	init_pmm(total_ram, bitmap_addr);
	pmm_deinit_region(0x0, 0x1000000);
	pmm_init_region(0x1000000, total_ram - 0x1000000);
	pml4_table_virt = (uint64_t *)(KERNEL_BASE + PHYS_PML4);
	_kprint("PMM & VMM is configured!\n");
	uint32_t eax, ebx, ecx, edx;
    eax = 7; ecx = 0;
    asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(eax), "c"(ecx));
    if (ebx & 1) {
        state.cpu_flags |= FSGSBASE;
        uint64_t cr4;
        asm volatile("mov %%cr4, %0" : "=r"(cr4));
        cr4 |= 0x10000;
        asm volatile("mov %0, %%cr4" :: "r"(cr4));
    } else {
		_kprint_error("Warning: FSGSBASE not supported! TLS may be slow.\n");
	}
	idt_install();
    pic_remap();
    timer_init(100);
	init_scheduler();
    outb(0x21, 0xFC);
    outb(0xA1, 0xFF);
	_kprint("IDT & PIC are set! We're safe\n");
	_kprint("Offset: ");
	uint64_to_dec(offsetof(thread_t, fpu_state), buff);
	_kprint(buff);
	_kprint("\n");
    __asm__ __volatile__("sti");
	create_kernel_thread(idle_thread);
	mbr_storage_init(boot_info->specific.mbr.drive_num);
	kprint("System volume is ");
	kernel_memset(buff, 0, 32);
	get_ide_device_name(system_ide, buff);
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
		if (files[i].attr & 0x10) {
			kprint(" [D]");
		}
		kprint("\n");
	}
	kprint("Keyboard driver..\n");
	if (!fat32_find_in_dir(system_volume, &drivers_dir, "KBDDRIVER.ELF", &file)){
		kernel_error(0x4, system_volume->id, drivers_dir.cluster, 0, 0);
	}
	elf_load_result_t* driver = (elf_load_result_t*)kernel_malloc(sizeof(elf_load_result_t));
	kernel_memset(driver, 0, sizeof(elf_load_result_t));
	load_elf_raw_fat32(system_volume, &file, driver);
	if (driver->result != ELF_RESULT_OK) kernel_error(0x6, driver->result, driver->entry_point, 0, 0);
    start_elf_process(driver);
	int tid = 0;
	driver_type_t dtype = DT_KEYBOARD;
	if(!sleep_while_zero(get_driver_tid_sleep_wrapper, &dtype, 5000, &tid)) kernel_error(0x6, 0x1DEAD, dtype, 0, 0);
	kprint("VFS driver..\n");
	if (!fat32_find_in_dir(system_volume, &drivers_dir, "VFSDRIVER.ELF", &file)){
		kernel_error(0x4, system_volume->id, drivers_dir.cluster, 0, 0);
	}
	kernel_memset(driver, 0, sizeof(elf_load_result_t));
	load_elf_raw_fat32(system_volume, &file, driver);
	if (driver->result != ELF_RESULT_OK) kernel_error(0x6, driver->result, driver->entry_point, 0, 0);
    start_elf_process(driver);
	kprint("Register...\n");
	tid = 0;
	dtype = DT_VFS;
	if(!sleep_while_zero(get_driver_tid_sleep_wrapper, &dtype, 5000, &tid)) kernel_error(0x6, 0x1DEAD, dtype, 0, 0);
	kprint("Driver TID: ");
	uint64_to_dec(tid, buff);
	kprint(buff);
	kprint("\n");
	kprint("Load tree.elf\n");
	if (!fat32_find_in_dir(system_volume, 0, "tree.elf", &file)){
		kernel_error(0x4, system_volume->id, drivers_dir.cluster, 0, 0);
	}
	kernel_memset(driver, 0, sizeof(elf_load_result_t));
	load_elf_raw_fat32(system_volume, &file, driver);
	if (driver->result != ELF_RESULT_OK) kernel_error(0x6, driver->result, driver->entry_point, 0, 0);
    start_elf_process(driver);
	while(1) {
        //char c = kbd_get_char();
        //kprint_char(c, 0x0D);
    }
}

void idle_thread() {
    while(1) {
        //cleanup_zombies();
        asm volatile("sti; hlt");
    }
}