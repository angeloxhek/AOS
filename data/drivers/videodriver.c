#include <stdint.h>
#include <aoslib.h>

AOS_DECLARE_DRIVER(DT_VIDEO, DRV_PERM_PHYS_MAP | DRV_PERM_EDIT_SYSTEM_FLAGS | DRV_PERM_GET_SPEC_INFO, 0);

uint32_t* framebuffer = 0;
sys_video_t vinfo;

uint32_t make_color(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t color = 0;
    
    color |= ((uint32_t)r << vinfo.red_mask_shift);
    color |= ((uint32_t)g << vinfo.green_mask_shift);
    color |= ((uint32_t)b << vinfo.blue_mask_shift);
    
    return color;
}

void put_pixel(uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b) {
    if (x >= vinfo.width || y >= vinfo.height) return;
    
    uint64_t offset = (y * vinfo.pitch) + (x * (vinfo.bpp / 8));
    uint8_t* pixel_addr = (uint8_t*)framebuffer + offset;
    
    uint32_t final_color = make_color(r, g, b);

    if (vinfo.bpp == 32) {
        *(uint32_t*)pixel_addr = final_color;
    } else if (vinfo.bpp == 24) {
        pixel_addr[0] = (final_color) & 0xFF;
        pixel_addr[1] = (final_color >> 8) & 0xFF;
        pixel_addr[2] = (final_color >> 16) & 0xFF;
    }
}

void clear_screen(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t final_color = make_color(r, g, b);
    
    if (vinfo.bpp == 32) {
        for (uint32_t y = 0; y < vinfo.height; y++) {
            uint32_t* row = (uint32_t*)((uint8_t*)framebuffer + (y * vinfo.pitch));
            for (uint32_t x = 0; x < vinfo.width; x++) {
                row[x] = final_color;
            }
        }
    } else {
        for (uint32_t y = 0; y < vinfo.height; y++) {
            for (uint32_t x = 0; x < vinfo.width; x++) {
                put_pixel(x, y, r, g, b);
            }
        }
    }
}

void handle_message(message_t* in) {
	
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

    clear_screen(40, 40, 40); 

    for (int y = vinfo.height/2 - 100; y < vinfo.height/2 + 100; y++) {
        for (int x = vinfo.width/2 - 100; x < vinfo.width/2 + 100; x++) {
            put_pixel(x, y, 255, 0, 0);
        }
    }

    for (int y = vinfo.height/2 - 50; y < vinfo.height/2 + 50; y++) {
        for (int x = vinfo.width/2 + 150; x < vinfo.width/2 + 250; x++) {
            put_pixel(x, y, 0, 0, 255);
        }
    }

    message_t msg;
    while(1) {
        ipc_recv_ex(0, MSG_TYPE_NONE, MSG_SUBTYPE_NONE, &msg);
        
        handle_message(&msg);
    }

    return 0;
}