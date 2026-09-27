#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <aos/types.h>
#include <aos/window.h>
#include <aos/ipc.h>
#include <aos/input.h>
#include <aos/vfs.h>
#include <aos/syscalls.h>
#include <aos/utils.h>

#include <agfx.h>
#include <agfx_ui.h>

#define MAX_FILES 128
#define PATH_MAX  256

uint8_t* read_entire_file(const char* path, uint64_t* out_size) {
    int fd = vfs_open(path, VFS_FREAD);
    if (fd < 0) return NULL;

    vfs_stat_info_t stat;
    if (vfs_stat(fd, &stat) != 0 || stat.size_bytes <= 0) {
        vfs_close(fd);
        return NULL;
    }

    uint8_t* data = malloc(stat.size_bytes);
    if (data) {
        vfs_read(fd, data, stat.size_bytes);
        if (out_size) *out_size = stat.size_bytes;
    }
    vfs_close(fd);
    return data;
}

// Глобальное состояние приложения
char current_path[PATH_MAX] = "/";
vfs_dirent_t files[MAX_FILES];
int file_count = 0;

// Загрузка списка файлов из текущей папки
void refresh_directory() {
    file_count = 0;
    int fd = vfs_open(current_path, VFS_FREAD);
    if (fd < 0) {
        printf("AExplorer: Failed to open directory %s\n", current_path);
        return;
    }

    file_count = vfs_readdir(fd, files, MAX_FILES);
    vfs_close(fd);

    // Простая сортировка: сначала папки, потом файлы
    for (int i = 0; i < file_count - 1; i++) {
        for (int j = i + 1; j < file_count; j++) {
            int is_dir_i = (files[i].type == VFS_FILE_TYPE_DIR);
            int is_dir_j = (files[j].type == VFS_FILE_TYPE_DIR);
            
            if (!is_dir_i && is_dir_j) {
                vfs_dirent_t temp = files[i];
                files[i] = files[j];
                files[j] = temp;
            }
        }
    }
}

