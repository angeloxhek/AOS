#include <stdint.h>
#define AOSLIB_SYSCALLS
#define AOSLIB_STRING
#include "../include/aoslib.h"
#include "../include/fs_interface.h"

#define FAT_ATTR_READ_ONLY 0x01
#define FAT_ATTR_HIDDEN    0x02
#define FAT_ATTR_SYSTEM    0x04
#define FAT_ATTR_VOLUME_ID 0x08
#define FAT_ATTR_DIRECTORY 0x10
#define FAT_ATTR_ARCHIVE   0x20
#define FAT_ATTR_LFN       0x0F

typedef struct {
    uint8_t  jmp[3];
    uint8_t  oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  fats_count;
    uint16_t root_entries_count;
    uint16_t total_sectors_16;
    uint8_t  media_descriptor;
    uint16_t sectors_per_fat_16;
    uint16_t sectors_per_track;
    uint16_t heads_count;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint32_t sectors_per_fat_32;
    uint16_t flags;
    uint16_t fat_version;
    uint32_t root_cluster;
    uint16_t fs_info_sector;
    uint16_t backup_boot_sector;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_signature;
    uint32_t volume_id;
    char     volume_label[11];
    char     fs_type[8];
} __attribute__((packed)) fat32_bpb_t;

typedef struct fat32_dir_entry {
    char     name[11];
    uint8_t  attr;
    uint8_t  nt_res;
    uint8_t  crt_time_tenth;
    uint16_t crt_time;
    uint16_t crt_date;
    uint16_t lst_acc_date;
    uint16_t cluster_high;
    uint16_t wrt_time;
    uint16_t wrt_date;
    uint16_t cluster_low;
    uint32_t file_size;
} __attribute__((packed)) fat32_dir_entry_t;

typedef struct {
    uint8_t  order;
    uint16_t name1[5];
    uint8_t  attr;
    uint8_t  type;
    uint8_t  checksum;
    uint16_t name2[6];
    uint16_t fst_clus_lo;
    uint16_t name3[2];
} __attribute__((packed)) fat32_lfn_entry_t;

typedef struct {
    uint32_t lead_sig;       // 0x41615252
    uint8_t  reserved1[480];
    uint32_t struct_sig;     // 0x61417272
    uint32_t free_count;
    uint32_t next_free;
    uint8_t  reserved2[12];
    uint32_t trail_sig;      // 0xAA550000
} __attribute__((packed)) fat32_fsinfo_t;

#define FAT32_CACHE_ENTRIES 256  // 128 KB
#define FAT32_CACHE_MASK    (FAT32_CACHE_ENTRIES - 1)
#define FAT32_MAX_SECTOR_SIZE 4096

typedef struct {
    uint64_t lba;
    uint8_t  data[FAT32_MAX_SECTOR_SIZE];
} fat32_cache_entry_t;

typedef struct {
    fat32_cache_entry_t entries[FAT32_CACHE_ENTRIES];
} fat32_cache_t;

typedef struct {
    block_dev_t* dev;
    uint32_t root_cluster;
    uint32_t sectors_per_cluster;
    uint32_t bytes_per_sector;
    uint32_t fat_start_lba;
    uint32_t data_start_lba;
	uint32_t total_clusters;
	fat32_cache_t* cache;
	uint16_t fs_info_sector;
	uint32_t next_free_cluster_hint;
	mutex_t cache_lock;
} fat32_instance_t;

typedef struct {
    uint32_t first_cluster;
    uint64_t size_bytes;
    uint32_t current_cluster;
    uint64_t current_offset;
    uint8_t  attributes;
	uint64_t dir_entry_lba;
	uint32_t dir_entry_offset;
} fat32_file_t;

void fat32_read_cached_sector(fat32_instance_t* inst, uint64_t lba, uint8_t* out_buffer) {
    if (!inst->cache) {
        block_read(inst->dev, lba, 1, out_buffer);
        return;
    }
	
	//mutex_lock(&inst->cache_lock);

    uint32_t idx = lba & FAT32_CACHE_MASK;

    if (inst->cache->entries[idx].lba == lba) {
        memcpy(out_buffer, inst->cache->entries[idx].data, inst->bytes_per_sector);
        return;
    }
	
	//mutex_unlock(&inst->cache_lock);

    block_read(inst->dev, lba, 1, out_buffer);

	//mutex_lock(&inst->cache_lock);
	
    inst->cache->entries[idx].lba = lba;
    memcpy(inst->cache->entries[idx].data, out_buffer, inst->bytes_per_sector);
	
	//mutex_unlock(&inst->cache_lock);
	
	// uncomment when multithreading appears in the VFS driver / раскомментировать при появлении многопоточности в драйвере VFS
}

void fat32_write_cached_sector(fat32_instance_t* inst, uint64_t lba, const uint8_t* in_buffer) {
    block_write(inst->dev, lba, 1, (void*)in_buffer);

    if (!inst->cache) return;

    uint32_t idx = lba & FAT32_CACHE_MASK;

    inst->cache->entries[idx].lba = lba;
    memcpy(inst->cache->entries[idx].data, in_buffer, inst->bytes_per_sector);
}

