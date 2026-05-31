#include <kernel/internal.h>

// -------------------------
//           ELF
// -------------------------

void load_elf_raw_proc(process_t* proc, uint8_t* raw_data, uint64_t file_size, elf_load_result_t* result) {
    if (!raw_data) {
        result->result = ELF_RESULT_INVALID;
        return;
    }

    Elf64_Ehdr* hdr = (Elf64_Ehdr*)raw_data;

    if (hdr->e_ident[0] != 0x7F || hdr->e_ident[1] != 'E' ||
        hdr->e_ident[2] != 'L' || hdr->e_ident[3] != 'F' ||
        hdr->e_ident[4] != 2) { // 2 = ELFCLASS64

        kernel_free(raw_data);
        result->result = ELF_RESULT_INVALID;
        return;
    }

    result->entry_point = hdr->e_entry;
    result->proc = proc;
	result->is_driver = 0; 

    if (hdr->e_phoff >= file_size || hdr->e_phoff + (uint64_t)hdr->e_phnum * sizeof(Elf64_Phdr) > file_size) {
        kernel_free(raw_data);
        result->result = ELF_RESULT_INVALID;
        return;
    }
    Elf64_Phdr* phdr = (Elf64_Phdr*)(raw_data + hdr->e_phoff);
    uint64_t max_vaddr = 0;

    for (int i = 0; i < hdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD) {
            uint64_t vaddr  = phdr[i].p_vaddr;
            if (!hal_is_valid_user_pointer((void*)vaddr)) { // Изменено на HAL
                kernel_free(raw_data);
                result->result = ELF_RESULT_INVALID;
                return;
            }
            uint64_t memsz  = phdr[i].p_memsz;
            uint64_t filesz = phdr[i].p_filesz;
            uint64_t offset = phdr[i].p_offset;
            uint64_t start_page = vaddr & ~4095ULL;
            uint64_t end_page   = (vaddr + memsz + 4095) & ~4095ULL;
            uint64_t page_count = (end_page - start_page) / 4096;

            if (vaddr + memsz > max_vaddr) {
                max_vaddr = vaddr + memsz;
            }

            for (uint64_t p = 0; p < page_count; p++) {
                uint64_t curr_virt = start_page + (p * 4096);
                uint64_t phys;
                uint64_t existing_phys = hal_get_phys((uint64_t)proc->page_directory, curr_virt);
                if (existing_phys != 0) {
                    phys = existing_phys;
                } else {
                    phys = pmm_alloc_block();
                    // 0x7 = USER | WRITE | PRESENT
                    hal_map_page_in_space((uint64_t)proc->page_directory, curr_virt, phys, 0x7);
                }
                void* ptr = temp_map(phys);
                kernel_memset(ptr, 0, 4096);

                uint64_t file_data_start = vaddr;
                uint64_t file_data_end   = vaddr + filesz;
                uint64_t page_start = curr_virt;
                uint64_t page_end   = curr_virt + 4096;
                uint64_t copy_start = (file_data_start > page_start) ? file_data_start : page_start;
                uint64_t copy_end   = (file_data_end < page_end)     ? file_data_end   : page_end;

                if (copy_start < copy_end) {
                    uint64_t bytes_to_copy  = copy_end - copy_start;
                    uint64_t offset_in_page = copy_start - page_start;
                    uint64_t offset_in_file = copy_start - vaddr;

                    kernel_memcpy(
                        (uint8_t*)ptr + offset_in_page,
                        raw_data + offset + offset_in_file,
                        bytes_to_copy
                    );
                }

                temp_unmap(ptr);
            }
        }
        if (phdr[i].p_type == PT_TLS) {
            proc->tls_image_vaddr = phdr[i].p_vaddr;
            proc->tls_file_size   = phdr[i].p_filesz;
            proc->tls_mem_size    = phdr[i].p_memsz;
            proc->tls_align       = phdr[i].p_align;
        }
    }

    if (max_vaddr > 0) {
        proc->heap_limit = (max_vaddr + 4095) & ~((uint64_t)4095);
    } else {
        proc->heap_limit = 0x40000000;
    }

    if (hdr->e_shoff >= file_size || hdr->e_shoff + (uint64_t)hdr->e_shnum * sizeof(Elf64_Shdr) > file_size) {
        kernel_free(raw_data);
        result->result = ELF_RESULT_OK;
        return;
    }
    Elf64_Shdr* shdr = (Elf64_Shdr*)(raw_data + hdr->e_shoff);
	
	if (hdr->e_shstrndx != 0 && hdr->e_shstrndx < hdr->e_shnum) {
        Elf64_Shdr* sh_strtab_hdr = &shdr[hdr->e_shstrndx];
        const char* sh_strtab = (const char*)(raw_data + sh_strtab_hdr->sh_offset);

        for (int i = 0; i < hdr->e_shnum; i++) {
            const char* section_name = sh_strtab + shdr[i].sh_name;
            
            if (kernel_strcmp(section_name, ".driver_info") == 0) {
                aos_driver_info_t* info = (aos_driver_info_t*)(raw_data + shdr[i].sh_offset);
                
                if (info->magic == AOS_DRIVER_MAGIC) {
                    result->is_driver = 1;
					result->driver_info = (aos_driver_info_t*)kernel_malloc(sizeof(aos_driver_info_t));
                    kernel_memcpy(result->driver_info, info, sizeof(aos_driver_info_t));
                }
                break;
            }
        }
    }
	
    char* strtab = 0;
    Elf64_Sym* symtab = 0;
    uint64_t sym_count = 0;

    for (int i = 0; i < hdr->e_shnum; i++) {
        if (shdr[i].sh_type == SHT_STRTAB && i != hdr->e_shstrndx) {
             strtab = (char*)(raw_data + shdr[i].sh_offset);
        }
    }

    for (int i = 0; i < hdr->e_shnum; i++) {
        if (shdr[i].sh_type == SHT_SYMTAB) {
            symtab = (Elf64_Sym*)(raw_data + shdr[i].sh_offset);
            sym_count = shdr[i].sh_size / sizeof(Elf64_Sym);

            if (shdr[i].sh_link != 0) {
                strtab = (char*)(raw_data + shdr[shdr[i].sh_link].sh_offset);
            }
            break;
        }
    }

    if (symtab && strtab) {
        for (uint64_t i = 0; i < sym_count; i++) {
            uint8_t bind = symtab[i].st_info >> 4;
            if (bind == 1 && symtab[i].st_name != 0 && symtab[i].st_value != 0) {
                // register_symbol((char*)(strtab + symtab[i].st_name), symtab[i].st_value);
            }
        }
    }

    result->result = ELF_RESULT_OK;
}

