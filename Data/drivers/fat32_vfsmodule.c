#include <stdint.h>
#include "../include/aoslib.h"
#include "../include/fs_interface.h"

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
    block_dev_t* dev;
    uint32_t root_cluster;
    uint32_t sectors_per_cluster;
    uint32_t bytes_per_sector;
    uint32_t fat_start_lba;
    uint32_t data_start_lba;
} fat32_instance_t;

typedef struct {
    uint32_t first_cluster;
    uint64_t size_bytes;
    uint32_t current_cluster;
    uint64_t current_offset;
    uint8_t  is_dir;
} fat32_file_t;

uint64_t cluster_to_lba(fat32_instance_t* inst, uint32_t cluster) {
    return inst->data_start_lba + ((uint64_t)(cluster - 2) * inst->sectors_per_cluster);
}

uint32_t get_next_cluster(fat32_instance_t* inst, uint32_t current_cluster) {
    uint32_t fat_offset = current_cluster * 4;
    uint32_t fat_sector = inst->fat_start_lba + (fat_offset / inst->bytes_per_sector);
    uint32_t ent_offset = fat_offset % inst->bytes_per_sector;

    uint8_t buffer[512];
    // Тут в идеале нужен кэш секторов
    block_read(inst->dev, fat_sector, 1, buffer);

    uint32_t val = *(uint32_t*)&buffer[ent_offset];
    return val & 0x0FFFFFFF;
}

unsigned char fat32_checksum(unsigned char *pName) {
    unsigned char sum = 0;
    for (int i = 11; i; i--) {
        sum = ((sum & 1) << 7) + (sum >> 1) + *pName++;
    }
    return sum;
}