void fat32_read_sectors(fat32_instance_t* inst, uint64_t lba, uint64_t count, void* buffer) {
    if (count == 1) {
        fat32_read_cached_sector(inst, lba, buffer);
        return;
    }

    uint8_t* buf_ptr = (uint8_t*)buffer;
    
    uint64_t pending_start_idx = 0; 
    uint64_t pending_count = 0;

    for (uint64_t i = 0; i < count; i++) {
        uint64_t current_lba = lba + i;
        int is_cached = 0;

        if (inst->cache) {
            uint32_t idx = current_lba & FAT32_CACHE_MASK;
            if (inst->cache->entries[idx].lba == current_lba) {
                memcpy(buf_ptr + (i * 512), inst->cache->entries[idx].data, 512);
                is_cached = 1;
            }
        }

        if (is_cached) {
            if (pending_count > 0) {
                block_read(inst->dev, lba + pending_start_idx, pending_count, buf_ptr + (pending_start_idx * 512));
                pending_count = 0;
            }
        } else {
            if (pending_count == 0) {
                pending_start_idx = i;
            }
            pending_count++;
        }
    }

    if (pending_count > 0) {
        block_read(inst->dev, lba + pending_start_idx, pending_count, buf_ptr + (pending_start_idx * 512));
    }
}

void fat32_write_sectors(fat32_instance_t* inst, uint64_t lba, uint64_t count, const void* buffer) {
    if (count == 1) {
        fat32_write_cached_sector(inst, lba, buffer);
        return;
    }

    block_write(inst->dev, lba, count, (void*)buffer);

    if (!inst->cache) return;

    const uint8_t* buf_ptr = (const uint8_t*)buffer;

    for (uint64_t i = 0; i < count; i++) {
        uint64_t current_lba = lba + i;
        
        uint32_t idx = current_lba & FAT32_CACHE_MASK;
        if (inst->cache->entries[idx].lba == current_lba) {
            memcpy(inst->cache->entries[idx].data, buf_ptr + (i * 512), 512);
        }
    }
}

uint64_t cluster_to_lba(fat32_instance_t* inst, uint32_t cluster) {
    return inst->data_start_lba + ((uint64_t)(cluster - 2) * inst->sectors_per_cluster);
}

uint32_t get_next_cluster(fat32_instance_t* inst, uint32_t current_cluster) {
    uint32_t fat_offset = current_cluster * 4;
    uint32_t fat_sector = inst->fat_start_lba + (fat_offset / inst->bytes_per_sector);
    uint32_t ent_offset = fat_offset % inst->bytes_per_sector;

    uint8_t buffer[FAT32_MAX_SECTOR_SIZE];
    fat32_read_cached_sector(inst, fat_sector, buffer);

    uint32_t val = *(uint32_t*)&buffer[ent_offset];
    return val & 0x0FFFFFFF;
}

void fat32_update_fsinfo(fat32_instance_t* inst) {
    if (inst->fs_info_sector == 0 || inst->fs_info_sector == 0xFFFF) return;
    
    uint8_t buf[512];
    fat32_read_cached_sector(inst, inst->fs_info_sector, buf);
    
    fat32_fsinfo_t* fsinfo = (fat32_fsinfo_t*)buf;
    if (fsinfo->lead_sig == 0x41615252 && fsinfo->struct_sig == 0x61417272) {
        fsinfo->next_free = inst->next_free_cluster_hint;
        fat32_write_cached_sector(inst, inst->fs_info_sector, buf);
    }
}

void set_next_cluster(fat32_instance_t* inst, uint32_t cluster, uint32_t next_cluster) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = inst->fat_start_lba + (fat_offset / inst->bytes_per_sector);
    uint32_t ent_offset = fat_offset % inst->bytes_per_sector;

    uint8_t buffer[FAT32_MAX_SECTOR_SIZE];
    fat32_read_cached_sector(inst, fat_sector, buffer);

    uint32_t val = *(uint32_t*)&buffer[ent_offset];
    val = (val & 0xF0000000) | (next_cluster & 0x0FFFFFFF);
    *(uint32_t*)&buffer[ent_offset] = val;

    fat32_write_cached_sector(inst, fat_sector, buffer); 
}

