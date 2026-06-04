#include <stdint.h>
#include <aoslib.h>

#define VBE_DISPI_IOPORT_INDEX 0x01CE
#define VBE_DISPI_IOPORT_DATA  0x01CF
#define VBE_DISPI_INDEX_ID     0
#define VBE_DISPI_INDEX_Y_OFFSET 9

AOS_DECLARE_DRIVER(DT_VIDEO, DRV_PERM_IO_PORTS | DRV_PERM_PHYS_MAP | DRV_PERM_EDIT_SYSTEM_FLAGS | DRV_PERM_GET_SPEC_INFO, 0xFFFF);

uint32_t* framebuffer = 0;
sys_video_t vinfo;

void* wm_backbuffer = 0;
uint64_t wm_backbuffer_shm_id = 0;

int is_bga_available = 0;
int current_page = 0;

uint32_t make_color(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t color = 0;
    color |= ((uint32_t)r << vinfo.red_mask_shift);
    color |= ((uint32_t)g << vinfo.green_mask_shift);
    color |= ((uint32_t)b << vinfo.blue_mask_shift);
    return color;
}

void fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (x >= vinfo.width || y >= vinfo.height) return;
    if (x + w > vinfo.width) w = vinfo.width - x;
    if (y + h > vinfo.height) h = vinfo.height - y;

    uint8_t bytes_per_pixel = vinfo.bpp / 8;

    if (vinfo.bpp == 32) {
        for (uint32_t cy = y; cy < y + h; cy++) {
            uint32_t* row = (uint32_t*)((uint8_t*)framebuffer + (cy * vinfo.pitch));
            for (uint32_t cx = x; cx < x + w; cx++) {
                row[cx] = color;
            }
        }
    } else {
        for (uint32_t cy = y; cy < y + h; cy++) {
            uint8_t* row = (uint8_t*)framebuffer + (cy * vinfo.pitch);
            for (uint32_t cx = x; cx < x + w; cx++) {
                uint32_t offset = cx * bytes_per_pixel;
                row[offset] = color & 0xFF;
                row[offset+1] = (color >> 8) & 0xFF;
                row[offset+2] = (color >> 16) & 0xFF;
            }
        }
    }
}

void blit_buffer(uint32_t x, uint32_t y, uint32_t w, uint32_t h, void* buffer) {
    if (!buffer || x >= vinfo.width || y >= vinfo.height) return;
    
    if (x + w > vinfo.width) w = vinfo.width - x;
    if (y + h > vinfo.height) h = vinfo.height - y;

    uint8_t bytes_per_pixel = vinfo.bpp / 8;
    uint8_t* src = (uint8_t*)buffer;
    uint32_t src_pitch = w * bytes_per_pixel;

    for (uint32_t cy = 0; cy < h; cy++) {
        uint64_t dest_offset = ((y + cy) * vinfo.pitch) + (x * bytes_per_pixel);
        uint8_t* dest = (uint8_t*)framebuffer + dest_offset;
        
        memcpy(dest, src + (cy * src_pitch), src_pitch);
    }
}