void fat32_collect_lfn_chars(fat32_lfn_entry_t* lfn, char* lfn_buffer) {
    int order = lfn->order & 0x1F;
    if (order < 1 || order > 20) return; 

    int index = (order - 1) * 13;
    if (index < 0 || index + 13 > 255) return;

    for (int i = 0; i < 5; i++)  lfn_buffer[index++] = (char)(lfn->name1[i] & 0xFF);
    for (int i = 0; i < 6; i++)  lfn_buffer[index++] = (char)(lfn->name2[i] & 0xFF);
    for (int i = 0; i < 2; i++)  lfn_buffer[index++] = (char)(lfn->name3[i] & 0xFF);
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

// === Реализация интерфейса драйвера ===

fs_instance_t fat32_mount(block_dev_t* dev) {
    uint8_t* buf = malloc(512);
    if (block_read(dev, 0, 1, buf) != 0) {
        free(buf);
        return 0;
    }

    fat32_bpb_t* bpb = (fat32_bpb_t*)buf;

    // Простая проверка
    if (bpb->boot_signature != 0x29 && bpb->boot_signature != 0x28 && buf[510] != 0x55) {
		printf("Mount failed\n");
		return 0;
    }

    fat32_instance_t* inst = malloc(sizeof(fat32_instance_t));
    inst->dev = dev;
    inst->bytes_per_sector = bpb->bytes_per_sector;
    inst->sectors_per_cluster = bpb->sectors_per_cluster;
    inst->root_cluster = bpb->root_cluster;
    inst->fat_start_lba = bpb->reserved_sectors;
    
    uint32_t fat_size = bpb->sectors_per_fat_32;
    inst->data_start_lba = inst->fat_start_lba + (bpb->fats_count * fat_size);

    free(buf);
    return (fs_instance_t)inst;
}

void fat32_umount(fs_instance_t fs) {
    free(fs);
}

// Поиск записи в директории (возвращает первый кластер файла)
// Возвращает 1 если нашел, заполняет file_out
int find_entry_in_cluster_chain(fat32_instance_t* inst, uint32_t start_cluster, const char* name, fat32_file_t* file_out) {
    uint8_t* buffer = malloc(inst->sectors_per_cluster * 512);
    uint32_t cluster = start_cluster;
    
    char search_name[256];
    strncpy(search_name, name, 255);
    to_upper(search_name);

    char lfn_temp[256];
    uint8_t lfn_checksum = 0;
    memset(lfn_temp, 0, 256);

    while (cluster < 0x0FFFFFF8 && cluster >= 2) {
        uint64_t lba = cluster_to_lba(inst, cluster);
        block_read(inst->dev, lba, inst->sectors_per_cluster, buffer);

        fat32_dir_entry_t* dir = (fat32_dir_entry_t*)buffer;
        int entries_per_cluster = (inst->sectors_per_cluster * 512) / 32;

        for (int i = 0; i < entries_per_cluster; i++) {
            if (dir[i].name[0] == 0x00) { free(buffer); return 0; } // Конец
            if (dir[i].name[0] == 0xE5) { // Удален
                memset(lfn_temp, 0, 256);
                continue;
            }

            if (dir[i].attr == 0x0F) { // LFN
                fat32_lfn_entry_t* lfn = (fat32_lfn_entry_t*)&dir[i];
                if (lfn->order & 0x40) {
                    memset(lfn_temp, 0, 256);
                    lfn_checksum = lfn->checksum;
                }
                fat32_collect_lfn_chars(lfn, lfn_temp);
                continue;
            }

            if (dir[i].attr & 0x08) { // Volume label
                memset(lfn_temp, 0, 256);
                continue;
            }

            // Формируем имя для сравнения
            char current_name[256];
            uint8_t sfn_sum = fat32_checksum((unsigned char*)dir[i].name);
            
            if (lfn_temp[0] != 0 && sfn_sum == lfn_checksum) {
                strncpy(current_name, lfn_temp, 256);
            } else {
                fat32_format_sfn(current_name, dir[i].name);
            }
            
            // Сравнение
            char current_upper[256];
            strncpy(current_upper, current_name, 256);
            to_upper(current_upper);

            if (strcmp(current_upper, search_name) == 0) {
                // Нашли!
                file_out->first_cluster = ((uint32_t)dir[i].cluster_high << 16) | dir[i].cluster_low;
                file_out->size_bytes = dir[i].file_size;
                file_out->is_dir = (dir[i].attr & 0x10) ? 1 : 0;
                file_out->current_cluster = file_out->first_cluster;
                file_out->current_offset = 0;
                
                free(buffer);
                return 1;
            }

            // Сброс LFN
            memset(lfn_temp, 0, 256);
        }
        cluster = get_next_cluster(inst, cluster);
    }

    free(buffer);
    return 0;
}

fs_file_handle_t fat32_open(fs_instance_t fs, const char* path) {
    fat32_instance_t* inst = (fat32_instance_t*)fs;
    const char* p = path;
    if (*p == '/') p++;

    fat32_file_t* handle = malloc(sizeof(fat32_file_t));

    if (*p == 0) {
        handle->first_cluster = inst->root_cluster;
        handle->current_cluster = inst->root_cluster;
        handle->current_offset = 0;
        handle->is_dir = 1;
        handle->size_bytes = 0;
        return (fs_file_handle_t)handle;
    }

    if (find_entry_in_cluster_chain(inst, inst->root_cluster, p, handle)) {
        return (fs_file_handle_t)handle;
    }

    free(handle);
    return 0;
}

// Адаптированная функция чтения с поддержкой offset и partial read
int fat32_read(fs_instance_t fs, fs_file_handle_t f, void* buf, uint64_t size, uint64_t offset) {
    fat32_instance_t* inst = (fat32_instance_t*)fs;
    fat32_file_t* file = (fat32_file_t*)f;
    
    if (file->is_dir) return -1; // Нельзя читать директорию как файл
    if (offset >= file->size_bytes) return 0; // EOF

    // Обрезаем size, если выходим за пределы файла
    if (offset + size > file->size_bytes) {
        size = file->size_bytes - offset;
    }

    uint64_t cluster_bytes = inst->sectors_per_cluster * 512;
    uint32_t cluster = file->first_cluster;
    
    // Оптимизация: если читаем последовательно
    uint64_t current_pos = 0;
    if (offset >= file->current_offset && file->current_cluster != 0) {
        cluster = file->current_cluster;
        current_pos = file->current_offset;
    }

    // Пропускаем кластеры до нужного offset
    while (current_pos + cluster_bytes <= offset) {
        cluster = get_next_cluster(inst, cluster);
        current_pos += cluster_bytes;
        if (cluster >= 0x0FFFFFF8) return -1; // Ошибка структуры FS
    }
    
    // Обновляем кэш в хендле
    file->current_cluster = cluster;
    file->current_offset = current_pos;

    // Читаем данные
    uint64_t bytes_read = 0;
    uint8_t* out_ptr = (uint8_t*)buf;
    
    // Временный буфер для чтения кластера (чтобы не читать лишнего в user buffer)
    uint8_t* cl_buf = malloc(cluster_bytes);

    while (bytes_read < size && cluster < 0x0FFFFFF8) {
        uint64_t lba = cluster_to_lba(inst, cluster);
        
        // Читаем кластер целиком
        block_read(inst->dev, lba, inst->sectors_per_cluster, cl_buf);

        // Определяем, какую часть кластера копировать
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
    
    // Обновляем кэш позиции для следующего чтения
    file->current_cluster = cluster;
    file->current_offset = current_pos; // Внимание: это начало кластера, а не offset

    return bytes_read;
}

int fat32_readdir(fs_instance_t fs, fs_file_handle_t dir_handle, int index, fs_dirent_t* out) {
    fat32_instance_t* inst = (fat32_instance_t*)fs;
    fat32_file_t* dir = (fat32_file_t*)dir_handle;
    
    // Если это не директория, ошибка
    if (!dir->is_dir && dir->first_cluster != inst->root_cluster) return 0;

    // Чтение сырых записей директории
    // Это сложнее, чем кажется, из-за LFN (длинных имен).
    // Драйвер должен уметь пропускать N валидных файлов.
    
    // УПРОЩЕННЫЙ ВАРИАНТ (Без LFN, только 8.3 имена) для теста:
    
    uint32_t cluster = dir->first_cluster;
    uint8_t buffer[512];
    int count = 0;
    
    while (cluster < 0x0FFFFFF8 && cluster >= 2) {
        uint64_t lba = cluster_to_lba(inst, cluster);
        
        // Читаем кластер посекторно (для простоты)
        for (int s = 0; s < inst->sectors_per_cluster; s++) {
            block_read(inst->dev, lba + s, 1, buffer);
            fat32_dir_entry_t* entries = (fat32_dir_entry_t*)buffer;
            
            for (int i = 0; i < 16; i++) {
                if (entries[i].name[0] == 0) return 0; // Конец
                if (entries[i].name[0] == 0xE5) continue; // Удален
                if (entries[i].attr == 0x0F) continue; // LFN (пропускаем пока)
                if (entries[i].attr & 0x08) continue; // Label

                // Если это файл номер 'index'
                if (count == index) {
                    fat32_format_sfn(out->name, entries[i].name);
                    out->size = entries[i].file_size;
                    out->is_dir = (entries[i].attr & 0x10) ? 1 : 0;
                    return 1; // Успех
                }
                count++;
            }
        }
        cluster = get_next_cluster(inst, cluster);
    }
    
    return 0; // EOF
}

void fat32_close(fs_instance_t fs, fs_file_handle_t f) {
    free(f);
}

// Глобальная структура драйвера
fs_driver_t fat32_driver = {
    .mount = fat32_mount,
    .umount = fat32_umount,
    .open = fat32_open,
    .read = fat32_read,
    .close = fat32_close,
    .readdir = fat32_readdir
};