#include "hal_arch.h"

extern uint8_t bcd_to_bin(uint8_t bcd);
extern uint64_t rtc_to_unix(uint8_t sec, uint8_t min, uint8_t hour, uint8_t day, uint8_t month, uint8_t year);

void hal_outb(uint16_t port, uint8_t val) {
    __asm__ volatile ( "outb %b0, %w1" : : "a"(val), "Nd"(port) : "memory");
}

uint8_t hal_inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ( "inb %w1, %b0" : "=a"(ret) : "Nd"(port) : "memory");
    return ret;
}

uint16_t hal_inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile ( "inw %w1, %w0" : "=a"(ret) : "Nd"(port) : "memory");
    return ret;
}

void hal_outw(uint16_t port, uint16_t val) {
    __asm__ volatile ( "outw %w0, %w1" : : "a"(val), "Nd"(port) : "memory");
}

void hal_insw(uint16_t port, void* addr, uint32_t count) {
    __asm__ volatile ( "cld; rep insw" : "+D" (addr), "+c" (count) : "d" (port) : "memory" );
}

void hal_outsw(uint16_t port, const void* addr, uint32_t count) {
    __asm__ volatile ( "cld; rep outsw" : "+S" (addr), "+c" (count) : "d" (port) : "memory" );
}

void hal_wrmsr(uint32_t msr, uint64_t value) {
    uint32_t low = value & 0xFFFFFFFF;
    uint32_t high = value >> 32;
    asm volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

uint64_t hal_rdmsr(uint32_t msr) {
    uint32_t low, high;
    asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static uint8_t read_cmos(uint8_t reg) {
    hal_outb(0x70, reg);
    return hal_inb(0x71);
}

static uint8_t is_bcd_mode() {
    hal_outb(0x70, 0x0B);
    return !(hal_inb(0x71) & 0x04);
}

uint64_t hal_get_boot_time(void) {
    uint64_t irq = hal_irq_save();
    uint8_t sec, min, hour, day, month, year;
    
    while (1) {
        hal_outb(0x70, 0x0A);
        if (!(hal_inb(0x71) & 0x80)) break;
    }
    
    sec   = read_cmos(0x00);
    min   = read_cmos(0x02);
    hour  = read_cmos(0x04);
    day   = read_cmos(0x07);
    month = read_cmos(0x08);
    year  = read_cmos(0x09);
    
    if (is_bcd_mode()) {
        sec   = bcd_to_bin(sec);
        min   = bcd_to_bin(min);
        hour  = bcd_to_bin(hour);
        day   = bcd_to_bin(day);
        month = bcd_to_bin(month);
        year  = bcd_to_bin(year);
    }
    
    hal_irq_restore(irq);
    return rtc_to_unix(sec, min, hour, day, month, year);
}

void hal_enable_interrupts(void) {
    asm volatile("sti");
}

void hal_disable_interrupts(void) {
    asm volatile("cli");
}

__attribute__((noreturn)) void hal_halt(void) {
    while (1) {
        asm volatile("cli; hlt");
    }
}

void hal_debug_print_early(const char* str) {
    volatile uint16_t* vga_buffer = (volatile uint16_t*)0xB8000;
    for(int i = 0; i < 80 * 25; i++) {
        vga_buffer[i] = 0x4F00; 
    }
    int i = 0;
    while(str[i]) {
        vga_buffer[i] = (uint16_t)str[i] | 0x4F00;
        i++;
    }
}

void hal_debug_pause(void) {
    while ((hal_inb(0x64) & 1) == 0) {
        hal_cpu_relax();
    }
    hal_inb(0x60);
}