uint32_t allocate_cluster(fat32_instance_t* inst) {
    uint32_t start_cluster = inst->next_free_cluster_hint;
    if (start_cluster < 2 || start_cluster >= inst->total_clusters + 2) {
        start_cluster = 2;
    }

    uint32_t cluster_to_check = start_cluster;

    do {
        if (get_next_cluster(inst, cluster_to_check) == 0x00000000) {
            set_next_cluster(inst, cluster_to_check, 0x0FFFFFFF); // Маркер EOF
            
            inst->next_free_cluster_hint = cluster_to_check + 1;
            if (inst->next_free_cluster_hint >= inst->total_clusters + 2) {
                inst->next_free_cluster_hint = 2;
            }

            fat32_update_fsinfo(inst);
            
            uint32_t cluster_size = inst->sectors_per_cluster * inst->bytes_per_sector;
            uint8_t* zero_buf = malloc(cluster_size);
            if (zero_buf) {
                memset(zero_buf, 0, cluster_size);
                uint64_t lba = cluster_to_lba(inst, cluster_to_check);
                fat32_write_sectors(inst, lba, inst->sectors_per_cluster, zero_buf);
                free(zero_buf);
            }
            return cluster_to_check;
        }

        cluster_to_check++;
        
        if (cluster_to_check >= inst->total_clusters + 2) {
            cluster_to_check = 2; 
        }
        
    } while (cluster_to_check != start_cluster);

    return 0; 
}

unsigned char fat32_checksum(unsigned char *pName) {
    unsigned char sum = 0;
    for (int i = 11; i; i--) {
        sum = ((sum & 1) << 7) + (sum >> 1) + *pName++;
    }
    return sum;
}

void fat32_utf16_to_utf8(const uint16_t* utf16, char* utf8, int max_bytes) {
    int out_idx = 0;
    for (int i = 0; i < 256 && utf16[i] != 0x0000 && utf16[i] != 0xFFFF; i++) {
        uint16_t c = utf16[i];
        if (out_idx >= max_bytes - 4) break;
        
        if (c < 0x0080) {
            utf8[out_idx++] = (char)c;
        } else if (c < 0x0800) {
            utf8[out_idx++] = 0xC0 | (c >> 6);
            utf8[out_idx++] = 0x80 | (c & 0x3F);
        } else {
            utf8[out_idx++] = 0xE0 | (c >> 12);
            utf8[out_idx++] = 0x80 | ((c >> 6) & 0x3F);
            utf8[out_idx++] = 0x80 | (c & 0x3F);
        }
    }
    utf8[out_idx] = '\0';
}

void fat32_collect_lfn_chars(fat32_lfn_entry_t* lfn, uint16_t* lfn_buffer) {
    int order = lfn->order & 0x1F;
    if (order < 1 || order > 20) return; 

    int index = (order - 1) * 13;
    if (index < 0 || index + 13 > 256) return;

    for (int i = 0; i < 5; i++)  lfn_buffer[index++] = lfn->name1[i];
    for (int i = 0; i < 6; i++)  lfn_buffer[index++] = lfn->name2[i];
    for (int i = 0; i < 2; i++)  lfn_buffer[index++] = lfn->name3[i];
}

void fat32_format_sfn(char* dest, const char* sfn_name) {
    int p = 0;
    for (int i = 0; i < 8; i++) {
        if (sfn_name[i] == ' ') break;
        dest[p++] = sfn_name[i];
    }
    if (sfn_name[8] != ' ') {
        dest[p++] = '.';
        for (int i = 8; i < 11; i++) {
            if (sfn_name[i] == ' ') break;
            dest[p++] = sfn_name[i];
        }
    }
    dest[p] = '\0';
}

void fat32_generate_sfn(const char* filename, char* sfn_out) {
    memset(sfn_out, ' ', 11);
    int i = 0, j = 0;
    while (filename[i] != '\0' && filename[i] != '.' && j < 8) {
        char c = filename[i++];
        if (c >= 'a' && c <= 'z') c -= 32;
        sfn_out[j++] = c;
    }
    
    while (filename[i] != '\0' && filename[i] != '.') i++;
    
    if (filename[i] == '.') {
        i++;
        j = 8;
        while (filename[i] != '\0' && j < 11) {
            char c = filename[i++];
            if (c >= 'a' && c <= 'z') c -= 32;
            sfn_out[j++] = c;
        }
    }
}

