#include <stdint.h>
#include <aoslib.h>

AOS_DECLARE_DRIVER(DT_INIT, 0, 0);

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
			int path_len = strlen(path);
            while (path_len > 0 && (path[path_len - 1] == '\r' || path[path_len - 1] == ' ')) {
                path[path_len - 1] = '\0';
                path_len--;
            }
			
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
            printf("INITDRIVER: Registering [type=%d, name=%s, path=%s]: code=%d\n", type, name ? name : "system", path, res);
            
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
	
	uint64_t size = drvstat->size_bytes;
	free(drvstat);
	if (size == 0 || size == -1) {
		vfs_close(drvfd);
		printf("INITDRIVER: config size = %d\n", size);
		return -1;
	}
	
	char* drvdata = (char*)calloc(size+1, sizeof(char));
	if (!drvdata) {
		vfs_close(drvfd);
		printf("INITDRIVER: OOM\n");
		return STAT_OOM;
	}
	uint64_t total_read = 0;
	while (total_read < size) {
		res = vfs_read(drvfd, (void*)(drvdata + total_read), (int)(size - total_read));
		if (res <= 0) break;
		total_read += res;
	}
	vfs_close(drvfd);
	
	if (total_read != size) {
		free(drvdata);
		printf("INITDRIVER: total_read != size => %d != %d\n", total_read, size);
		return -1;
	}
	
	drvdata[size] = '\0';
	
	parse_drivers_conf(drvdata);
	
	free(drvdata);
	
	return 0;
}