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

char paths[2][PATH_MAX];
vfs_dirent_t files[2][MAX_FILES];
int is_dir[2][MAX_FILES];
int is_virtual[2][MAX_FILES];
int file_counts[2] = {0, 0};
int scrolls[2] = {0, 0};
int selected_idx[2] = {-1, -1}; 

int active_panel = PANEL_LEFT;

agfx_ui_context_t ui;

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

void refresh_panel(int p) {
    file_counts[p] = 0;
    scrolls[p] = 0;
	selected_idx[p] = -1;
    
    int fd = vfs_open(paths[p], VFS_FREAD);
    if (fd < 0) {
        printf("AExplorer: Failed to open directory %s\n", paths[p]);
        return;
    }

    file_counts[p] = vfs_readdir(fd, files[p], MAX_FILES);
    vfs_close(fd);

    for (int i = 0; i < file_counts[p]; i++) {
        is_dir[p][i] = (files[p][i].type == VFS_FILE_TYPE_DIR);
        is_virtual[p][i] = (files[p][i].is_virtual == 1) || (files[p][i].type == VFS_FILE_TYPE_DEVICE);
        
        if (files[p][i].type == VFS_FILE_TYPE_SYMLINK) {
            char full_path[PATH_MAX];
            snprintf(full_path, PATH_MAX, "%s%s", paths[p], files[p][i].name);
            int sfd = vfs_open(full_path, VFS_FREAD);
            if (sfd >= 0) {
                vfs_stat_info_t st;
                if (vfs_stat(sfd, &st) == 0) {
                    if (st.attributes & 0x10) is_dir[p][i] = 1;
                    if (st.attributes & 0x80000000) is_virtual[p][i] = 1;
                }
                vfs_close(sfd);
            }
        }
    }

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

                int temp_virt = is_virtual[p][i];
                is_virtual[p][i] = is_virtual[p][j];
                is_virtual[p][j] = temp_virt;
            }
        }
    }
	
	ui.force_redraw = 1;
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
        
		printf("AExplorer: Executing %s\n", full_path);
        startup_info_t info;
        memset(&info, 0, sizeof(startup_info_t));
        info.type = STARTUP_MAIN;
        info.state = THREAD_BLOCKED;
        
        apid_t pid;
        sysspawn(full_path, &info, 0, &pid);
    }
}

static int get_text_width(const agfx_font_t* font, const char* text) {
    if (!font || !text) return 0;
    int w = 0;
    for (int i = 0; text[i]; i++) {
        unsigned char c = text[i];
        if (c >= 32 && c <= 127) w += font->glyph_cache[c - 32].advance;
    }
    return w;
}

static void fast_format_size(char *buf, size_t buf_size, unsigned long long value, const char *suffix) {
    char temp[32];
    int i = 0;
    
    if (value == 0) {
        temp[i++] = '0';
    } else {
        while (value > 0 && i < 20) {
            temp[i++] = (value % 10) + '0';
            value /= 10;
        }
    }
    
    int out = 0;
    while (i > 0 && out < (int)buf_size - 1) {
        buf[out++] = temp[--i];
    }
    buf[out] = '\0';
    
    strlcat(buf, suffix, buf_size); 
}