int fat32_create_file_entry(fat32_instance_t* inst, uint32_t parent_cluster, const char* filename, fat32_file_t* handle_out) {
    char sfn[11];
    fat32_generate_sfn(filename, sfn);
    
    uint32_t cluster = parent_cluster;
    uint32_t last_cluster = cluster;
    uint8_t buffer[FAT32_MAX_SECTOR_SIZE];
    
    while (cluster < 0x0FFFFFF8 && cluster >= 2) {
        uint64_t lba = cluster_to_lba(inst, cluster);
        
        for (uint32_t s = 0; s < inst->sectors_per_cluster; s++) {
            fat32_read_cached_sector(inst, lba + s, buffer);
            fat32_dir_entry_t* entries = (fat32_dir_entry_t*)buffer;
            
            for (uint32_t i = 0; i < inst->bytes_per_sector / 32; i++) {
                if ((entries[i].name[0] & 0xFF) == 0x00 || (entries[i].name[0] & 0xFF) == 0xE5) {
                    
                    memset(&entries[i], 0, sizeof(fat32_dir_entry_t));
                    memcpy(entries[i].name, sfn, 11);
                    entries[i].attr = FAT_ATTR_ARCHIVE;
                    
                    fat32_write_cached_sector(inst, lba + s, buffer);
                    
                    handle_out->first_cluster = 0;
                    handle_out->current_cluster = 0;
                    handle_out->size_bytes = 0;
                    handle_out->current_offset = 0;
                    handle_out->attributes = FAT_ATTR_ARCHIVE;
                    handle_out->dir_entry_lba = lba + s;
                    handle_out->dir_entry_offset = i * 32;
                    
                    return 1;
                }
            }
        }
        last_cluster = cluster;
        cluster = get_next_cluster(inst, cluster);
    }
    
    uint32_t new_cluster = allocate_cluster(inst);
    if (!new_cluster) return 0;
    
    set_next_cluster(inst, last_cluster, new_cluster);
    
    uint64_t lba = cluster_to_lba(inst, new_cluster);
    fat32_read_cached_sector(inst, lba, buffer); 
    
    fat32_dir_entry_t* entries = (fat32_dir_entry_t*)buffer;
    memcpy(entries[0].name, sfn, 11);
    entries[0].attr = FAT_ATTR_ARCHIVE;
    
    fat32_write_cached_sector(inst, lba, buffer);
    
    handle_out->first_cluster = 0;
    handle_out->current_cluster = 0;
    handle_out->size_bytes = 0;
    handle_out->current_offset = 0;
    handle_out->attributes = FAT_ATTR_ARCHIVE;
    handle_out->dir_entry_lba = lba;
    handle_out->dir_entry_offset = 0;
    
    return 1;
}

void fat32_truncate_file(fat32_instance_t* inst, fat32_file_t* file) {
    if (file->first_cluster >= 2 && file->first_cluster < 0x0FFFFFF8) {
        uint32_t cluster = file->first_cluster;
        while (cluster < 0x0FFFFFF8 && cluster >= 2) {
            uint32_t next = get_next_cluster(inst, cluster);
            set_next_cluster(inst, cluster, 0x00000000);
            cluster = next;
        }
    }
    
    file->first_cluster = 0;
    file->current_cluster = 0;
    file->size_bytes = 0;
    file->current_offset = 0;
    
    uint8_t sec_buf[FAT32_MAX_SECTOR_SIZE];
    fat32_read_cached_sector(inst, file->dir_entry_lba, sec_buf);
    fat32_dir_entry_t* d_ent = (fat32_dir_entry_t*)&sec_buf[file->dir_entry_offset];
    d_ent->file_size = 0;
    d_ent->cluster_high = 0;
    d_ent->cluster_low = 0;
    fat32_write_cached_sector(inst, file->dir_entry_lba, sec_buf);
}

fs_instance_t fat32_mount(block_dev_t* dev) {
    uint8_t* buf = malloc(FAT32_MAX_SECTOR_SIZE);
    if (block_read(dev, 0, 1, buf) != 0) {
        free(buf);
        return 0;
    }

    fat32_bpb_t* bpb = (fat32_bpb_t*)buf;

    if (bpb->boot_signature != 0x29 && bpb->boot_signature != 0x28 && buf[510] != 0x55) {
		printf("Mount failed: bad signature\n");
		free(buf);
		return 0;
    }
    if (buf[511] != 0xAA && bpb->boot_signature != 0x29 && bpb->boot_signature != 0x28) {
		printf("Mount failed: bad boot marker\n");
		free(buf);
		return 0;
    }
    if (bpb->bytes_per_sector == 0 || bpb->sectors_per_cluster == 0) {
		printf("Mount failed: invalid BPB\n");
		free(buf);
		return 0;
    }
    if (bpb->bytes_per_sector != 512 && bpb->bytes_per_sector != 1024 &&
        bpb->bytes_per_sector != 2048 && bpb->bytes_per_sector != 4096) {
		printf("Mount failed: unsupported sector size\n");
		free(buf);
		return 0;
    }

    fat32_instance_t* inst = malloc(sizeof(fat32_instance_t));
    if (!inst) {
		free(buf);
		return 0;
    }
	
	mutex_init(&inst->cache_lock);
	
    inst->dev = dev;
    inst->bytes_per_sector = bpb->bytes_per_sector;
    inst->sectors_per_cluster = bpb->sectors_per_cluster;
    inst->root_cluster = bpb->root_cluster;
    inst->fat_start_lba = bpb->reserved_sectors;
    
    uint32_t fat_size = bpb->sectors_per_fat_32;
    inst->data_start_lba = inst->fat_start_lba + (bpb->fats_count * fat_size);
	
	uint32_t total_sectors = bpb->total_sectors_32 != 0 ? bpb->total_sectors_32 : bpb->total_sectors_16;
    uint32_t data_sectors = total_sectors - inst->data_start_lba;
    inst->total_clusters = data_sectors / inst->sectors_per_cluster;
	
	inst->cache = malloc(sizeof(fat32_cache_t));
    if (inst->cache) {
        for (int i = 0; i < FAT32_CACHE_ENTRIES; i++) {
            inst->cache->entries[i].lba = (uint64_t)-1;
        }
    }
	
	inst->fs_info_sector = bpb->fs_info_sector;
    inst->next_free_cluster_hint = 2; 

    if (inst->fs_info_sector != 0 && inst->fs_info_sector != 0xFFFF) {
        uint8_t fsinfo_buf[512];
        if (block_read(dev, inst->fs_info_sector, 1, fsinfo_buf) == 0) {
            fat32_fsinfo_t* fsinfo = (fat32_fsinfo_t*)fsinfo_buf;
            
            if (fsinfo->lead_sig == 0x41615252 && fsinfo->struct_sig == 0x61417272) {
                if (fsinfo->next_free >= 2 && fsinfo->next_free < inst->total_clusters + 2) {
                    inst->next_free_cluster_hint = fsinfo->next_free;
                }
            }
        }
    }

    free(buf);
    return (fs_instance_t)inst;
}

