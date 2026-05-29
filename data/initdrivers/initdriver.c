#include <stdint.h>
#include <aoslib.h>

AOS_DECLARE_DRIVER(DT_INIT, 0, 0);

void hexdump(const uint8_t* buffer, uint64_t size) {
    if (!buffer || size == 0) {
        printf("hexdump: Invalid buffer or size 0\n");
        return;
    }

    // Проходим по буферу блоками по 16 байт
    for (uint64_t i = 0; i < size; i += 16) {
        // 1. Выводим смещение (offset) от начала буфера
        printf("%08X  ", (unsigned int)i);

        // 2. Выводим шестнадцатеричные значения (HEX)
        for (uint64_t j = 0; j < 16; j++) {
            if (i + j < size) {
                printf("%02X ", buffer[i + j]);
            } else {
                printf("   "); // Пустое место, если файл закончился
            }

            // Добавляем дополнительный пробел в середине (между 8 и 9 байтом)
            if (j == 7) {
                printf(" ");
            }
        }

        printf(" |");

        // 3. Выводим читаемые ASCII-символы
        for (uint64_t j = 0; j < 16; j++) {
            if (i + j < size) {
                uint8_t c = buffer[i + j];
                // Читаемыми считаются символы от 32 (пробел) до 126 (~)
                if (c >= 32 && c <= 126) {
                    printf("%c", c);
                } else {
                    printf("."); // Нечитаемые заменяем точкой
                }
            } else {
                printf(" "); // Пустое место
            }
        }

        printf("|\n");
    }
}

int spawn_driver(driver_type_t type, const char* name, const char* path) {
	startup_info_t info;
	memset(&info, 0, sizeof(startup_info_t));
	info.type = STARTUP_DRIVERMAIN;
	int res = sysspawn(path, &info, 0);
	if (res) return res;
	uint32_t pid = 0;
    if(!sleep_while_zero(get_driver_pid_sleep_wrapper, &type, 5000, (int*)&pid)) return -1;
	return 0;
}

void parse_drivers_conf(char* buffer) {
    char *line_saveptr;
    char *line = strtok_r(buffer, "\n", &line_saveptr);

    while (line != NULL) {
        if (line[0] == '\0' || line[0] == '#') {
            line = strtok_r(NULL, "\n", &line_saveptr);
            continue;
        }

        char *key = strtok(line, "=");
        char *path = strtok(NULL, "=");

        if (key && path) {
            driver_type_t type = DT_USER;
            char *name = NULL;

            char *semicolon = strchr(key, ';');
            
            if (semicolon) {
                *semicolon = '\0';
                type = dt_from_str(key);
                name = semicolon + 1;
            } else if (strncmp(key, "DT_", 3) == 0) {
                type = dt_from_str(key);
                name = NULL;
            } else {
                type = DT_USER;
                name = key;
            }

			int res = spawn_driver(type, name, path);
            printf("Registering [type=%d, name=%s, path=%s]: code=%d\n", type, name ? name : "system", path, res);
            
        }

        line = strtok_r(NULL, "\n", &line_saveptr);
    }
}

int driver_main(void* reserved1, void* reserved2) {	
	printf("AOS, Initdriver is here...\n");
	int res;
	
	int drvfd = vfs_open("/boot/Configs/drivers.conf", VFS_FREAD);
	if (drvfd < 0) {
		printf("INITDRIVER: vfs_open code = %d\n", drvfd);
		return -1;
	}
	
	printf("INITDRIVER: Config file opened\n");
	
	vfs_stat_info_t* drvstat = (vfs_stat_info_t*)malloc(sizeof(vfs_stat_info_t));
	if (!drvstat) {
		vfs_close(drvfd);
		printf("INITDRIVER: OOM\n");
		return STAT_OOM;
	}
	memset(drvstat, 0, sizeof(vfs_stat_info_t));
	res = vfs_stat(drvfd, drvstat);
	if (res) {
		free(drvstat);
		vfs_close(drvfd);
		printf("INITDRIVER: vfs_stat code = %d\n", res);
		return -1;
	}
	
	hexdump((uint8_t*)drvstat, sizeof(vfs_stat_info_t));
	
	uint64_t size = drvstat->size_bytes;
	free(drvstat);
	if (size == 0 || size == -1) { vfs_close(drvfd); return -1; }
	
	char* drvdata = (char*)calloc(size, sizeof(char));
	if (!drvdata) { vfs_close(drvfd); return STAT_OOM; }
	uint64_t total_read = 0;
	while (total_read < size) {
		res = vfs_read(drvfd, (void*)(drvdata + total_read), (int)(size - total_read));
		if (res <= 0) break;
		total_read += res;
	}
	vfs_close(drvfd);
	
	if (total_read != size) { free(drvdata); return -1; }
	
	drvdata[size] = '\0';
	
	parse_drivers_conf(drvdata);
	
	free(drvdata);
	
	return 0;
}