#include <kernel/internal.h>

#define COM1_PORT 0x3F8

void serial_init() {
    hal_outb(COM1_PORT + 1, 0x00);    // Отключаем прерывания
    hal_outb(COM1_PORT + 3, 0x80);    // Включаем DLAB (установка baud rate)
    hal_outb(COM1_PORT + 0, 0x03);    // 38400 baud (делитель 3)
    hal_outb(COM1_PORT + 1, 0x00);    // (старший байт делителя)
    hal_outb(COM1_PORT + 3, 0x03);    // 8 бит, нет четности, 1 стоп-бит
    hal_outb(COM1_PORT + 2, 0xC7);    // Включаем FIFO, очищаем буферы
    hal_outb(COM1_PORT + 4, 0x0B);    // Включаем IRQ (опционально)
}

int serial_is_transmit_empty() {
    return hal_inb(COM1_PORT + 5) & 0x20;
}

void serial_putchar(char a) {
    while (serial_is_transmit_empty() == 0);
    hal_outb(COM1_PORT, a);
}

void serial_print(const char* str) {
    while (*str) {
        serial_putchar(*str++);
    }
}