void fat32_umount(fs_instance_t fs) {
    fat32_instance_t* inst = (fat32_instance_t*)fs;
    if (inst->cache) {
        free(inst->cache);
    }
    free(inst);
}

int find_entry_in_cluster_chain(fat32_instance_t* inst, uint32_t start_cluster, const char* name, fat32_file_t* file_out) {
    uint8_t* buffer = malloc(inst->sectors_per_cluster * inst->bytes_per_sector);
	if (!buffer) return 0; 
    uint32_t cluster = start_cluster;
    
    char search_name[256];
    strncpy(search_name, name, 255);
	search_name[255] = 0;
    to_upper(search_name);

    uint16_t lfn_temp[256];
    uint8_t lfn_checksum = 0;
    memset(lfn_temp, 0, sizeof(lfn_temp));

    while (cluster < 0x0FFFFFF8 && cluster >= 2) {
        uint64_t lba = cluster_to_lba(inst, cluster);
        fat32_read_sectors(inst, lba, inst->sectors_per_cluster, buffer);

        fat32_dir_entry_t* dir = (fat32_dir_entry_t*)buffer;
        int entries_per_cluster = (inst->sectors_per_cluster * inst->bytes_per_sector) / 32;

        for (int i = 0; i < entries_per_cluster; i++) {
            if ((dir[i].name[0] & 0xFF) == 0x00) { free(buffer); return 0; } // Конец
            if ((dir[i].name[0] & 0xFF) == 0xE5) {
				memset(lfn_temp, 0, sizeof(lfn_temp));
				continue;
			}

            if (dir[i].attr == 0x0F) {
                fat32_lfn_entry_t* lfn = (fat32_lfn_entry_t*)&dir[i];
                if (lfn->order & 0x40) {
                    memset(lfn_temp, 0, sizeof(lfn_temp));
                    lfn_checksum = lfn->checksum;
                }
                fat32_collect_lfn_chars(lfn, lfn_temp);
                continue;
            }

            if (dir[i].attr & 0x08) {
                memset(lfn_temp, 0, sizeof(lfn_temp));
                continue;
            }

            char current_name[256];
            uint8_t sfn_sum = fat32_checksum((unsigned char*)dir[i].name);
            
            if (lfn_temp[0] != 0 && sfn_sum == lfn_checksum) {
                fat32_utf16_to_utf8(lfn_temp, current_name, 256);
            } else {
                fat32_format_sfn(current_name, dir[i].name);
            }
            
            char current_upper[256];
            strncpy(current_upper, current_name, 256);
            to_upper(current_upper);

            if (strcmp(current_upper, search_name) == 0) {
                file_out->first_cluster = ((uint32_t)dir[i].cluster_high << 16) | dir[i].cluster_low;
                file_out->size_bytes = dir[i].file_size;
                file_out->attributes = dir[i].attr;
                file_out->current_cluster = file_out->first_cluster;
                file_out->current_offset = 0;
				
				file_out->dir_entry_lba = lba + ((i * 32) / inst->bytes_per_sector);
                file_out->dir_entry_offset = (i * 32) % inst->bytes_per_sector;
                
                free(buffer);
                return 1;
            }

            memset(lfn_temp, 0, 256);
        }
        cluster = get_next_cluster(inst, cluster);
    }

    free(buffer);
    return 0;
}