void handle_video_request(message_t* in) {
    message_t out;
    memset(&out, 0, sizeof(message_t));
    out.type = MSG_TYPE_VIDEO;
    out.subtype = MSG_SUBTYPE_RESPONSE;

    switch (in->param1) {
        
        case VIDEO_CMD_BLIT: {
            uint32_t x = in->param2 >> 16;
            uint32_t y = in->param2 & 0xFFFF;
            uint32_t w = in->param3 >> 16;
            uint32_t h = in->param3 & 0xFFFF;
            
            uint64_t shm_id = *(uint64_t*)(in->data);
            
            void* pixels = shm_map(shm_id);
            if (pixels) {
                blit_buffer(x, y, w, h, pixels);
                out.param1 = VIDEO_ERR_OK;
            } else {
                out.param1 = VIDEO_ERR_UNKNOWN;
            }
            
            shm_free(shm_id);
            break;
        }

        case VIDEO_CMD_FILL_RECT: {
            uint32_t x = in->param2 >> 16;
            uint32_t y = in->param2 & 0xFFFF;
            uint32_t w = in->param3 >> 16;
            uint32_t h = in->param3 & 0xFFFF;
            
            uint32_t color = *(uint32_t*)(in->data);
            
            fill_rect(x, y, w, h, color);
            out.param1 = VIDEO_ERR_OK;
            break;
        }
		
		case VIDEO_CMD_SET_BACKBUFFER: {
            uint64_t shm_id = *(uint64_t*)(in->data);
            
            if (wm_backbuffer_shm_id != 0) {
                shm_free(wm_backbuffer_shm_id);
            }
            
            wm_backbuffer = shm_map(shm_id);
            if (wm_backbuffer) {
                wm_backbuffer_shm_id = shm_id;
                out.param1 = VIDEO_ERR_OK;
            } else {
                wm_backbuffer_shm_id = 0;
                out.param1 = VIDEO_ERR_UNKNOWN;
            }
            break;
        }

        case VIDEO_CMD_FLUSH_RECTS: {
            if (!wm_backbuffer) {
                out.param1 = -1;
                break;
            }

            uint64_t rect_count = (uint64_t)in->param2;
            uint64_t shm_id = *(uint64_t*)(in->data);
            
            video_rect_t* rects = (video_rect_t*)shm_map(shm_id);
            if (!rects) {
                out.param1 = -1;
                break;
            }

            uint8_t bytes_per_pixel = vinfo.bpp / 8;

            if (is_bga_available) {
                uint32_t hidden_y = (current_page == 0) ? vinfo.height : 0;
                uint64_t vram_offset = hidden_y * vinfo.pitch;
                uint8_t* hidden_vram = (uint8_t*)framebuffer + vram_offset;

                hal_memcpy_toio(hidden_vram, wm_backbuffer, vinfo.height * vinfo.pitch);

                hal_outw(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_Y_OFFSET);
                hal_outw(VBE_DISPI_IOPORT_DATA, hidden_y);
                
                current_page = (current_page == 0) ? 1 : 0;
            } else {
                for (uint64_t i = 0; i < rect_count; i++) {
                    uint32_t x = rects[i].x;
                    uint32_t y = rects[i].y;
                    uint32_t w = rects[i].w;
                    uint32_t h = rects[i].h;

                    if (x >= vinfo.width || y >= vinfo.height) continue;
                    if (x + w > vinfo.width) w = vinfo.width - x;
                    if (y + h > vinfo.height) h = vinfo.height - y;

                    for (uint32_t cy = 0; cy < h; cy++) {
                        uint64_t offset = ((y + cy) * vinfo.pitch) + (x * bytes_per_pixel);
                        
                        uint8_t* dest = (uint8_t*)framebuffer + offset;
                        uint8_t* src  = (uint8_t*)wm_backbuffer + offset;
                        
                        hal_memcpy_toio(dest, src, w * bytes_per_pixel);
                    }
                }
            }

            shm_free(shm_id);
            out.param1 = 0;
            break;
        }

        default: {
            out.param1 = VIDEO_ERR_NOCOMM;
            break;
        }
    }
    
    ipc_send(in->sender_pid, &out);
}

int driver_main(void* reserved1, void* reserved2) {
    printf("VideoDriver: Initializing...\n");

    if (sysget_spec_info(SPEC_INFO_VIDEO, &vinfo) != SYS_RES_OK) {
        printf("VideoDriver: Failed to get video info!\n");
        return -1;
    }

    printf("VideoDriver: Screen %dx%d, BPP: %d, Phys: 0x%X\n", 
           vinfo.width, vinfo.height, vinfo.bpp, (uint64_t)vinfo.framebuffer_addr);

    uint64_t size_bytes = vinfo.height * vinfo.pitch;

    hal_outw(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_ID);
    uint16_t bga_id = hal_inw(VBE_DISPI_IOPORT_DATA);
    
    if (bga_id >= 0xB0C0 && bga_id <= 0xB0C5) {
        printf("VideoDriver: BGA hardware detected! Page Flipping ENABLED.\n");
        is_bga_available = 1;

        hal_outw(VBE_DISPI_IOPORT_INDEX, 4); // VBE_DISPI_INDEX_ENABLE
        hal_outw(VBE_DISPI_IOPORT_DATA, 0); 
        
        hal_outw(VBE_DISPI_IOPORT_INDEX, 7); // VBE_DISPI_INDEX_VIRT_HEIGHT
        hal_outw(VBE_DISPI_IOPORT_DATA, vinfo.height * 2);

        // 0x01 = Enable, 0x40 = LFB
        hal_outw(VBE_DISPI_IOPORT_INDEX, 4); 
        hal_outw(VBE_DISPI_IOPORT_DATA, 0x01 | 0x40); 

        size_bytes *= 2; 
    } else {
        printf("VideoDriver: Real hardware detected. Using software rendering.\n");
        is_bga_available = 0;
    }

    uint64_t mapped_vaddr = 0;
    if (sysmap_phys(vinfo.framebuffer_addr, size_bytes, &mapped_vaddr) != SYS_RES_OK) {
        printf("VideoDriver: Failed to map framebuffer! Check permissions.\n");
        return -1;
    }
    
    framebuffer = (uint32_t*)mapped_vaddr;

    system_info_t info;
    get_sysinfo(&info);
    uint32_t sysflags = info.flags;
    
    sysflags &= ~CAN_PRINT;

    sysedit_sys_flags(sysflags);

    uint32_t bg_color = make_color(0, 0, 0);
    fill_rect(0, 0, vinfo.width, vinfo.height, bg_color);
    
    printf("VideoDriver: Ready and waiting for commands...\n");

    message_t msg;
    while(1) {
        ipc_recv_ex(0, MSG_TYPE_VIDEO, MSG_SUBTYPE_NONE, &msg);
        
        handle_video_request(&msg);
    }

    return 0;
}