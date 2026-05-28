#ifndef PART_INTERFACE_H
#define PART_INTERFACE_H

#include "disk_interface.h"

typedef void (*part_found_cb_t)(block_dev_t* raw_disk, uint8_t part_id, uint64_t start_lba, uint64_t size_sectors, int is_bootable, void* context);

typedef struct {
    const char* name;
    
    int (*parse)(block_dev_t* raw_disk, part_found_cb_t callback, void* context);
} part_driver_t;

#endif /* PART_INTERFACE_H */