#ifndef PART_INTERFACE_H
#define PART_INTERFACE_H

#include "disk_interface.h"

typedef enum {
	PART_BOOT_UNKNOWN = 0,
	PART_BOOT_MBR,
	PART_BOOT_GPT
} part_boot_type_t;

typedef struct {
    part_boot_type_t type;
    union {
        struct {
            uint64_t part_lba;
            uint32_t drive_sign;
        } mbr;
        struct {
            uint8_t guid[16];
        } gpt;
    } specific;
} part_boot_desc_t;

typedef void (*part_found_cb_t)(block_dev_t* raw_disk, uint8_t part_id, uint64_t start_lba, uint64_t size_sectors, int is_bootable, void* context);

typedef struct {
    const char* name;
    
    int (*parse)(block_dev_t* raw_disk, const part_boot_desc_t* boot_desc, part_found_cb_t callback, void* context);
} part_driver_t;

#endif /* PART_INTERFACE_H */