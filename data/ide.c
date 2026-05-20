#include "include/kernel_internal.h"

// -------------------------
//           IDE
// -------------------------

void get_ide_device_name(ide_device_t* device, char* buff) { // buff >= 24 bytes
    buff[0] = 'i';
    buff[1] = 'd';
    buff[2] = 'e';

    char buf[21];
    uint64_to_dec(device->id, buf);

    for (int i = 0; i < 20; i++) {
        buff[3 + i] = buf[i];
        if (buf[i] == 0) {
            break;
        }
    }
}

void get_volume_name(volume_t* v, char* buff) { // buff >= 22 bytes
    buff[0] = 'p';

    char buf[21];
    uint64_to_dec(v->id, buf);

    for (int i = 0; i < 20; i++) {
        buff[1 + i] = buf[i];
        if (buf[i] == 0) {
            break;
        }
    }
}

int ide_identify(ide_device_t* dev) {
    outb(dev->io_base + 6, dev->drive_select);
    outb(dev->io_base + 2, 0);
    outb(dev->io_base + 3, 0);
    outb(dev->io_base + 4, 0);
    outb(dev->io_base + 5, 0);
    outb(dev->io_base + 7, 0xEC);
    uint8_t status = inb(dev->io_base + 7);
    if (status == 0) return 0;
    if (!sleep_while_zero(ide_wait_ready, (void*)dev, 5000, 0)) return 0;
    if (inb(dev->io_base + 4) != 0 || inb(dev->io_base + 5) != 0) return 0;
    int res = 0;
    if (!sleep_while_zero(ide_wait_drq, (void*)dev, 5000, &res) || res == 2) {
        return 0;
    }
    for (int i = 0; i < 256; i++) {
        inw(dev->io_base);
    }
    return 1;
}

int ide_wait_ready(void* dev) {
    return (inb(((ide_device_t*)dev)->io_base + 7) & 0x80) ? 0 : 1;
}

int ide_wait_drq(void* dev) {
    uint8_t status = inb(((ide_device_t*)dev)->io_base + 7);
    if (status & 0x01) return 2;
    if (!(status & 0x80) && (status & 0x08)) return 1;
    return 0;
}

int ide_read_sectors(ide_device_t* dev, uint64_t lba, uint16_t count, uint8_t* buffer) {
    if (!sleep_while_zero(ide_wait_ready, (void*)dev, 5000, 0)) return 0;
    uint16_t io = dev->io_base;
    uint8_t slave_bit = (dev->drive_select & 0x10);
    outb(io + 6, 0x40 | slave_bit);
    outb(io + 2, (uint8_t)(count >> 8));
    outb(io + 2, (uint8_t)count);
    outb(io + 3, (uint8_t)(lba >> 24));
    outb(io + 3, (uint8_t)lba);
    outb(io + 4, (uint8_t)(lba >> 32));
    outb(io + 4, (uint8_t)(lba >> 8));
    outb(io + 5, (uint8_t)(lba >> 40));
    outb(io + 5, (uint8_t)(lba >> 16));
    outb(io + 7, ATA_CMD_READ_PIO_EXT);
    int res = 0;
    for (int i = 0; i < count; i++) {
        if (!sleep_while_zero(ide_wait_drq, (void*)dev, 5000, &res) || res == 2) {
            return 0;
        }
        insw(io, buffer + (i * 512), 256);
    }
    return 1;
}

int ide_read_sector(ide_device_t* dev, uint64_t lba, uint8_t* buffer) {
    return ide_read_sectors(dev, lba, 1, buffer);
}

int ide_write_sectors(ide_device_t* dev, uint64_t lba, uint16_t count, const uint8_t* buffer) {
    if (!sleep_while_zero(ide_wait_ready, (void*)dev, 5000, 0)) return 0;

    uint16_t io = dev->io_base;
    uint8_t slave_bit = (dev->drive_select & 0x10);

    outb(io + 6, 0x40 | slave_bit);

    outb(io + 2, (uint8_t)(count >> 8));
    outb(io + 2, (uint8_t)count);
    outb(io + 3, (uint8_t)(lba >> 24));
    outb(io + 3, (uint8_t)lba);
    outb(io + 4, (uint8_t)(lba >> 32));
    outb(io + 4, (uint8_t)(lba >> 8));
    outb(io + 5, (uint8_t)(lba >> 40));
    outb(io + 5, (uint8_t)(lba >> 16));

    outb(io + 7, ATA_CMD_WRITE_PIO_EXT);

    int res = 0;
    for (int i = 0; i < count; i++) {
        if (!sleep_while_zero(ide_wait_drq, (void*)dev, 5000, &res) || res == 2) {
            return 0;
        }
        outsw(io, buffer + (i * 512), 256);
    }

    outb(io + 7, ATA_CMD_CACHE_FLUSH_EXT);
    if (!sleep_while_zero(ide_wait_ready, (void*)dev, 5000, 0)) return 0;

    return 1;
}

int ide_write_sector(ide_device_t* dev, uint64_t lba, const uint8_t* buffer) {
    return ide_write_sectors(dev, lba, 1, buffer);
}

// ----------------------------
//         File System
// ----------------------------

void mbr_mount_all_partitions(ide_device_t* dev, uint8_t is_boot_device) {
    uint8_t sector[512];

    ide_read_sector(dev, 0, sector);

    for (int i = 0; i < 4; i++) {
        uint32_t entry_offset = 0x1BE + (i * 16);
        uint8_t status = sector[entry_offset];
        uint32_t lba_start = *(uint32_t*)&sector[entry_offset + 8];
        uint32_t total_sectors = *(uint32_t*)&sector[entry_offset + 12];

        if (total_sectors == 0) continue;

        uint8_t vbr[512];
        ide_read_sector(dev, lba_start, vbr);
        struct fat32_bpb* bpb = (struct fat32_bpb*)vbr;

        if (vbr[0x52] == 'F' && vbr[0x53] == 'A' && vbr[0x54] == 'T') {
            if (volume_count < MAX_VOLUMES) {
                volume_t* v = &mounted_volumes[volume_count];
                v->device = *dev;
                v->partition_lba = lba_start;
                v->root_cluster = bpb->root_clus;
                v->sec_per_clus = bpb->sec_per_clus;
                v->fat_lba = lba_start + bpb->reserved_sec_cnt;
                v->data_lba = v->fat_lba + (bpb->fat_sz_32 * bpb->num_fats);
                v->active = (status == 0x80);
                v->id = volume_count;

                if (is_boot_device && v->active) {
                    system_volume = v;
                }

                volume_count++;
            }
        }
    }
}

void mbr_storage_init(uint8_t boot_drive_id) {
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

    if (!system_volume) kernel_error(0x2, boot_drive_id, ide_count, volume_count, 0);
}

uint64_t cluster_to_lba(volume_t* vol, uint32_t cluster) {
    uint64_t offset = (uint64_t)(cluster - 2) * (uint64_t)vol->sec_per_clus;
    return vol->data_lba + offset;
}

uint32_t get_next_cluster(volume_t* vol, uint32_t current_cluster) {
    uint8_t buffer[512];
    uint64_t fat_offset = (uint64_t)current_cluster * 4;
    uint64_t fat_sector = fat_offset / 512;
    uint64_t ent_offset = fat_offset % 512;
    uint64_t lba = vol->fat_lba + fat_sector;
    ide_read_sector(&vol->device, lba, buffer);
    uint32_t val = *(uint32_t*)&buffer[ent_offset];
    return val & 0x0FFFFFFF;
}