fs_file_handle_t fat32_open_from_cluster(fat32_instance_t* inst, uint32_t start_cluster, const char* path, uint32_t flags) {
    if (!path || path[0] == 0) return 0;

    fat32_file_t* handle = malloc(sizeof(fat32_file_t));
    if (!handle) return 0;
	
	handle->first_cluster = start_cluster;
    handle->current_cluster = start_cluster;
    handle->size_bytes = 0;
    handle->current_offset = 0;
    handle->attributes = FAT_ATTR_DIRECTORY;
    handle->dir_entry_lba = 0;
    handle->dir_entry_offset = 0;

    if (path[0] == '/' && path[1] == '\0') {
        return (fs_file_handle_t)handle;
    }
	
	char path_copy[256];
    strncpy(path_copy, path, 255);
    path_copy[255] = 0;

    uint32_t current_cluster = start_cluster;
    char* saveptr;
    char* token = strtok_r(path_copy, "/", &saveptr);

    while (token != 0) {
        char* next_token = strtok_r(0, "/", &saveptr);
        
        if (!find_entry_in_cluster_chain(inst, current_cluster, token, handle)) {
            if (next_token == 0 && (flags & VFS_FCREATE)) {
                if (!fat32_create_file_entry(inst, current_cluster, token, handle)) {
                    free(handle);
                    return 0;
                }
                break;
            } else {
                free(handle);
                return 0;
            }
        }
        
        if (next_token == 0) {
            if ((flags & VFS_FTRUNC) && (flags & VFS_FWRITE) && !(handle->attributes & FAT_ATTR_DIRECTORY)) {
                fat32_truncate_file(inst, handle);
            }
        } else {
            if (!(handle->attributes & FAT_ATTR_DIRECTORY)) {
                free(handle);
                return 0;
            }
            current_cluster = handle->first_cluster;
        }
        
        token = next_token;
    }

    return (fs_file_handle_t)handle;
}

fs_file_handle_t fat32_open(fs_instance_t fs, const char* path, uint32_t flags) {
    fat32_instance_t* inst = (fat32_instance_t*)fs;
    return fat32_open_from_cluster(inst, inst->root_cluster, path, flags);
}

fs_file_handle_t fat32_openat(fs_instance_t fs, fs_file_handle_t dir_handle, const char* path, uint32_t flags) {
    fat32_instance_t* inst = (fat32_instance_t*)fs;
    fat32_file_t* parent = (fat32_file_t*)dir_handle;
    
    if (!(parent->attributes & FAT_ATTR_DIRECTORY) && parent->first_cluster != inst->root_cluster) {
        return 0; 
    }

    return fat32_open_from_cluster(inst, parent->first_cluster, path, flags);
}

int fat32_read(fs_instance_t fs, fs_file_handle_t f, void* buf, uint64_t size, uint64_t offset) {
    fat32_instance_t* inst = (fat32_instance_t*)fs;
    fat32_file_t* file = (fat32_file_t*)f;
    
    if (file->attributes & FAT_ATTR_DIRECTORY) return -1;
	if (file->size_bytes == 0 || offset >= file->size_bytes) return 0; // EOF

    if (offset + size > file->size_bytes) {
        size = file->size_bytes - offset;
    }

    uint64_t cluster_bytes = inst->sectors_per_cluster * inst->bytes_per_sector;
    uint32_t cluster = file->first_cluster;

    uint64_t current_pos = 0;
    if (offset >= file->current_offset && file->current_cluster != 0) {
        cluster = file->current_cluster;
        current_pos = file->current_offset;
    }

    while (current_pos + cluster_bytes <= offset) {
        cluster = get_next_cluster(inst, cluster);
        current_pos += cluster_bytes;
        if (cluster >= 0x0FFFFFF8) return -1;
    }
	
    file->current_cluster = cluster;
    file->current_offset = current_pos;
    uint64_t bytes_read = 0;
    uint8_t* out_ptr = (uint8_t*)buf;
    
    uint8_t* cl_buf = malloc(cluster_bytes);

    while (bytes_read < size && cluster < 0x0FFFFFF8) {
        uint64_t lba = cluster_to_lba(inst, cluster);
        fat32_read_sectors(inst, lba, inst->sectors_per_cluster, cl_buf);

        uint64_t offset_in_cluster = (offset + bytes_read) - current_pos;
        uint64_t available_in_cluster = cluster_bytes - offset_in_cluster;
        uint64_t to_copy = size - bytes_read;

        if (to_copy > available_in_cluster) to_copy = available_in_cluster;

        memcpy(out_ptr + bytes_read, cl_buf + offset_in_cluster, to_copy);
        
        bytes_read += to_copy;
        
        if (bytes_read < size) {
            cluster = get_next_cluster(inst, cluster);
            current_pos += cluster_bytes;
        }
    }

    free(cl_buf);
    file->current_cluster = cluster;
    file->current_offset = current_pos;

    return bytes_read;
}

