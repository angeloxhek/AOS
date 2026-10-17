#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <aos/types.h>
#include <aos/driver.h>
#include <aos/ipc.h>
#include <aos/syscalls.h>
#include <aos/vfs.h>
#include <aos/pci.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

AOS_DECLARE_DRIVER(DT_PCI, DRV_PERM_IO_PORTS, PCI_CONFIG_ADDRESS, PCI_CONFIG_DATA);

typedef struct pci_device_info_node {
    pci_device_info_t data;
	
	struct pci_device_info_node* next;
} pci_device_info_node_t;

pci_device_info_node_t* pci_head = NULL;

typedef enum {
    PCI_FILE_ROOT = 1,
    PCI_FILE_DEV_DIR,
    PCI_FILE_VENDOR,
    PCI_FILE_DEVICE,
    PCI_FILE_CLASS,
    PCI_FILE_SUBCLASS,
    PCI_FILE_BAR0 = 10,
	PCI_FILE_BAR1 = 11,
	PCI_FILE_BAR2 = 12,
	PCI_FILE_BAR3 = 13,
	PCI_FILE_BAR4 = 14,
	PCI_FILE_BAR5 = 15
} pcifs_file_type_t;

fs_file_handle_t make_fd(pci_device_info_node_t* dev, pcifs_file_type_t type) {
    return (fs_file_handle_t)(((uint64_t)dev & ~0xFFULL) | (type & 0xFF));
}

pci_device_info_node_t* get_dev_from_fd(fs_file_handle_t fd) {
    return (pci_device_info_node_t*)((uint64_t)fd & ~0xFFULL);
}

pcifs_file_type_t get_type_from_fd(fs_file_handle_t fd) {
    return (uint64_t)fd & 0xFF;
}

uint32_t pci_read_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)(
        ((uint32_t)bus << 16)  | 
        ((uint32_t)slot << 11) | 
        ((uint32_t)func << 8)  | 
        (offset & 0xFC)        | 
        ((uint32_t)0x80000000)
    );

    hal_outl(PCI_CONFIG_ADDRESS, address);

    return hal_inl(PCI_CONFIG_DATA);
}

void pci_write_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t address = (uint32_t)(
        ((uint32_t)bus << 16)  | 
        ((uint32_t)slot << 11) | 
        ((uint32_t)func << 8)  | 
        (offset & 0xFC)        | 
        ((uint32_t)0x80000000)
    );

    hal_outl(PCI_CONFIG_ADDRESS, address);

    hal_outl(PCI_CONFIG_DATA, value);
}

void scan_pci() {
    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            uint32_t id = pci_read_config(bus, slot, 0, 0);
            if ((id & 0xFFFF) == 0xFFFF) continue;

            uint32_t header_type = pci_read_config(bus, slot, 0, 0x0C);
            int max_func = ((header_type >> 16) & 0x80) ? 8 : 1;

            for (int func = 0; func < max_func; func++) {
                id = pci_read_config(bus, slot, func, 0);
                if ((id & 0xFFFF) == 0xFFFF) continue;
				
				pci_device_info_node_t* device = malloc(sizeof(pci_device_info_node_t));
				if (!device) continue;
				
				memset(device, 0, sizeof(pci_device_info_node_t));

                device->data.bus = bus;
                device->data.slot = slot;
                device->data.func = func;
                device->data.vendor_id = id & 0xFFFF;
                device->data.device_id = id >> 16;
                
                uint32_t class_info = pci_read_config(bus, slot, func, 0x08);
                device->data.class_code = class_info >> 24;
                device->data.subclass = (class_info >> 16) & 0xFF;

                for (int b = 0; b < 6; b++) {
                    device->data.bar[b] = pci_read_config(bus, slot, func, 0x10 + (b * 4));
                }
				
				device->next = pci_head;
				pci_head = device;
            }
        }
    }
}

fs_instance_t pcifs_mount(block_dev_t* dev, fs_context_t context) { UNUSED(context); return (fs_instance_t)(uint64_t)(dev == 0); }
void pcifs_umount(fs_instance_t fs) { UNUSED(fs); }

