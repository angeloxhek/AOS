#include "hal_arch.h"

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

void hal_cpu_relax(void) {
    asm volatile("pause" ::: "memory");
}