void load_elf_raw(char* name, uint8_t* raw_data, uint64_t file_size, elf_load_result_t* result) {
    process_t* proc = create_process(name);
    load_elf_raw_proc(proc, raw_data, file_size, result);
}

int startup_info_args_copy(startup_info_t* k_info, startup_info_t* child_info, void* kvirt, uint64_t child_virt_addr) {
    child_info->type = k_info->type;
    if (k_info->type == STARTUP_MAIN) {
        child_info->data.main.argc = k_info->data.main.argc;
        child_info->data.main.envc = k_info->data.main.envc;

        uint64_t offset = sizeof(startup_info_t);

        if (k_info->data.main.argc > 0) {
            char** child_argv_base = (char**)((uint64_t)kvirt + offset);
            child_info->data.main.argv = (char**)(child_virt_addr + offset);
            offset += (k_info->data.main.argc + 1) * sizeof(char*);

            for (int i = 0; i < k_info->data.main.argc; i++) {
                char* dst = (char*)((uint64_t)kvirt + offset);
                int len = copy_string_from_user(k_info->data.main.argv[i], dst, 4096 - offset);
                
                if (len == 0) {
                    return -1;
                }
                
                child_argv_base[i] = (char*)(child_virt_addr + offset);
                offset += len;
            }
            child_argv_base[k_info->data.main.argc] = NULL;
        }

        if (k_info->data.main.envc > 0) {
            char** child_envp_base = (char**)((uint64_t)kvirt + offset);
            child_info->data.main.envp = (char**)(child_virt_addr + offset);
            offset += (k_info->data.main.envc + 1) * sizeof(char*);

            for (int i = 0; i < k_info->data.main.envc; i++) {
                char* dst = (char*)((uint64_t)kvirt + offset);
                int len = copy_string_from_user(k_info->data.main.envp[i], dst, 4096 - offset);
                
                if (len == 0) {
                    return -1;
                }
                
                child_envp_base[i] = (char*)(child_virt_addr + offset);
                offset += len;
            }
            child_envp_base[k_info->data.main.envc] = NULL;
        }
    } else {
        child_info->data.driver.reserved1 = k_info->data.driver.reserved1;
        child_info->data.driver.reserved2 = k_info->data.driver.reserved2;
    }
    return 0;
}