fs_file_handle_t pcifs_open(fs_instance_t fs, const char* path, uint32_t flags) {
    UNUSED(fs); UNUSED(flags);
    
    if (path[0] == '\0' || strcmp(path, "/") == 0) {
        return make_fd(NULL, PCI_FILE_ROOT);
    }

    char path_copy[64];
    strlcpy(path_copy, path, 64);
    
    char* p = path_copy;
    if (*p == '/') p++;

    char* slash = strchr(p, '/');
    char* dev_name = p;
    char* file_name = NULL;
    
    if (slash) {
        *slash = '\0';
        file_name = slash + 1;
    }

    pci_device_info_node_t* target_dev = NULL;
    pci_device_info_node_t* curr = pci_head;
    char name_buf[16];
    
    while (curr) {
        snprintf(name_buf, sizeof(name_buf), "%02x:%02x.%x", curr->data.bus, curr->data.slot, curr->data.func);
        if (strcmp(name_buf, dev_name) == 0) {
            target_dev = curr;
            break;
        }
        curr = curr->next;
    }

    if (!target_dev) return 0;

    if (!file_name || file_name[0] == '\0') {
        return make_fd(target_dev, PCI_FILE_DEV_DIR);
    }

    if (strcmp(file_name, "vendor") == 0) return make_fd(target_dev, PCI_FILE_VENDOR);
    if (strcmp(file_name, "device") == 0) return make_fd(target_dev, PCI_FILE_DEVICE);
    if (strcmp(file_name, "class") == 0) return make_fd(target_dev, PCI_FILE_CLASS);
    if (strcmp(file_name, "subclass") == 0) return make_fd(target_dev, PCI_FILE_SUBCLASS);
    
    if (strncmp(file_name, "bar", 3) == 0) {
        int bar_num = file_name[3] - '0';
        if (bar_num >= 0 && bar_num <= 5) {
            return make_fd(target_dev, PCI_FILE_BAR0 + bar_num);
        }
    }

    return 0;
}

int pcifs_read(fs_instance_t fs, fs_file_handle_t fd, void* buf, uint64_t size, uint64_t offset) {
    UNUSED(fs);
    pci_device_info_node_t* dev = get_dev_from_fd(fd);
    int type = get_type_from_fd(fd);

    char text[32] = {0};

    switch (type) {
        case PCI_FILE_VENDOR:   snprintf(text, sizeof(text), "0x%04X\n", dev->data.vendor_id); break;
        case PCI_FILE_DEVICE:   snprintf(text, sizeof(text), "0x%04X\n", dev->data.device_id); break;
        case PCI_FILE_CLASS:    snprintf(text, sizeof(text), "0x%02X\n", dev->data.class_code); break;
        case PCI_FILE_SUBCLASS: snprintf(text, sizeof(text), "0x%02X\n", dev->data.subclass); break;
        default:
            if (type >= PCI_FILE_BAR0 && type <= PCI_FILE_BAR5) {
                snprintf(text, sizeof(text), "0x%08X\n", dev->data.bar[type - PCI_FILE_BAR0]);
            } else {
                return -1;
            }
    }

    uint64_t len = strlen(text);
    if (offset >= len) return 0; // EOF
    
    uint64_t to_copy = size;
    if (offset + to_copy > len) to_copy = len - offset;
    
    memcpy(buf, text + offset, to_copy);
    return to_copy;
}

int pcifs_readdir(fs_instance_t fs, fs_file_handle_t fd, uint64_t* offset, fs_dirent_t* out_array, int max_entries) {
    UNUSED(fs);
    pci_device_info_node_t* dev = get_dev_from_fd(fd);
    int type = get_type_from_fd(fd);
	
	UNUSED(dev);

    if (*offset == (uint64_t)-1) return 0;

    int count = 0;

    if (type == PCI_FILE_ROOT) {
        pci_device_info_node_t* curr = pci_head;
        
        for (uint64_t i = 0; i < *offset && curr; i++) curr = curr->next;

        while (curr && count < max_entries) {
            snprintf(out_array[count].name, sizeof(out_array[count].name), "%02x:%02x.%x",
					 curr->data.bus, curr->data.slot, curr->data.func);
            out_array[count].type = VFS_FILE_TYPE_DIR;
            out_array[count].size = 0;
            
            curr = curr->next;
            count++;
            (*offset)++;
        }
        
        if (!curr) *offset = (uint64_t)-1; // EOF
    } 
    else if (type == PCI_FILE_DEV_DIR) {
        const char* files[] = {"vendor", "device", "class", "subclass", "bar0", "bar1", "bar2", "bar3", "bar4", "bar5"};
        int total_files = 10;

        while (*offset < (uint64_t)total_files && count < max_entries) {
            strlcpy(out_array[count].name, files[*offset], sizeof(out_array[count].name));
            out_array[count].type = VFS_FILE_TYPE_REGULAR;
            out_array[count].size = 0;
            
            count++;
            (*offset)++;
        }
        
        if (*offset >= (uint64_t)total_files) *offset = (uint64_t)-1; // EOF
    }

    return count;
}

