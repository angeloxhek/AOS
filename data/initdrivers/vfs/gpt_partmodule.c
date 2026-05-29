#include "../include/aoslib.h"
#include "../include/vfs/part_interface.h"

typedef struct {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t header_crc32;
    uint32_t reserved;
    uint64_t my_lba;
    uint64_t alternate_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint8_t  disk_guid[16];
    uint64_t partition_entry_lba;
    uint32_t num_partition_entries;
    uint32_t sizeof_partition_entry;
    uint32_t partition_entry_array_crc32;
} __attribute__((packed)) gpt_header_t;

typedef struct {
    uint8_t  partition_type_guid[16];
    uint8_t  unique_partition_guid[16];
    uint64_t starting_lba;
    uint64_t ending_lba;
    uint64_t attributes;
    uint16_t partition_name[36];
} __attribute__((packed)) gpt_entry_t;

static uint32_t gpt_crc32(const uint8_t *data, uint32_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
        }
    }
    return ~crc;
}

static int gpt_parse(block_dev_t* raw_disk, part_found_cb_t callback, void* context) {
    uint32_t sec_size = raw_disk->sector_size;
    if (sec_size < 512) sec_size = 512;

    uint8_t* header_sec = malloc(sec_size);
    if (!header_sec) return 0;

    if (block_read(raw_disk, 1, 1, header_sec) != 0) {
        free(header_sec);
        return 0;
    }

    gpt_header_t* hdr = (gpt_header_t*)header_sec;
    
    if (hdr->signature != 0x5452415020494645ULL) {
        free(header_sec);
        return 0;
    }

    uint32_t saved_crc = hdr->header_crc32;
    hdr->header_crc32 = 0;
    uint32_t calc_crc = gpt_crc32(header_sec, hdr->header_size);
    if (saved_crc != calc_crc) {
        free(header_sec);
        return 0; 
    }

    uint64_t entry_lba = hdr->partition_entry_lba;
    uint32_t num_entries = hdr->num_partition_entries;
    uint32_t entry_size = hdr->sizeof_partition_entry;

    if (entry_size < sizeof(gpt_entry_t) || num_entries > 1024) {
        free(header_sec);
        return 0;
    }

    uint8_t* entry_sec = malloc(sec_size);
    if (!entry_sec) {
        free(header_sec);
        return 0;
    }

    uint32_t entries_per_sector = sec_size / entry_size;
    uint32_t current_entry = 0;
    uint8_t part_id_counter = 0;

    while (current_entry < num_entries) {
        if (block_read(raw_disk, entry_lba, 1, entry_sec) != 0) break;

        for (uint32_t i = 0; i < entries_per_sector && current_entry < num_entries; i++) {
            gpt_entry_t* entry = (gpt_entry_t*)(entry_sec + (i * entry_size));

            int is_empty = 1;
            for (int j = 0; j < 16; j++) {
                if (entry->partition_type_guid[j] != 0) {
                    is_empty = 0; 
                    break;
                }
            }

            if (!is_empty) {
                uint64_t start_lba = entry->starting_lba;
                uint64_t end_lba = entry->ending_lba;
                uint64_t size_sectors = (end_lba - start_lba) + 1;
                
                int is_bootable = (entry->attributes & (1ULL << 2)) ? 1 : 0;

                callback(raw_disk, part_id_counter, start_lba, size_sectors, is_bootable, context);
                part_id_counter++;
            }
            
            current_entry++;
        }
        entry_lba++;
    }

    free(entry_sec);
    free(header_sec);
    
    return 1;
}

part_driver_t gpt_part_driver = {
    .name = "gpt",
    .parse = gpt_parse
};