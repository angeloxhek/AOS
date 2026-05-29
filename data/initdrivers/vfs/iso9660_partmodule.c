#include <aoslib.h>
#include <vfs/part_interface.h>

static int iso9660_parse(block_dev_t* raw_disk, part_found_cb_t callback, void* context) {
    uint32_t sec_size = raw_disk->sector_size;
    if (sec_size < 2048) sec_size = 2048;

    uint8_t* sector = malloc(sec_size);
    if (!sector) return 0;

    int is_iso = 0;
    uint32_t boot_catalog_lba = 0;

    for (uint64_t lba = 16; lba < 32; lba++) {
        if (block_read(raw_disk, lba, 1, sector) != 0) break;

        if (sector[1] != 'C' || sector[2] != 'D' || sector[3] != '0' ||
            sector[4] != '0' || sector[5] != '1') {
            continue; 
        }

        is_iso = 1;

        if (sector[0] == 0) {
            if (sector[7] == 'E' && sector[8] == 'L' && sector[9] == ' ' && sector[10] == 'T') {
                boot_catalog_lba = *(uint32_t*)&sector[0x47];
            }
        }
        if (sector[0] == 255) break;
    }

    if (!is_iso) {
        free(sector);
        return 0;
    }

    callback(raw_disk, 0, 0, raw_disk->size_sectors, 0, context);

    if (boot_catalog_lba != 0) {
        if (block_read(raw_disk, boot_catalog_lba, 1, sector) == 0) {
            uint8_t boot_indicator = sector[32]; 
            uint8_t media_type = sector[33] & 0x0F;
            uint32_t image_lba = *(uint32_t*)&sector[40];

            if (boot_indicator == 0x88 && image_lba != 0) {
                uint64_t image_sectors = 0;
                switch (media_type) {
                    case 1: image_sectors = (1200 * 1024) / 2048; break; 
                    case 2: image_sectors = (1440 * 1024) / 2048; break; 
                    case 3: image_sectors = (2880 * 1024) / 2048; break; 
                    default: image_sectors = 32768; break; 
                }

                if (image_sectors > 0) {
                    callback(raw_disk, 1, image_lba, image_sectors, 1, context); 
                }
            }
        }
    }

    free(sector);
    return 1;
}

part_driver_t iso9660_part_driver = { .name = "iso", .parse = iso9660_parse };