int fat32_write(fs_instance_t fs, fs_file_handle_t f, const void* buf, uint64_t size, uint64_t offset) {
    fat32_instance_t* inst = (fat32_instance_t*)fs;
    fat32_file_t* file = (fat32_file_t*)f;
    
    if (file->attributes & FAT_ATTR_DIRECTORY) return -1; // В папки писать нельзя

    uint64_t cluster_bytes = inst->sectors_per_cluster * inst->bytes_per_sector;

    if (offset + size > file->size_bytes) {
        uint64_t current_capacity = ((file->size_bytes + cluster_bytes - 1) / cluster_bytes) * cluster_bytes;
        if (file->size_bytes == 0) current_capacity = 0;

        uint64_t needed_capacity = offset + size;

        if (file->first_cluster == 0 && needed_capacity > 0) {
            file->first_cluster = allocate_cluster(inst);
            if (!file->first_cluster) return -1; // Disk Full
            file->current_cluster = file->first_cluster;
        }

        if (needed_capacity > current_capacity && file->first_cluster != 0) {
            uint32_t last_cluster = file->first_cluster;
            
            while (1) {
                uint32_t next = get_next_cluster(inst, last_cluster);
                if (next >= 0x0FFFFFF8) break;
                last_cluster = next;
            }

            while (current_capacity < needed_capacity) {
                uint32_t new_cluster = allocate_cluster(inst);
                if (!new_cluster) return -1; // Disk Full
                set_next_cluster(inst, last_cluster, new_cluster);
                last_cluster = new_cluster;
                current_capacity += cluster_bytes;
            }
        }
    }

    uint32_t cluster = file->first_cluster;
    uint64_t current_pos = 0;

    while (current_pos + cluster_bytes <= offset) {
        cluster = get_next_cluster(inst, cluster);
        current_pos += cluster_bytes;
        if (cluster >= 0x0FFFFFF8) return -1; // Защита от сбоя FAT
    }

    uint64_t bytes_written = 0;
    const uint8_t* in_ptr = (const uint8_t*)buf;
    uint8_t* cl_buf = malloc(cluster_bytes);
	if (!cl_buf) return -1;

    while (bytes_written < size && cluster < 0x0FFFFFF8) {
        uint64_t lba = cluster_to_lba(inst, cluster);

        uint64_t offset_in_cluster = (offset + bytes_written) - current_pos;
        uint64_t available_in_cluster = cluster_bytes - offset_in_cluster;
        uint64_t to_copy = size - bytes_written;
        if (to_copy > available_in_cluster) to_copy = available_in_cluster;

        if (to_copy != cluster_bytes) {
            fat32_read_sectors(inst, lba, inst->sectors_per_cluster, cl_buf);
        }

        memcpy(cl_buf + offset_in_cluster, in_ptr + bytes_written, to_copy);
        fat32_write_sectors(inst, lba, inst->sectors_per_cluster, cl_buf);

        bytes_written += to_copy;

        if (bytes_written < size) {
            cluster = get_next_cluster(inst, cluster);
            current_pos += cluster_bytes;
        }
    }
    
    free(cl_buf);

    if (offset + bytes_written > file->size_bytes) {
        file->size_bytes = offset + bytes_written;

        uint8_t sec_buf[FAT32_MAX_SECTOR_SIZE];
        fat32_read_cached_sector(inst, file->dir_entry_lba, sec_buf);

        fat32_dir_entry_t* d_ent = (fat32_dir_entry_t*)&sec_buf[file->dir_entry_offset];
        d_ent->file_size = file->size_bytes;
        
        d_ent->cluster_high = (file->first_cluster >> 16) & 0xFFFF;
        d_ent->cluster_low = file->first_cluster & 0xFFFF;

        fat32_write_cached_sector(inst, file->dir_entry_lba, sec_buf);
    }

    return bytes_written;
}

