#include "include/kernel_internal.h"

// -------------------------
//        Storage / FS
// -------------------------

void get_drv_device_name(ide_device_t* device, char* buff) { 
    buff[0] = 'd';
    buff[1] = 'r';
    buff[2] = 'v';
    char buf[21];
    uint64_to_dec(device->id, buf);
    for (int i = 0; i < 20; i++) {
        buff[3 + i] = buf[i];
        if (buf[i] == 0) break;
    }
}

void get_volume_name(volume_t* v, char* buff) { 
    buff[0] = 'p';
    char buf[21];
    uint64_to_dec(v->id, buf);
    for (int i = 0; i < 20; i++) {
        buff[1 + i] = buf[i];
        if (buf[i] == 0) break;
    }
}

void mbr_mount_all_partitions(ide_device_t* dev, uint8_t is_boot_device) {
    uint8_t sector[512];
    
    hal_disk_read(dev->id, 0, 1, sector);

    for (int i = 0; i < 4; i++) {
        uint32_t entry_offset = 0x1BE + (i * 16);
        uint8_t status = sector[entry_offset];
        uint32_t lba_start = *(uint32_t*)&sector[entry_offset + 8];
        uint32_t total_sectors = *(uint32_t*)&sector[entry_offset + 12];

        if (total_sectors == 0) continue;

        uint8_t vbr[512];
        hal_disk_read(dev->id, lba_start, 1, vbr);
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
    
    hal_disk_read(vol->device.id, lba, 1, buffer);
    
    uint32_t val = *(uint32_t*)&buffer[ent_offset];
    return val & 0x0FFFFFFF;
}