startup_info_t* prepare_child_startup_info(process_t* proc, startup_info_t* user_info_ptr) {
    if (!hal_is_valid_user_pointer(user_info_ptr)) return 0;

    startup_info_t* k_info = (startup_info_t*)kernel_malloc(sizeof(startup_info_t));
    if (!k_info) return 0;
    kernel_memcpy(k_info, (void*)user_info_ptr, sizeof(startup_info_t));

    uint64_t child_virt_addr = 0x00007FFFF0000000;
    uint64_t phys = pmm_alloc_block();
    if (!phys) { kernel_free(k_info); return 0; }

    hal_map_page_in_space((uint64_t)proc->page_directory, child_virt_addr, phys, 0x7);
    
    void* kvirt = temp_map(phys);
    kernel_memset(kvirt, 0, 4096);

    startup_info_t* child_info = (startup_info_t*)kvirt;
    
    if (startup_info_args_copy(k_info, child_info, kvirt, child_virt_addr)) {
        pmm_free_block(phys);
        child_virt_addr = 0;
    }

    temp_unmap(kvirt);
    kernel_free(k_info);
    return (startup_info_t*)child_virt_addr;
}

startup_info_t* prepare_child_startup_info_kernel(process_t* proc, startup_info_t* k_info) {
    uint64_t child_virt_addr = 0x00007FFFF0000000;
    uint64_t phys = pmm_alloc_block();
    if (!phys) return 0;

    hal_map_page_in_space((uint64_t)proc->page_directory, child_virt_addr, phys, 0x7);
    
    void* kvirt = temp_map(phys);
    kernel_memset(kvirt, 0, 4096);

    startup_info_t* child_info = (startup_info_t*)kvirt;
    
    if (startup_info_args_copy(k_info, child_info, kvirt, child_virt_addr)) {
        pmm_free_block(phys);
        child_virt_addr = 0;
    }
    
    temp_unmap(kvirt);
    return (startup_info_t*)child_virt_addr;
}

int start_elf_process(elf_load_result_t* res, startup_info_t* info, uint64_t arg2) {
    uint64_t user_stack_virt = 0x0000700000000000;
    uint64_t stack_pages = 8;

    for (uint64_t i = 0; i < stack_pages; i++) {
        uint64_t phys_page = pmm_alloc_block();
        if (phys_page == 0) return -1;
        hal_map_page_in_space((uint64_t)res->proc->page_directory, 
                              user_stack_virt - (i * 4096), phys_page, 0x7);
        void* ptr = temp_map(phys_page);
        kernel_memset(ptr, 0, 4096);
        temp_unmap(ptr);
    }
    uint64_t user_rsp = user_stack_virt + 4096;
    user_rsp = (user_rsp & ~0xFULL) - 8;
    
    startup_info_t* info_addr = 0;
    if (info != NULL) {
        if (hal_is_valid_user_pointer(info)) {
            info_addr = prepare_child_startup_info(res->proc, info);
        } else {
            info_addr = prepare_child_startup_info_kernel(res->proc, info);
        }
    }
    
    thread_t* thread = create_user_thread(res->entry_point, user_rsp, (uint64_t)res->proc->page_directory, res->proc, (uint64_t)info_addr, arg2);
	
	if (res->is_driver) {
		uint16_t temp_ports[ALLOWED_PORTS_MAX];
        for (int i = 0; i < ALLOWED_PORTS_MAX; i++) {
            temp_ports[i] = res->driver_info->allowed_ports[i]; 
        }
		const char* drv_name = res->driver_info->name;
        if (drv_name[0] == '\0') {
            drv_name = 0;
        }
        register_driver(
            res->driver_info->type, 
            drv_name,
            res->driver_info->requested_perms, 
            temp_ports,
			thread->owner
        );
    }
    return 0;
}