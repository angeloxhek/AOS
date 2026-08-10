#include <stdint.h>
#include <aoslib.h>

AOS_DECLARE_DRIVER(DT_INIT, 0, 0);

auth_id_t localroot;

int spawn_driver(driver_type_t type, const char* name, const char* path) {
	startup_info_t info;
	memset(&info, 0, sizeof(startup_info_t));
	info.type = STARTUP_DRIVERMAIN;
	int res = sysspawn(path, &info, 0, NULL);
	if (res) return res;
	apid_t pid = 0;
    if(!sleep_while_zero(get_driver_pid_sleep_wrapper, &type, 5000, (uint64_t*)&pid)) return -1;
	return 0;
}

#define MAX_ARGS 16

int parse_arguments(char* path, char* args_str, char** argv) {
    int argc = 0;
    
    argv[argc++] = path;

    if (!args_str) return argc;

    char* p = args_str;
    int in_quotes = 0;
    char* current_arg = NULL;

    while (*p) {
        if (!in_quotes && *p == ' ') {
            *p = '\0';
            current_arg = NULL;
        } 
        else if (*p == '"') {
            in_quotes = !in_quotes;
            *p = '\0';
        } 
        else {
            if (!current_arg) {
                if (argc < MAX_ARGS - 1) {
                    current_arg = p;
                    argv[argc++] = current_arg;
                }
            }
        }
        p++;
    }

    argv[argc] = NULL;
    return argc;
}

int spawn_application(char* path, char* args_str) {
    char* argv[MAX_ARGS];
    int argc = parse_arguments(path, args_str, argv);

    startup_info_t info;
    memset(&info, 0, sizeof(startup_info_t));
    info.type = STARTUP_MAIN;
	info.state = THREAD_BLOCKED;
    info.data.main.argc = argc;
    info.data.main.argv = argv;
    
    info.data.main.envc = 0; 
    info.data.main.envp = NULL; 
	
	apid_t pid = 0;
    int res = sysspawn(path, &info, 0, &pid);
	auth_set_id(pid, localroot);
	
	proc_info_user_t pinfo;
	get_proc_info(pid, &pinfo);
	
	
    return res;
}

void execute_command(char* cmd) {
    while (*cmd == ' ' || *cmd == '\n' || *cmd == '\r' || *cmd == '\t') cmd++;
    if (*cmd == '\0' || *cmd == '#') return;

    char* path = cmd;
    char* args = NULL;
    int in_quotes = 0;
    char* p = cmd;

    while (*p) {
        if (*p == '"') in_quotes = !in_quotes;
        else if (*p == ' ' && !in_quotes) {
            *p = '\0';
            args = p + 1;
            break;
        }
        p++;
    }

    if (args) {
        while (*args == ' ') args++;
    }

    char* tail = args ? args : path;
    int len = strlen(tail);
    while (len > 0 && (tail[len - 1] == ' ' || tail[len - 1] == '\n' || tail[len - 1] == '\r')) {
        tail[len - 1] = '\0';
        len--;
    }

    if (path[0] == '"' && path[strlen(path)-1] == '"') {
        path[strlen(path)-1] = '\0';
        path++;
    }

    int res = spawn_application(path, args);
    printf("INITDRIVER: Spawned APP [%s] Args: [%s] Code: %d\n", path, args ? args : "none", res);
}

void parse_apps_conf(char* buffer) {
    char* cmd_start = buffer;
    int in_quotes = 0;
    char* p = buffer;

    while (*p) {
        if (*p == '"') {
            in_quotes = !in_quotes;
        } 
        else if (*p == ';' && !in_quotes) {
            *p = '\0';
            execute_command(cmd_start);
            cmd_start = p + 1;
        }
        p++;
    }
    
    if (cmd_start < p) {
        execute_command(cmd_start);
    }
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

char* read_entire_file(const char* path) {
    int fd = vfs_open(path, VFS_FREAD);
    if (fd < 0) return NULL;

    vfs_stat_info_t stat;
    if (vfs_stat(fd, &stat) != 0 || stat.size_bytes <= 0) {
        vfs_close(fd);
        return NULL;
    }

    uint64_t size = stat.size_bytes;
    char* data = (char*)calloc(size + 1, sizeof(char));
    if (!data) {
        vfs_close(fd);
        return NULL;
    }

    uint64_t total_read = 0;
    while (total_read < size) {
        int res = vfs_read(fd, data + total_read, size - total_read);
        if (res <= 0) break;
        total_read += res;
    }
    vfs_close(fd);

    if (total_read != size) {
        free(data);
        return NULL;
    }
    
    data[size] = '\0';
    return data;
}

uint8_t* read_entire_binary(const char* path, uint64_t* out_size) {
    int fd = vfs_open(path, VFS_FREAD);
    if (fd < 0) return NULL;

    vfs_stat_info_t stat;
    if (vfs_stat(fd, &stat) != 0 || stat.size_bytes <= 0) {
        vfs_close(fd);
        return NULL;
    }

    uint64_t size = stat.size_bytes;
    uint8_t* data = (uint8_t*)calloc(size, 1);
    if (!data) { vfs_close(fd); return NULL; }

    uint64_t total_read = 0;
    while (total_read < size) {
        int res = vfs_read(fd, data + total_read, size - total_read);
        if (res <= 0) break;
        total_read += res;
    }
    vfs_close(fd);

    if (total_read != size) { free(data); return NULL; }
    
    *out_size = size;
    return data;
}

int driver_main(void* reserved1, void* reserved2) {	
	printf("AOS, Initdriver is here...\n");
	
	uint64_t auth_len = 0;
    uint8_t* authbase = read_entire_binary("/boot/configs/authbase.ahbs", &auth_len);
    
    if (authbase_load(authbase, auth_len)) {
		printf("INITDRIVER: Authbase failed/missing. Use 'localroot' / 'aoslocal'\n");
    }
	
	if (authbase) free(authbase);
	
	auth_idex_t* localrootex = (auth_idex_t*)malloc(sizeof(auth_idex_t));
	if (!localrootex) {
		printf("INITDRIVER: Failed to allocate memory!\n");
	} else {
		int get_res = auth_get_user_by_name("localroot", localrootex);
        if (get_res != 0) {
            printf("INITDRIVER: Failed to get localroot! Error code: %d\n", get_res);
        } else {
			printf("INITDRIVER: Got localroot successfully! UID: %d\n", localrootex->id.user.uid);
            localroot.raw = localrootex->id.raw;
        }
        free(localrootex);
    }
	
	char* drv_data = read_entire_file("/boot/configs/drivers.conf");
    if (drv_data) {
        printf("INITDRIVER: drivers.conf loaded\n");
        parse_drivers_conf(drv_data);
        free(drv_data);
    } else {
        printf("INITDRIVER: Failed to load drivers.conf!\n");
    }

    char* apps_data = read_entire_file("/boot/configs/apps.conf");
    if (apps_data) {
        printf("INITDRIVER: apps.conf loaded\n");
        parse_apps_conf(apps_data);
        free(apps_data);
    } else {
        printf("INITDRIVER: Failed to load apps.conf (or empty)!\n");
    }
	
	return 0;
}