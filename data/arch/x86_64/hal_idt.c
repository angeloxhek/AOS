#include "hal_arch.h"

#define IDT_INTERRUPTS \
    X(0)  X(1)  X(2)  X(3)  X(4)  X(5)  X(6)  X(7)  \
    X(8)  X(9)  X(10) X(11) X(12) X(13) X(14) X(15) \
    X(16) X(17) X(18) X(19) X(20) X(21) X(22) X(23) \
    X(24) X(25) X(26) X(27) X(28) X(29) X(30) X(31) \
    X(32) X(33) X(34) X(35) X(36) X(37) X(38) X(39) \
    X(40) X(41) X(42) X(43) X(44) X(45) X(46) X(47)

struct idt_entry idt[256];
struct idt_ptr   idtp;

extern void kernel_on_timer_tick(void);
extern void kernel_on_keyboard_irq(uint8_t scancode);
extern void kernel_handle_user_exception(uint64_t int_no, uint64_t rip);
extern void kernel_error(uint64_t code, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4);

extern st_flags_t state;

static void pic_remap() {
    uint8_t a1, a2;
    a1 = hal_inb(0x21);
    a2 = hal_inb(0xA1);
    hal_outb(0x20, 0x11);
    hal_outb(0xA0, 0x11);
    hal_outb(0x21, 0x20);
    hal_outb(0xA1, 0x28);
    hal_outb(0x21, 0x04);
    hal_outb(0xA1, 0x02);
    hal_outb(0x21, 0x01);
    hal_outb(0xA1, 0x01);
    hal_outb(0x21, a1);
    hal_outb(0xA1, a2);
}

static void idt_set_gate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low  = (base & 0xFFFF);
    idt[num].sel       = sel;
    idt[num].ist       = 0;
    idt[num].flags     = flags;
    idt[num].base_mid  = (base >> 16) & 0xFFFF;
    idt[num].base_high = (base >> 32) & 0xFFFFFFFF;
    idt[num].reserved  = 0;
}

void hal_interrupts_init(void) {
    idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
    idtp.base  = (uint64_t)&idt;
    for(int i = 0; i < 256; i++) {
        idt_set_gate(i, 0, 0, 0); 
    }
    
    #define X(n) extern void isr##n(); idt_set_gate(n, (uint64_t)isr##n, 0x08, 0x8E);
    IDT_INTERRUPTS
    #undef X
    
    __asm__ __volatile__("lidt (%0)" : : "r" (&idtp));
    pic_remap();
	
	hal_outb(0x21, 0xFC);
	hal_outb(0xA1, 0xFF); 
}

void hal_timer_init(uint32_t frequency) {
    uint32_t divisor = 1193180 / frequency;
    hal_outb(0x43, 0x36);
    hal_outb(0x40, (uint8_t)(divisor & 0xFF));
    hal_outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}

void isr_handler(registers_t *r) {
    if (r->int_no < 32) {
        if (state.system_flags & KERNEL_PANIC) {
            __asm__ volatile("cli; hlt");
            __builtin_unreachable();
        }
        state.system_flags |= KERNEL_PANIC;
        
        if ((r->cs & 3) != 3) { // Kernel space crash
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
		
		__asm__ volatile("cli; hlt");
        
        kernel_handle_user_exception(r->int_no, r->rip);
        return;
        
    } else if (r->int_no == 33) {
        uint8_t scancode = hal_inb(0x60);
        kernel_on_keyboard_irq(scancode);
        hal_outb(0x20, 0x20); // EOI
        
    } else if (r->int_no == 32) {
        hal_outb(0x20, 0x20); // EOI
        kernel_on_timer_tick();
        
    } else if (r->int_no >= 32) {
        if (r->int_no >= 40) hal_outb(0xA0, 0x20);
        hal_outb(0x20, 0x20);
    }
}