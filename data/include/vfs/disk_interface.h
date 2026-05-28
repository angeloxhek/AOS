#ifndef DISK_INTERFACE_H
#define DISK_INTERFACE_H

#include <stdint.h>

typedef void* disk_instance_t;
typedef struct disk_driver disk_driver_t;

typedef struct {
    disk_driver_t* driver;
    disk_instance_t disk_inst;
    
    uint32_t disk_id;
    uint64_t partition_offset_lba;
    uint64_t size_sectors;
    uint32_t sector_size;
} block_dev_t;

struct disk_driver {
    const char* name;
    
    int (*init)(void); 
    
    disk_instance_t (*get_disk)(int index, uint64_t* out_total_sectors, uint32_t* out_sector_size);
    
    int (*read)(disk_instance_t disk, uint64_t lba, uint64_t count, void* buf);
    int (*write)(disk_instance_t disk, uint64_t lba, uint64_t count, const void* buf);
};

static inline int block_read(block_dev_t* dev, uint64_t lba, uint64_t count, void* buf) {
    uint64_t abs_lba = dev->partition_offset_lba + lba;
    return dev->driver->read(dev->disk_inst, abs_lba, count, buf);
}

static inline int block_write(block_dev_t* dev, uint64_t lba, uint64_t count, const void* buf) {
    uint64_t abs_lba = dev->partition_offset_lba + lba;
    return dev->driver->write(dev->disk_inst, abs_lba, count, buf);
}

#endif /* DISK_INTERFACE_H */