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

#define PANEL_LEFT  0
#define PANEL_RIGHT 1

// Состояние двух панелей
char paths[2][PATH_MAX];
vfs_dirent_t files[2][MAX_FILES];
int is_dir[2][MAX_FILES];
int file_counts[2] = {0, 0};
int scrolls[2] = {0, 0};

int active_panel = PANEL_LEFT;

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

// Загрузка списка файлов для конкретной панели
void refresh_panel(int p) {
    file_counts[p] = 0;
    scrolls[p] = 0; 
    
    int fd = vfs_open(paths[p], VFS_FREAD);
    if (fd < 0) {
        printf("AOS Commander: Failed to open directory %s\n", paths[p]);
        return;
    }

    file_counts[p] = vfs_readdir(fd, files[p], MAX_FILES);
    vfs_close(fd);

    // Определяем, кто является папкой (включая симлинки на папки)
    for (int i = 0; i < file_counts[p]; i++) {
        is_dir[p][i] = (files[p][i].type == VFS_FILE_TYPE_DIR);
        
        if (files[p][i].type == VFS_FILE_TYPE_SYMLINK) {
            char full_path[PATH_MAX];
            snprintf(full_path, PATH_MAX, "%s%s", paths[p], files[p][i].name);
            int sfd = vfs_open(full_path, VFS_FREAD);
            if (sfd >= 0) {
                vfs_stat_info_t st;
                if (vfs_stat(sfd, &st) == 0 && (st.attributes & 0x10)) {
                    is_dir[p][i] = 1; // Симлинк указывает на директорию
                }
                vfs_close(sfd);
            }
        }
    }

    // Сортировка: папки сверху, файлы снизу, по алфавиту
    for (int i = 0; i < file_counts[p] - 1; i++) {
        for (int j = i + 1; j < file_counts[p]; j++) {
            int swap = 0;
            if (!is_dir[p][i] && is_dir[p][j]) {
                swap = 1;
            } else if (is_dir[p][i] == is_dir[p][j]) {
                if (strcmp(files[p][i].name, files[p][j].name) > 0) swap = 1;
            }

            if (swap) {
                vfs_dirent_t temp = files[p][i];
                files[p][i] = files[p][j];
                files[p][j] = temp;
                
                int temp_dir = is_dir[p][i];
                is_dir[p][i] = is_dir[p][j];
                is_dir[p][j] = temp_dir;
            }
        }
    }
}

void go_up(int p) {
    if (strcmp(paths[p], "/") == 0) return;
    
    int len = strlen(paths[p]);
    if (len > 1) {
        len -= 2; 
        while (len > 0 && paths[p][len] != '/') len--;
        paths[p][len + 1] = '\0';
    }
    refresh_panel(p);
}