int fat32_readdir(fs_instance_t fs, fs_file_handle_t dir_handle, uint64_t* offset, fs_dirent_t* out_array, int max_entries) {
    fat32_instance_t* inst = (fat32_instance_t*)fs;
    fat32_file_t* dir = (fat32_file_t*)dir_handle;
    
    if (!(dir->attributes & FAT_ATTR_DIRECTORY) && dir->first_cluster != inst->root_cluster) return 0;
    
    if (*offset == (uint64_t)-1) return 0;

    uint32_t cluster;
    uint32_t byte_in_cluster;

    if (*offset == 0) {
        cluster = dir->first_cluster;
        byte_in_cluster = 0;
    } else {
        cluster = (uint32_t)(*offset >> 32);
        byte_in_cluster = (uint32_t)(*offset & 0xFFFFFFFF);
    }

    uint8_t buffer[FAT32_MAX_SECTOR_SIZE];
    uint16_t lfn_temp[256];
    uint8_t lfn_checksum = 0;
    memset(lfn_temp, 0, sizeof(lfn_temp));
    
    uint32_t cluster_size_bytes = inst->sectors_per_cluster * inst->bytes_per_sector;
    
    int entries_read = 0;

    while (entries_read < max_entries && cluster < 0x0FFFFFF8 && cluster >= 2) {
        uint64_t lba = cluster_to_lba(inst, cluster);
        uint32_t start_sector = byte_in_cluster / inst->bytes_per_sector;
        uint32_t start_entry  = (byte_in_cluster % inst->bytes_per_sector) / 32;

        for (uint32_t s = start_sector; s < inst->sectors_per_cluster; s++) {
            fat32_read_cached_sector(inst, lba + s, buffer);
            fat32_dir_entry_t* entries = (fat32_dir_entry_t*)buffer;
            
            for (uint32_t i = start_entry; i < inst->bytes_per_sector / 32; i++) {
                byte_in_cluster = (s * inst->bytes_per_sector) + (i * 32);

                if ((entries[i].name[0] & 0xFF) == 0x00) {
                    *offset = (uint64_t)-1; // EOF
                    return entries_read;    // Возвращаем сколько успели прочитать
                }
                if ((entries[i].name[0] & 0xFF) == 0xE5) {
                    memset(lfn_temp, 0, sizeof(lfn_temp));
                    lfn_checksum = 0;
                    continue;
                }
                if (entries[i].attr == 0x0F) { 
                    fat32_lfn_entry_t* lfn = (fat32_lfn_entry_t*)&entries[i];
                    if (lfn->order & 0x40) {
                        memset(lfn_temp, 0, sizeof(lfn_temp));
                        lfn_checksum = lfn->checksum;
                    }
                    fat32_collect_lfn_chars(lfn, lfn_temp);
                    continue;
                }
                if (entries[i].attr & 0x08) { // Volume ID
                    memset(lfn_temp, 0, sizeof(lfn_temp));
                    continue; 
                }

                fs_dirent_t* out = &out_array[entries_read];

                uint8_t sfn_sum = fat32_checksum((unsigned char*)entries[i].name);
                if (lfn_temp[0] != 0 && sfn_sum == lfn_checksum) {
                    fat32_utf16_to_utf8(lfn_temp, out->name, sizeof(out->name));
                } else {
                    fat32_format_sfn(out->name, entries[i].name);
                }
                out->size = entries[i].file_size;
                out->type = (entries[i].attr & FAT_ATTR_DIRECTORY) ? VFS_FILE_TYPE_DIR : VFS_FILE_TYPE_REGULAR;
                
                memset(lfn_temp, 0, sizeof(lfn_temp));
                lfn_checksum = 0;

                uint32_t next_byte = byte_in_cluster + 32;
                uint32_t next_cluster = cluster;
                
                if (next_byte >= cluster_size_bytes) {
                    next_cluster = get_next_cluster(inst, cluster);
                    next_byte = 0;
                }

                if (next_cluster >= 0x0FFFFFF8) {
                    *offset = (uint64_t)-1;
                } else {
                    *offset = ((uint64_t)next_cluster << 32) | next_byte; 
                }
                
                entries_read++;
                
                if (entries_read >= max_entries) {
                    return entries_read;
                }
            }
            start_entry = 0;
        }
        cluster = get_next_cluster(inst, cluster);
        byte_in_cluster = 0;
        start_sector = 0;
    }
    
    if (entries_read == 0) *offset = (uint64_t)-1;
    return entries_read; 
}

int fat32_stat(fs_instance_t fs, fs_file_handle_t f, fs_stat_t* out_info) {
    if (!f || !out_info) return -1;
    
    fat32_file_t* file = (fat32_file_t*)f;
    
    memset(out_info, 0, sizeof(fs_stat_t));
    out_info->size_bytes = file->size_bytes;
    out_info->attributes = file->attributes;
    
    out_info->inode_id = (file->dir_entry_lba << 32) | file->dir_entry_offset;
    
    return 0;
}

void fat32_get_label(fs_instance_t fs, char* out_label) {
    fat32_instance_t* inst = (fat32_instance_t*)fs;
    uint32_t cluster = inst->root_cluster;
    uint8_t buffer[FAT32_MAX_SECTOR_SIZE];

    strcpy(out_label, "NO_NAME");

    while (cluster < 0x0FFFFFF8 && cluster >= 2) {
        uint64_t lba = cluster_to_lba(inst, cluster);
        
        for (int s = 0; s < inst->sectors_per_cluster; s++) {
            fat32_read_cached_sector(inst, lba + s, buffer);
            fat32_dir_entry_t* entries = (fat32_dir_entry_t*)buffer;
            
            for (int i = 0; i < (int)(inst->bytes_per_sector / 32); i++) {
                if ((entries[i].name[0] & 0xFF) == 0x00) return;
                if ((entries[i].name[0] & 0xFF) == 0xE5) continue;
                if (entries[i].attr == 0x0F) continue;
                
                if (entries[i].attr == 0x08) { 
                    int len = 11;
                    while (len > 0 && entries[i].name[len - 1] == ' ') {
                        len--;
                    }
                    
                    if (len > 0) {
                        memcpy(out_label, entries[i].name, len);
                        out_label[len] = '\0';
                    }
                    return;
                }
            }
        }
        cluster = get_next_cluster(inst, cluster);
    }
}

void fat32_close(fs_instance_t fs, fs_file_handle_t f) {
    free(f);
}

fs_driver_t fat32_driver = {
    .mount = fat32_mount,
    .umount = fat32_umount,
    .open = fat32_open,
	.openat = fat32_openat,
    .read = fat32_read,
	.write = fat32_write,
    .close = fat32_close,
    .readdir = fat32_readdir,
	.stat = fat32_stat,
    .get_label = fat32_get_label
};