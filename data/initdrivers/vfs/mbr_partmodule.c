#include <aoslib.h>
#include <vfs/part_interface.h>

typedef struct {
    uint8_t  status;
    uint8_t  chs_first[3];
    uint8_t  type;
    uint8_t  chs_last[3];
    uint32_t lba_start;
    uint32_t sectors;
} __attribute__((packed)) mbr_entry_t;

static void parse_ebr(block_dev_t* raw_disk, uint64_t ext_base_lba, uint64_t ebr_lba, part_found_cb_t callback, void* context) {
    uint8_t sector[512];
    
    if (block_read(raw_disk, ebr_lba, 1, sector) != 0) return;
    
    if (sector[510] != 0x55 || sector[511] != 0xAA) return;

    mbr_entry_t* parts = (mbr_entry_t*)&sector[446];

    if (parts[0].type != 0 && parts[0].sectors != 0) {
        uint64_t logic_start = ebr_lba + parts[0].lba_start;
        callback(raw_disk, 0, logic_start, parts[0].sectors, 0, context);
    }

    if (parts[1].type == 0x05 || parts[1].type == 0x0F) {
        uint64_t next_ebr = ext_base_lba + parts[1].lba_start;
        parse_ebr(raw_disk, ext_base_lba, next_ebr, callback, context);
    }
}

static int mbr_parse(block_dev_t* raw_disk, part_found_cb_t callback, void* context) {
    uint8_t sector[512];
    
    if (block_read(raw_disk, 0, 1, sector) != 0) return 0;
    if (sector[510] != 0x55 || sector[511] != 0xAA) return 0;
    
    mbr_entry_t* parts = (mbr_entry_t*)&sector[446];
    if (parts[0].type == 0xEE) return 0;
    
    int found_any = 0;
    for (int i = 0; i < 4; i++) {
        if (parts[i].type == 0 || parts[i].sectors == 0) continue;
        found_any = 1;
        
        if (parts[i].type == 0x05 || parts[i].type == 0x0F) {
            parse_ebr(raw_disk, parts[i].lba_start, parts[i].lba_start, callback, context);
        } else {
            callback(raw_disk, i, parts[i].lba_start, parts[i].sectors, (parts[i].status == 0x80), context);
        }
    }
    return found_any; 
}

part_driver_t mbr_part_driver = { .name = "mbr", .parse = mbr_parse };