// Переход в папку или запуск файла
void handle_file_click(vfs_dirent_t* file) {
    if (file->type == VFS_FILE_TYPE_DIR) {
        // Переход в папку
        if (strcmp(file->name, "..") == 0) {
            // Идем наверх
            int len = strlen(current_path);
            if (len > 1) {
                len -= 2; // Пропускаем последний слэш
                while (len > 0 && current_path[len] != '/') len--;
                current_path[len + 1] = '\0';
            }
        } else if (strcmp(file->name, ".") != 0) {
            // Идем внутрь
            strlcat(current_path, file->name, PATH_MAX);
            strlcat(current_path, "/", PATH_MAX);
        }
        refresh_directory();
    } 
    else {
        // Запуск файла (если это .elf)
        int len = strlen(file->name);
        if (len > 4 && strcmp(file->name + len - 4, ".elf") == 0) {
            char full_path[PATH_MAX];
            snprintf(full_path, PATH_MAX, "%s%s", current_path, file->name);
            
            printf("AExplorer: Launching %s\n", full_path);
            
            startup_info_t info;
            memset(&info, 0, sizeof(startup_info_t));
            info.type = STARTUP_MAIN;
            info.state = THREAD_BLOCKED;
            
            apid_t pid;
            sysspawn(full_path, &info, 0, &pid);
        } else {
            printf("AExplorer: Cannot execute %s (not an .elf file)\n", file->name);
        }
    }
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("AExplorer: Starting...\n");

    agfx_set_allocators(malloc, free);

    int win_w = 600, win_h = 400;
    window_t* win = window_create(100, 100, win_w, win_h, WND_FLAG_NORMAL);
    if (!win) return -1;

    agfx_surface_t screen;
    agfx_init(&screen, win->buffer, win_w, win_h, win_w * 4);

    uint64_t font_size = 0;
	uint8_t* font_buf = read_entire_file("/boot/assets/selawk.ttf", &font_size);
	agfx_font_t font;
	int has_font = 0;

	if (font_buf) {
		has_font = agfx_font_init(&font, font_buf, 16);
		if (has_font) printf("AExplorer: Font loaded successfully!\n");
		else printf("AExplorer: Font init failed!\n");
	} else {
		printf("AExplorer: Failed to read font file!\n");
	}

    agfx_ui_context_t ui;
    agfx_ui_init(&ui, &screen, has_font ? &font : NULL);

    refresh_directory();

    int mouse_x = 0, mouse_y = 0;
    int mouse_down = 0, mouse_clicked = 0;
    int needs_redraw = 1;

    while (1) {
        message_t msg;
        while (ipc_tryrecv(&msg) == SYS_RES_OK) {
            if (msg.type == MSG_TYPE_INPUT && msg.subtype == MSG_SUBTYPE_SEND) {
                if (msg.param1 == INPUT_EVENT_MOUSE) {
                    mouse_x = (int16_t)(msg.param2 & 0xFFFF);
                    mouse_y = (int16_t)((msg.param2 >> 16) & 0xFFFF);
                    
                    int new_down = msg.param3 & 1;
                    if (new_down && !mouse_down) mouse_clicked = 1;
                    mouse_down = new_down;

                    agfx_ui_set_input(&ui, mouse_x, mouse_y, mouse_down, mouse_clicked);
                    needs_redraw = 1;
                }
            }
            else if (msg.type == MSG_TYPE_WND && msg.subtype == MSG_SUBTYPE_SEND) {
                if (msg.param1 == WND_CMD_DESTROY) {
                    printf("AExplorer: Closed by WindowManager.\n");
                    exit(0);
                }
            }
        }

        if (needs_redraw) {
            needs_redraw = 0;
            mouse_clicked = 0;
            
            agfx_fill_rect(&screen, 0, 0, win_w, win_h, ui.theme.bg);

            agfx_fill_rect(&screen, 0, 0, win_w, 40, ui.theme.control);
            agfx_draw_line(&screen, 0, 40, win_w, 40, 1, ui.theme.border);
            
            agfx_ui_begin(&ui, 10, 10);
            agfx_ui_label(&ui, "Path:");
            agfx_ui_same_line(&ui);
            agfx_ui_textbox(&ui, current_path, win_w - 80, 24, 0);

            agfx_ui_begin(&ui, 10, 50);
            
            if (strcmp(current_path, "/") != 0) {
                if (agfx_ui_button(&ui, "[ .. ] Up", win_w - 20, 30)) {
                    vfs_dirent_t up = { .name = "..", .type = VFS_FILE_TYPE_DIR };
                    handle_file_click(&up);
                    needs_redraw = 1;
                }
            }

            for (int i = 0; i < file_count; i++) {
                if (strcmp(files[i].name, ".") == 0 || strcmp(files[i].name, "..") == 0) continue;

                char btn_text[128];
                if (files[i].type == VFS_FILE_TYPE_DIR) {
                    snprintf(btn_text, sizeof(btn_text), "[DIR]  %s", files[i].name);
                } else {
                    snprintf(btn_text, sizeof(btn_text), "       %s  (%d bytes)", files[i].name, (int)files[i].size);
                }

                if (agfx_ui_button(&ui, btn_text, win_w - 20, 30)) {
                    handle_file_click(&files[i]);
                    needs_redraw = 1;
                }
            }

            window_flush(win);
        }
		
        ipc_recv(&msg);
        
        if (msg.type == MSG_TYPE_INPUT && msg.subtype == MSG_SUBTYPE_SEND) {
            if (msg.param1 == INPUT_EVENT_MOUSE) {
                mouse_x = (int16_t)(msg.param2 & 0xFFFF);
                mouse_y = (int16_t)((msg.param2 >> 16) & 0xFFFF);
                
                int new_down = msg.param3 & 1;
                if (new_down && !mouse_down) mouse_clicked = 1;
                mouse_down = new_down;

                agfx_ui_set_input(&ui, mouse_x, mouse_y, mouse_down, mouse_clicked);
                needs_redraw = 1;
            }
        }
        else if (msg.type == MSG_TYPE_WND && msg.subtype == MSG_SUBTYPE_SEND) {
            if (msg.param1 == WND_CMD_DESTROY) {
                printf("AExplorer: Closed by WindowManager.\n");
                exit(0);
            }
        }
    }

    return 0;
}