void handle_file_click(int p, vfs_dirent_t* file) {
    active_panel = p;

    if (strcmp(file->name, "..") == 0) {
        go_up(p);
        return;
    }

    int idx = file - files[p];

    if (is_dir[p][idx]) {
        strlcat(paths[p], file->name, PATH_MAX);
        strlcat(paths[p], "/", PATH_MAX);
        refresh_panel(p);
    } 
    else {
        char full_path[PATH_MAX];
        snprintf(full_path, PATH_MAX, "%s%s", paths[p], file->name);
        
        int len = strlen(file->name);
        if (len > 4 && strcmp(file->name + len - 4, ".elf") == 0) {
            printf("AOS Commander: Executing %s\n", full_path);
            startup_info_t info;
            memset(&info, 0, sizeof(startup_info_t));
            info.type = STARTUP_MAIN;
            info.state = THREAD_BLOCKED;
            
            apid_t pid;
            sysspawn(full_path, &info, 0, &pid);
        } else {
            printf("AOS Commander: No associated app for %s\n", file->name);
        }
    }
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("AOS Commander: Starting...\n");

    agfx_set_allocators(malloc, free);

    // Большое окно для двух панелей
    int win_w = 800, win_h = 500;
    window_t* win = window_create(50, 50, win_w, win_h, WND_FLAG_NORMAL);
    if (!win) return -1;

    agfx_surface_t screen;
    agfx_init(&screen, win->buffer, win_w, win_h, win_w * 4);

    uint64_t font_size = 0;
    uint8_t* font_buf = read_entire_file("/boot/assets/selawk.ttf", &font_size);
    agfx_font_t font;
    int has_font = 0;

    if (font_buf) {
        has_font = agfx_font_init(&font, font_buf, 16);
    }

    agfx_ui_context_t ui;
    agfx_ui_init(&ui, &screen, has_font ? &font : NULL);

    // Инициализация путей
    strlcpy(paths[PANEL_LEFT], "/", PATH_MAX);
    strlcpy(paths[PANEL_RIGHT], "/hw/", PATH_MAX); // Правая панель смотрит в устройства
    
    refresh_panel(PANEL_LEFT);
    refresh_panel(PANEL_RIGHT);

    int mouse_x = 0, mouse_y = 0;
    int mouse_down = 0, mouse_clicked = 0;
    int needs_redraw = 1;

    int topbar_h = 36;
    int bottombar_h = 40;
    int panel_w = (win_w - 30) / 2; // Две панели с отступами
    int panel_h = win_h - topbar_h - bottombar_h - 20;

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
                if (msg.param1 == WND_CMD_DESTROY) exit(0);
            }
        }

        if (needs_redraw) {
            needs_redraw = 0;
            mouse_clicked = 0;
            
            agfx_fill_rect(&screen, 0, 0, win_w, win_h, ui.theme.bg);

            // ==========================================
            // ОТРИСОВКА ПАНЕЛЕЙ (Левая и Правая)
            // ==========================================
            for (int p = 0; p < 2; p++) {
                int px = (p == 0) ? 10 : (20 + panel_w);
                int py = 10;

                // Подсветка активной панели
                uint32_t path_bg = (active_panel == p) ? ui.theme.accent : ui.theme.control;
                
                // Верхняя плашка с путем
                agfx_fill_rect(&screen, px, py, panel_w, topbar_h, path_bg);
                agfx_draw_rect(&screen, px, py, panel_w, topbar_h, 1, ui.theme.border);
                
                agfx_ui_begin(&ui, px + 10, py + 6);
                if (agfx_ui_button(&ui, "<", 30, 24)) {
                    go_up(p);
                    active_panel = p;
                    needs_redraw = 1;
                }
                agfx_ui_same_line(&ui);
                agfx_ui_label(&ui, paths[p]);

                // Затемняем фон неактивной панели
                uint32_t original_bg = ui.theme.bg;
                if (active_panel != p) {
                    ui.theme.bg = 0xFF151515; 
                }

                // Сама панель с файлами
                agfx_ui_begin_panel(&ui, px, py + topbar_h + 5, panel_w, panel_h, &scrolls[p]);
                
                for (int i = 0; i < file_counts[p]; i++) {
                    if (strcmp(files[p][i].name, ".") == 0) continue;

                    if (ui.cursor_y + 24 < ui.panel_y || ui.cursor_y > ui.panel_y + ui.panel_h) {
                        agfx_ui_advance(&ui, panel_w - 20, 24);
                        continue;
                    }

                    int item_w = panel_w - 20;
                    int item_h = 24;
                    int bx, by, bstate;
                    
                    int clicked = agfx_ui_button_behavior(&ui, item_w, item_h, &bx, &by, &bstate);
                    
                    if (bstate == AGFX_UI_STATE_PRESSED) {
                        agfx_fill_rect(&screen, bx, by, item_w, item_h, ui.theme.control_pressed);
                    } else if (bstate == AGFX_UI_STATE_HOVER) {
                        agfx_fill_rect(&screen, bx, by, item_w, item_h, ui.theme.control_hover);
                    }

                    // Цветовая дифференциация
                    // Цветовая дифференциация и имя
                    uint32_t text_color = ui.theme.text;
                    char name_buf[128];
                    int is_directory = is_dir[p][i];
                    
                    if (files[p][i].type == VFS_FILE_TYPE_DIR) {
                        snprintf(name_buf, sizeof(name_buf), "%s/", files[p][i].name);
                        text_color = 0xFFFFFFFF; // Белый для папок
                    } else if (files[p][i].type == VFS_FILE_TYPE_SYMLINK) {
                        // Если симлинк на папку — добавляем слеш!
                        if (is_directory) snprintf(name_buf, sizeof(name_buf), "%s/", files[p][i].name);
                        else snprintf(name_buf, sizeof(name_buf), "%s", files[p][i].name);
                        
                        text_color = 0xFF5CE6E6; // Бирюзовый для симлинков
                    } else if (files[p][i].type == VFS_FILE_TYPE_DEVICE) {
                        snprintf(name_buf, sizeof(name_buf), "%s", files[p][i].name);
                        text_color = 0xFF5CE673; // Зеленый для устройств
                    } else {
                        snprintf(name_buf, sizeof(name_buf), "%s", files[p][i].name);
                        text_color = 0xFFCCCCCC; // Светло-серый для файлов
                    }

                    int tw, th;
                    uint8_t* mask = agfx_mask_generate_string(ui.font, name_buf, &tw, &th);
                    if (mask) {
                        agfx_fill_alpha_mask(&screen, bx + 5, by + (item_h - th) / 2, tw, th, mask, text_color);
                        agfx_mask_free(mask);
                    }

                    // Отрисовка размера: скрываем для папок, симлинков-папок И устройств!
                    if (!is_directory && files[p][i].type != VFS_FILE_TYPE_DEVICE) {
                        char size_buf[32];
                        if (files[p][i].size > 1024 * 1024) {
                            snprintf(size_buf, sizeof(size_buf), "%d MB", (int)(files[p][i].size / (1024 * 1024)));
                        } else if (files[p][i].size > 1024) {
                            snprintf(size_buf, sizeof(size_buf), "%d KB", (int)(files[p][i].size / 1024));
                        } else {
                            snprintf(size_buf, sizeof(size_buf), "%d B", (int)files[p][i].size);
                        }

                        uint8_t* smask = agfx_mask_generate_string(ui.font, size_buf, &tw, &th);
                        if (smask) {
                            agfx_fill_alpha_mask(&screen, bx + item_w - tw - 5, by + (item_h - th) / 2, tw, th, smask, ui.theme.text_secondary);
                            agfx_mask_free(smask);
                        }
                    }

                    if (clicked) {
                        handle_file_click(p, &files[p][i]);
                        needs_redraw = 1;
                    }
                }
                agfx_ui_end_panel(&ui);
                
                // Возвращаем оригинальный фон
                ui.theme.bg = original_bg;
                
                // Рамка вокруг панели
                uint32_t border_color = (active_panel == p) ? ui.theme.accent : ui.theme.border;
                agfx_draw_rect(&screen, px, py + topbar_h + 5, panel_w, panel_h, 1, border_color);
            }

            // ==========================================
            // НИЖНЯЯ ПАНЕЛЬ (Кнопки действий)
            // ==========================================
            int bottom_y = win_h - bottombar_h;
            
            // Делаем фон нижней панели темнее, чтобы стандартные кнопки выделялись
            agfx_fill_rect(&screen, 0, bottom_y, win_w, bottombar_h, 0xFF151515);
            agfx_draw_line(&screen, 0, bottom_y, win_w, bottom_y, 1, ui.theme.border);

            agfx_ui_begin(&ui, 10, bottom_y + 6);
            
            int btn_w = (win_w - 80) / 6; // 6 кнопок
            
            if (agfx_ui_button(&ui, "F3 View", btn_w, 28)) printf("View not implemented\n");
            agfx_ui_same_line(&ui);
            if (agfx_ui_button(&ui, "F4 Edit", btn_w, 28)) printf("Edit not implemented\n");
            agfx_ui_same_line(&ui);
            if (agfx_ui_button(&ui, "F5 Copy", btn_w, 28)) printf("Copy not implemented\n");
            agfx_ui_same_line(&ui);
            if (agfx_ui_button(&ui, "F6 Move", btn_w, 28)) printf("Move not implemented\n");
            agfx_ui_same_line(&ui);
            if (agfx_ui_button(&ui, "F7 Mkdir", btn_w, 28)) printf("Mkdir not implemented\n");
            agfx_ui_same_line(&ui);
            if (agfx_ui_button(&ui, "F8 Delete", btn_w, 28)) printf("Delete not implemented\n");

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
            if (msg.param1 == WND_CMD_DESTROY) exit(0);
        }
    }

    return 0;
}