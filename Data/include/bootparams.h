#ifndef BOOTPARAMS_H
#define BOOTPARAMS_H

#include <stdint.h>

#define BOOT_MAGIC 0xB007CAFE
#define BOOT_VERSION 1

typedef enum {
    BOOT_TYPE_UNKNOWN = 0,
    BOOT_TYPE_MBR     = 1,
    BOOT_TYPE_UEFI    = 2
} boot_type_t;

/* --- 1. ОБЩИЕ СТРУКТУРЫ (COMMON) --- */

/* Видео (FrameBuffer) - одинаково нужно всем */
typedef struct {
    uint64_t framebuffer_addr;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint8_t  red_mask_size;
    uint8_t  red_mask_shift;
    uint8_t  green_mask_size;
    uint8_t  green_mask_shift;
    uint8_t  blue_mask_size;
    uint8_t  blue_mask_shift;
} __attribute__((packed)) boot_video_t; // Размер: 30 байт

/* Контейнер карты памяти */
typedef struct {
    uint64_t map_addr;       // Адрес буфера
    uint64_t map_size;       // Размер буфера в байтах
    uint32_t desc_size;      // Размер одного дескриптора
    uint32_t desc_version;   // Версия (для UEFI)
} __attribute__((packed)) boot_mmap_t; // Размер: 24 байта

/* --- 2. СПЕЦИФИЧНЫЕ ПОЛЯ (SPECIFIC) --- */

/* Только для BIOS/MBR */
typedef struct {
    uint8_t  drive_num;      // Например, 0x80
    uint8_t  reserved[15];   // Выравнивание (опционально)
} __attribute__((packed)) mbr_info_t;

/* Только для UEFI */
typedef struct {
    uint64_t system_table;   // EFI_SYSTEM_TABLE*
    uint64_t runtime_services;// EFI_RUNTIME_SERVICES*
    uint64_t image_handle;   // EFI_HANDLE
} __attribute__((packed)) uefi_info_t;

/* --- 3. ГЛАВНАЯ СТРУКТУРА --- */

#define BOOT_FLAG_VIDEO_PRESENT  (1 << 0) // Графика инициализирована (LFB)
#define BOOT_FLAG_ACPI_PRESENT   (1 << 1) // ACPI найден
#define BOOT_FLAG_SMBIOS_PRESENT (1 << 2) // SMBIOS найден

typedef struct {
    // [0] Заголовок
    uint32_t magic;           // 0xB007CAFE
    uint32_t version;         // Версия структуры
    uint8_t  type;            // boot_type_t

    // [9] Общие данные
    boot_video_t video;       // Смещение 9, Размер 30
    boot_mmap_t  mmap;        // Смещение 39, Размер 24
    uint64_t acpi_rsdp;       // Offset 63. Адрес структуры ACPI
    uint64_t smbios_entry;    // Offset 71. Адрес структуры SMBIOS
    uint64_t kernel_size;     // Offset 79. Размер загруженного ядра в байтах
	uint32_t flags;

    // [91] Специфичные данные (Union)
    union {
        mbr_info_t  mbr;
        uefi_info_t uefi;
    } specific;               // Смещение 91

} __attribute__((packed)) boot_info_t;

#endif