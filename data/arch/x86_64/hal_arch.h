#ifndef HAL_ARCH_H
#define HAL_ARCH_H

#include <stdint.h>
#include <kernel/internal.h>

#define ATA_CMD_READ_PIO_EXT    0x24
#define ATA_CMD_WRITE_PIO_EXT   0x34
#define ATA_CMD_CACHE_FLUSH_EXT 0xEA

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
    uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp, r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed)) registers_t;

typedef struct {
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
    uint64_t rax;
    uint64_t rcx;
    uint64_t r11;
    uint64_t rsp;
} syscall_regs_t;

struct idt_entry {
    uint16_t base_low;
    uint16_t sel;
    uint8_t  ist;
    uint8_t  flags;
    uint16_t base_mid;
    uint32_t base_high;
    uint32_t reserved;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
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

struct tss_entry_t {
    uint32_t reserved1;
    uint64_t rsp0;
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
    uint16_t iopb_offset;
    uint8_t  io_bitmap[8192];
    uint8_t  iopb_end;
} __attribute__((packed));

typedef struct {
    uint64_t user_rsp_scratch;
    uint64_t kernel_rsp;
    uint64_t reserved[3];
    uint64_t canary;
} __attribute__((packed)) kernel_tcb_t;

#define KERNEL_BASE    0xFFFFFFFF80000000
#define P2V(phys)      ((uint64_t)(phys) + KERNEL_BASE)
#define V2P(virt)      ((uint64_t)(virt) - KERNEL_BASE)
#define PAGE_PRESENT   0x1
#define PAGE_WRITE     0x2
#define PAGE_USER      0x4
#define PAGE_FRAME     0x000FFFFFFFFFF000
#define PAGE_MASK      0xFFFFFFFFFFFFF000
#define PHYS_PML4      0x80000

#define PHYS_ASM_PDPT   0x81000
#define PHYS_ASM_PD     0x82000
#define PHYS_HHDM_PDPT  0x84000
#define PHYS_HHDM_PD    0x85000

#endif