int pcifs_stat(fs_instance_t fs, fs_file_handle_t fd, fs_stat_t* out_info) {
    UNUSED(fs);
    int type = get_type_from_fd(fd);
    
    memset(out_info, 0, sizeof(fs_stat_t));
    if (type == PCI_FILE_ROOT || type == PCI_FILE_DEV_DIR) {
        out_info->attributes = 0x10; // DIR
    } else {
        out_info->attributes = 0; // REGULAR
        out_info->size_bytes = 11;
    }
    return 0;
}

void pcifs_close(fs_instance_t fs, fs_file_handle_t file) {
    UNUSED(fs); UNUSED(file);
}

fs_driver_t pci_driver = {
	.name = "pcifs",
    .mount = pcifs_mount,
    .umount = pcifs_umount,
    .open = pcifs_open,
    .read = pcifs_read,
    .readdir = pcifs_readdir,
    .stat = pcifs_stat,
    .close = pcifs_close
};

int vfs_ok = 1;

void handle_vfs(message_t* in) {
    message_t out;
    memset(&out, 0, sizeof(message_t));
    out.type = MSG_TYPE_VFS;
    out.subtype = MSG_SUBTYPE_RESPONSE;
    
    switch (in->param1) {
        case VFS_CMD_MOUNT_FS: {
            uint64_t shm_id = in->param2;
            block_dev_t* dev = shm_map(shm_id);
            fs_instance_t res = pci_driver.mount(dev, 0);
            shm_free(shm_id);
            out.param1 = VFS_ERR_OK;
            out.param2 = (uint64_t)res;
            break;
        }
        case VFS_CMD_UMOUNT_FS: {
            fs_instance_t fs = (fs_instance_t)in->param2;
            pci_driver.umount(fs);
            out.param1 = VFS_ERR_OK;
            break;
        }
        case VFS_CMD_OPEN: {
            uint32_t flags = in->param2;
            fs_instance_t fs = (fs_instance_t)in->param3;
            char* path = (char*)in->data;
            
            fs_file_handle_t fd = pci_driver.open(fs, path, flags);
            if (fd) {
                out.param1 = VFS_ERR_OK;
                out.param2 = (uint64_t)fd;
            } else {
                out.param1 = VFS_ERR_NOTFOUND;
            }
            break;
        }
        case VFS_CMD_READ: {
            fs_file_handle_t fd = (fs_file_handle_t)in->param2;
            uint64_t size = in->param3;
            uint64_t shm_id = *(uint64_t*)(in->data);
            uint64_t offset = *(uint64_t*)(in->data + 8);
            
            void* buf = shm_map(shm_id);
            if (buf) {
                int bytes = pci_driver.read(0, fd, buf, size, offset);
                if (bytes >= 0) {
                    out.param1 = VFS_ERR_OK;
                    out.param2 = bytes;
                } else {
                    out.param1 = VFS_ERR_UNKNOWN;
                }
                shm_free(shm_id);
            } else {
                out.param1 = VFS_ERR_UNKNOWN;
            }
            break;
        }
        case VFS_CMD_LIST: {
            fs_file_handle_t fd = (fs_file_handle_t)in->param2;
            int max_entries = in->param3;
            uint64_t shm_id = *(uint64_t*)(in->data);
            uint64_t offset = *(uint64_t*)(in->data + 8);
            
            fs_dirent_t* buf = (fs_dirent_t*)shm_map(shm_id);
            if (buf) {
                int entries = pci_driver.readdir(0, fd, &offset, buf, max_entries);
                out.param1 = VFS_ERR_OK;
                out.param2 = entries;
                *(uint64_t*)(out.data) = offset;
                shm_free(shm_id);
            } else {
                out.param1 = VFS_ERR_UNKNOWN;
            }
            break;
        }
        case VFS_CMD_STAT: {
            fs_file_handle_t fd = (fs_file_handle_t)in->param2;
            uint64_t shm_id = *(uint64_t*)(in->data);
            
            fs_stat_t* buf = (fs_stat_t*)shm_map(shm_id);
            if (buf) {
                if (pci_driver.stat(0, fd, buf) == 0) {
                    out.param1 = VFS_ERR_OK;
                } else {
                    out.param1 = VFS_ERR_UNKNOWN;
                }
                shm_free(shm_id);
            } else {
                out.param1 = VFS_ERR_UNKNOWN;
            }
            break;
        }
        case VFS_CMD_CLOSE: {
            fs_file_handle_t fd = (fs_file_handle_t)in->param2;
            pci_driver.close(0, fd);
            out.param1 = VFS_ERR_OK;
            break;
        }
        default: {
            out.param1 = VFS_ERR_NOCOMM;
            break;
        }
    }
    
    ipc_reply(in, &out);
}

