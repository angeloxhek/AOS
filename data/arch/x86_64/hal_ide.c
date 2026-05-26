#include "hal_arch.h"

extern ide_device_t mounted_ides[];
extern int ide_count;
extern volume_t* system_volume;
extern ide_device_t* system_ide;
extern int volume_count;
extern void mbr_mount_all_partitions(ide_device_t* dev, uint8_t is_boot_device);
extern int sleep_while_zero(int (*condition)(void*), void* arg, uint64_t timeout, int* out_res);
#ifndef ATA_CMD_READ_PIO_EXT
#define ATA_CMD_READ_PIO_EXT 0x24
#define ATA_CMD_WRITE_PIO_EXT 0x34
#define ATA_CMD_CACHE_FLUSH_EXT 0xEA
#endif


static int ide_wait_ready(void* dev_ptr) {
    ide_device_t* dev = (ide_device_t*)dev_ptr;
    return (hal_inb(dev->io_base + 7) & 0x80) ? 0 : 1;
}

static int ide_wait_drq(void* dev_ptr) {
    ide_device_t* dev = (ide_device_t*)dev_ptr;
    uint8_t status = hal_inb(dev->io_base + 7);
    if (status & 0x01) return 2; // Error
    if (!(status & 0x80) && (status & 0x08)) return 1; // Ready
    return 0; // Wait
}

static int ide_identify(ide_device_t* dev) {
    hal_outb(dev->io_base + 6, dev->drive_select);
    hal_outb(dev->io_base + 2, 0);
    hal_outb(dev->io_base + 3, 0);
    hal_outb(dev->io_base + 4, 0);
    hal_outb(dev->io_base + 5, 0);
    hal_outb(dev->io_base + 7, 0xEC);
    
    uint8_t status = hal_inb(dev->io_base + 7);
    if (status == 0) return 0;
    if (!sleep_while_zero(ide_wait_ready, (void*)dev, 5000, 0)) return 0;
    if (hal_inb(dev->io_base + 4) != 0 || hal_inb(dev->io_base + 5) != 0) return 0;
    
    int res = 0;
    if (!sleep_while_zero(ide_wait_drq, (void*)dev, 5000, &res) || res == 2) {
        return 0;
    }
    
    for (int i = 0; i < 256; i++) {
        hal_inw(dev->io_base);
    }
    return 1;
}

int hal_disk_read(uint32_t drive_id, uint64_t lba, uint16_t count, uint8_t* buffer) {
    ide_device_t* dev = &mounted_ides[drive_id];
    if (!sleep_while_zero(ide_wait_ready, (void*)dev, 5000, 0)) return 0;
    
    uint16_t io = dev->io_base;
    uint8_t slave_bit = (dev->drive_select & 0x10);
    
    hal_outb(io + 6, 0x40 | slave_bit);
    hal_outb(io + 2, (uint8_t)(count >> 8));
    hal_outb(io + 2, (uint8_t)count);
    hal_outb(io + 3, (uint8_t)(lba >> 24));
    hal_outb(io + 3, (uint8_t)lba);
    hal_outb(io + 4, (uint8_t)(lba >> 32));
    hal_outb(io + 4, (uint8_t)(lba >> 8));
    hal_outb(io + 5, (uint8_t)(lba >> 40));
    hal_outb(io + 5, (uint8_t)(lba >> 16));
    hal_outb(io + 7, ATA_CMD_READ_PIO_EXT);
    
    int res = 0;
    for (int i = 0; i < count; i++) {
        if (!sleep_while_zero(ide_wait_drq, (void*)dev, 5000, &res) || res == 2) {
            return 0;
        }
        hal_insw(io, buffer + (i * 512), 256);
    }
    return 1;
}

int hal_disk_write(uint32_t drive_id, uint64_t lba, uint16_t count, const uint8_t* buffer) {
    ide_device_t* dev = &mounted_ides[drive_id];
    if (!sleep_while_zero(ide_wait_ready, (void*)dev, 5000, 0)) return 0;

    uint16_t io = dev->io_base;
    uint8_t slave_bit = (dev->drive_select & 0x10);

    hal_outb(io + 6, 0x40 | slave_bit);
    hal_outb(io + 2, (uint8_t)(count >> 8));
    hal_outb(io + 2, (uint8_t)count);
    hal_outb(io + 3, (uint8_t)(lba >> 24));
    hal_outb(io + 3, (uint8_t)lba);
    hal_outb(io + 4, (uint8_t)(lba >> 32));
    hal_outb(io + 4, (uint8_t)(lba >> 8));
    hal_outb(io + 5, (uint8_t)(lba >> 40));
    hal_outb(io + 5, (uint8_t)(lba >> 16));
    hal_outb(io + 7, ATA_CMD_WRITE_PIO_EXT);

    int res = 0;
    for (int i = 0; i < count; i++) {
        if (!sleep_while_zero(ide_wait_drq, (void*)dev, 5000, &res) || res == 2) {
            return 0;
        }
        hal_outsw(io, buffer + (i * 512), 256);
    }

    hal_outb(io + 7, ATA_CMD_CACHE_FLUSH_EXT);
    if (!sleep_while_zero(ide_wait_ready, (void*)dev, 5000, 0)) return 0;
    return 1;
}

void hal_storage_init(uint8_t boot_drive_id) {
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

    if (!system_volume) {
        kernel_error(0x2, boot_drive_id, ide_count, volume_count, 0);
    }
}