#include <aoslib.h>
#include <vfs/part_interface.h>

#define BSD_MAGIC 0x82564557

typedef struct {
    uint32_t p_size;
    uint32_t p_offset;
    uint32_t p_fsize;
    uint8_t  p_fstype;
    uint8_t  p_frag;
    uint16_t p_cpg;
} __attribute__((packed)) bsd_partition_t;

typedef struct {
    uint32_t d_magic;
    uint8_t  reserved1[128];
    uint32_t d_magic2;
    uint16_t d_checksum;
    uint16_t d_npartitions;
    uint32_t d_bbsize;
    uint32_t d_sbsize;
    bsd_partition_t d_partitions[8];
} __attribute__((packed)) bsd_disklabel_t;

typedef struct {
    uint8_t  status;
    uint8_t  chs_first[3];
    uint8_t  type;
    uint8_t  chs_last[3];
    uint32_t lba_start;
    uint32_t sectors;
} __attribute__((packed)) mbr_entry_t;

static int bsd_parse(block_dev_t* raw_disk, part_found_cb_t callback, void* context) {
    uint8_t sector[512];
    
    if (block_read(raw_disk, 0, 1, sector) != 0) return 0;
    if (sector[510] != 0x55 || sector[511] != 0xAA) return 0;
    
    mbr_entry_t* parts = (mbr_entry_t*)&sector[446];
    uint64_t bsd_slice_lba = 0;
    
    for (int i = 0; i < 4; i++) {
        if (parts[i].type == 0xA5 || parts[i].type == 0xA6 || parts[i].type == 0xA9) {
            bsd_slice_lba = parts[i].lba_start;
            break;
        }
    }
    
    if (bsd_slice_lba == 0) return 0; 

    if (block_read(raw_disk, bsd_slice_lba + 1, 1, sector) != 0) return 0;

    bsd_disklabel_t* label = (bsd_disklabel_t*)sector;
    if (label->d_magic != BSD_MAGIC || label->d_magic2 != BSD_MAGIC) return 0;

    uint16_t num_parts = label->d_npartitions;
    if (num_parts > 8) num_parts = 8; 

    int found_any = 0;
    for (int i = 0; i < num_parts; i++) {
        bsd_partition_t* p = &label->d_partitions[i];
        if (i == 2 || p->p_size == 0 || p->p_fstype == 0) continue;

        found_any = 1;
        callback(raw_disk, i, p->p_offset, p->p_size, 0, context);
    }
    return found_any;
}

part_driver_t bsd_part_driver = { .name = "bsd", .parse = bsd_parse };