int driver_main(void* reserved1, void* reserved2) {
    (void)reserved1;
    (void)reserved2;
	
	scan_pci();
	
    printf("PCI Driver: Ready.\n");
	
	fs_context_t ctx = vfs_register_fs(pci_driver.name);
	if (!ctx) {
		vfs_ok = 0;
	}
	
	vfs_mount_fs_async(ctx, "/hw/pci");

    message_t msg;
    while (1) {
        ipc_recv(&msg);
		
		if (!(msg.flags & MSG_FLAG_NEW_ID)) {
			ipc_requeue(&msg);
			continue;
		}
        
		if(msg.type == MSG_TYPE_VFS) {
			handle_vfs(&msg);
		} else if (msg.type == MSG_TYPE_PCI) {
            message_t out;
            memset(&out, 0, sizeof(message_t));
            out.type = MSG_TYPE_PCI;
            out.subtype = MSG_SUBTYPE_RESPONSE;

            switch (msg.param1) {
                case PCI_CMD_FIND_DEVICE: {
                    uint8_t class_code = msg.param2;
                    uint8_t subclass = msg.param3;
                    uint64_t shm_id = *(uint64_t*)(msg.data);

                    pci_device_info_node_t* curr = pci_head;
                    pci_device_info_node_t* found = NULL;

                    while (curr) {
                        if (curr->data.class_code == class_code && curr->data.subclass == subclass) {
                            found = curr;
                            break;
                        }
                        curr = curr->next;
                    }

                    if (found) {
                        void* shm_vaddr = shm_map(shm_id);
                        if (shm_vaddr) {
                            memcpy(shm_vaddr, &found->data, sizeof(pci_device_info_t));
                            out.param1 = PCI_ERR_OK;
                        } else {
                            out.param1 = PCI_ERR_NOCOMM;
                        }
                        shm_free(shm_id);
                    } else {
                        out.param1 = PCI_ERR_NOTFOUND;
                    }
                    break;
                }
                case PCI_CMD_READ_CONFIG: {
                    uint8_t bus = (msg.param2 >> 16) & 0xFF;
                    uint8_t slot = (msg.param2 >> 8) & 0xFF;
                    uint8_t func = msg.param2 & 0xFF;
                    uint8_t offset = msg.param3 & 0xFF;

                    out.param1 = PCI_ERR_OK;
                    out.param2 = pci_read_config(bus, slot, func, offset);
                    break;
                }
                case PCI_CMD_WRITE_CONFIG: {
                    uint8_t bus = (msg.param2 >> 16) & 0xFF;
                    uint8_t slot = (msg.param2 >> 8) & 0xFF;
                    uint8_t func = msg.param2 & 0xFF;
                    uint8_t offset = msg.param3 & 0xFF;
                    uint32_t value = *(uint32_t*)(msg.data);

                    pci_write_config(bus, slot, func, offset, value);
                    out.param1 = PCI_ERR_OK;
                    break;
                }
                default:
                    out.param1 = PCI_ERR_NOCOMM;
                    break;
            }
            ipc_reply(&msg, &out);
        } else {
			message_t out = { .type = MSG_TYPE_PCI, .subtype = MSG_SUBTYPE_RESPONSE, .param1 = PCI_ERR_NOCOMM};
			ipc_reply(&msg, &out);
		}
    }
}