int main(int argc, char** argv) {
    UNUSED(argc); UNUSED(argv);
    printf("AExplorer: Starting...\n");

    agfx_set_allocators(malloc, free);

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

    agfx_ui_init(&ui, &screen, has_font ? &font : NULL);

    strlcpy(paths[PANEL_LEFT], "/", PATH_MAX);
    strlcpy(paths[PANEL_RIGHT], "/hw/", PATH_MAX);
    
    refresh_panel(PANEL_LEFT);
    refresh_panel(PANEL_RIGHT);

    int mouse_x = 0, mouse_y = 0;
    int mouse_down = 0, mouse_clicked = 0;
    int needs_redraw = 1;

    int topbar_h = 36;
    int bottombar_h = 40;
    int panel_w = (win_w - 30) / 2;
    int panel_h = win_h - topbar_h - bottombar_h - 20;

    while (1) {
        message_t msg;
        
        ipc_recv(&msg);

        do {
            if (msg.type == MSG_TYPE_INPUT && msg.subtype == MSG_SUBTYPE_SEND) {
                if (msg.param1 == INPUT_EVENT_MOUSE) {
                    mouse_x = (int16_t)(msg.param2 & 0xFFFF);
                    mouse_y = (int16_t)((msg.param2 >> 16) & 0xFFFF);
                    
                    int new_down = msg.param3 & 1;
                    if (new_down && !mouse_down) mouse_clicked = 1;
                    mouse_down = new_down;

                    needs_redraw = 1;
                }
            }
            else if (msg.type == MSG_TYPE_WND && msg.subtype == MSG_SUBTYPE_SEND) {
                if (msg.param1 == WND_CMD_DESTROY) exit(0);
            }
        } while (ipc_tryrecv(&msg) == SYS_RES_OK);

        agfx_ui_set_input(&ui, mouse_x, mouse_y, mouse_down, mouse_clicked);

        if (needs_redraw) {
            needs_redraw = 0;
            mouse_clicked = 0;
			
			ui.hot_item = 0;
            if (!ui.mouse_down) ui.active_item = 0;
			
			int action_go_up = -1;
            int action_click_panel = -1;
            int action_click_idx = -1;
            
            agfx_fill_rect(&screen, 0, 0, win_w, win_h, ui.theme.bg);

            for (int p = 0; p < 2; p++) {
                int px = (p == 0) ? 10 : (20 + panel_w);
                int py = 10;

                uint32_t path_bg = (active_panel == p) ? ui.theme.accent : ui.theme.control;
                
                agfx_fill_rect(&screen, px, py, panel_w, topbar_h, path_bg);
                agfx_draw_rect(&screen, px, py, panel_w, topbar_h, 1, ui.theme.border);
                
                agfx_ui_begin(&ui, px + 10, py + 6);
                if (agfx_ui_button(&ui, "<", 30, 24)) {
                    action_go_up = p;
                }
                agfx_ui_same_line(&ui);
                agfx_ui_label(&ui, paths[p]);

                uint32_t original_bg = ui.theme.bg;
                if (active_panel != p) {
                    ui.theme.bg = 0xFF151515; 
                }

                agfx_ui_begin_panel(&ui, px, py + topbar_h + 5, panel_w, panel_h, &scrolls[p]);
                
                for (int i = 0; i < file_counts[p]; i++) {
                    if (strcmp(files[p][i].name, ".") == 0) continue;

                    if (ui.cursor_y + 24 < ui.panel_y || ui.cursor_y > ui.panel_y + ui.panel_h) {
                        agfx_ui_advance(&ui, panel_w - 20, 24);
						ui.current_item_id++;
                        continue;
                    }

                    int item_w = panel_w - 20;
                    int item_h = 24;
                    int bx, by, bstate;
                    
                    int clicked = agfx_ui_button_behavior(&ui, item_w, item_h, &bx, &by, &bstate);
                    
                    int is_selected = (active_panel == p && selected_idx[p] == i);
                    
                    if (is_selected) {
                        agfx_fill_rect(&screen, bx, by, item_w, item_h, ui.theme.accent);
                    } else if (bstate == AGFX_UI_STATE_PRESSED) {
                        agfx_fill_rect(&screen, bx, by, item_w, item_h, ui.theme.control_pressed);
                    } else if (bstate == AGFX_UI_STATE_HOVER) {
                        agfx_fill_rect(&screen, bx, by, item_w, item_h, ui.theme.control_hover);
                    }

                    uint32_t text_color = is_selected ? 0xFFFFFFFF : ui.theme.text;
                    char name_buf[128];
                    int is_directory = is_dir[p][i];
                    int is_virt = is_virtual[p][i];
                    
                    if (files[p][i].type == VFS_FILE_TYPE_DIR) {
                        strlcpy(name_buf, files[p][i].name, sizeof(name_buf));
						strlcat(name_buf, "/", sizeof(name_buf));
                        if (!is_selected) text_color = 0xFFFFFFFF;
                    } else if (files[p][i].type == VFS_FILE_TYPE_SYMLINK) {
                        if (is_directory) {
							strlcpy(name_buf, files[p][i].name, sizeof(name_buf));
							strlcat(name_buf, "/", sizeof(name_buf));
						}
                        else strlcpy(name_buf, files[p][i].name, sizeof(name_buf));
                        if (!is_selected) text_color = 0xFF5CE6E6;
                    } else if (is_virt || files[p][i].type == VFS_FILE_TYPE_DEVICE) {
                        strlcpy(name_buf, files[p][i].name, sizeof(name_buf));
                        if (!is_selected) text_color = 0xFF5CE673;
                    } else {
                        strlcpy(name_buf, files[p][i].name, sizeof(name_buf));
                        if (!is_selected) text_color = 0xFFCCCCCC;
                    }

                    if (has_font) {
                        int th = (int)((ui.font->ascent - ui.font->descent) * ui.font->scale);
                        agfx_draw_text(&screen, ui.font, name_buf, bx + 5, by + (item_h - th) / 2, text_color);
                    }

                    if (!is_directory && !is_virt && files[p][i].type != VFS_FILE_TYPE_DEVICE) {
						char size_buf[32];
						unsigned long long size = (unsigned long long)files[p][i].size;

						if (size > 1024 * 1024) {
							fast_format_size(size_buf, sizeof(size_buf), size / (1024 * 1024), " MB");
						} else if (size > 1024) {
							fast_format_size(size_buf, sizeof(size_buf), size / 1024, " KB");
						} else {
							fast_format_size(size_buf, sizeof(size_buf), size, " B");
						}

						if (has_font) {
							int th = (int)((ui.font->ascent - ui.font->descent) * ui.font->scale);
							int tw = get_text_width(ui.font, size_buf);
							uint32_t size_color = is_selected ? 0xFFFFFFFF : ui.theme.text_secondary;
							
							agfx_draw_text(&screen, ui.font, size_buf, bx + item_w - tw - 5, by + (item_h - th) / 2, size_color);
						}
					}

                    if (clicked) {
                        action_click_panel = p;
                        action_click_idx = i;
                    }
                }
                agfx_ui_end_panel(&ui);
                
                ui.theme.bg = original_bg;
                
                uint32_t border_color = (active_panel == p) ? ui.theme.accent : ui.theme.border;
                agfx_draw_rect(&screen, px, py + topbar_h + 5, panel_w, panel_h, 1, border_color);
            }

            int bottom_y = win_h - bottombar_h;
            
            agfx_fill_rect(&screen, 0, bottom_y, win_w, bottombar_h, 0xFF151515);
            agfx_draw_line(&screen, 0, bottom_y, win_w, bottom_y, 1, ui.theme.border);

            agfx_ui_begin(&ui, 10, bottom_y + 6);
            
            int btn_w = (win_w - 80) / 6;
            
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
            if (agfx_ui_button(&ui, "F8 Delete", btn_w, 28)) {
                if (file_counts[active_panel] > 0) {
                    int idx = selected_idx[active_panel];
                    vfs_dirent_t* target_file = &files[active_panel][idx];
                    
                    char target_path[PATH_MAX];
                    snprintf(target_path, PATH_MAX, "%s%s", paths[active_panel], target_file->name);
                    
                    if (is_dir[active_panel][idx]) {
                        printf("AOS Commander: Cannot delete directory %s\n", target_path);
                    } 
                    else if (is_virtual[active_panel][idx] || target_file->type == VFS_FILE_TYPE_DEVICE) {
                        printf("AOS Commander: Cannot delete virtual file %s\n", target_path);
                    }
                    else {
                        printf("AOS Commander: Deleting %s...\n", target_path);
                        
                        if (vfs_unlink(target_path) == 0) {
                            printf("AOS Commander: Deleted successfully.\n");
                            refresh_panel(active_panel);
                            needs_redraw = 1;
                        } else {
                            printf("AOS Commander: Failed to delete %s\n", target_path);
                        }
                    }
                }
            }

            if (action_go_up != -1) {
                go_up(action_go_up);
                active_panel = action_go_up;
                needs_redraw = 1;
                ui.force_redraw = 1;
            }
            else if (action_click_panel != -1) {
                int p = action_click_panel;
                int idx = action_click_idx;
                active_panel = p;

                if (selected_idx[p] == idx) {
					selected_idx[p] = -1;
                    handle_file_click(p, &files[p][idx]);
                } else {
                    selected_idx[p] = idx;
                }
                
                needs_redraw = 1;
                ui.force_redraw = 1;
            }

            if (ui.force_redraw || 
                ui.hot_item != ui.last_hot_item || 
                ui.active_item != ui.last_active_item) 
            {
                window_flush(win);
                ui.force_redraw = 0;
            }
            
            ui.last_hot_item = ui.hot_item;
            ui.last_active_item = ui.active_item;
        }
    }

    return 0;
}