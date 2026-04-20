#include "include/kernel_internal.h"

// -------------------------
//           ELF
// -------------------------

void load_elf_raw_fat32(volume_t* v, fat32_dirent_t* file, elf_load_result_t* result) {
    uint64_t file_size = 0;
    uint8_t* raw_data = (uint8_t*)fat32_read_file(v, file, &file_size);
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

    process_t* proc = create_process(file->name);
    result->proc = proc;

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
            // Reject segments that map into kernel address space
            if (vaddr >= 0x800000000000ULL) {
                kernel_free(raw_data);
                result->result = ELF_RESULT_INVALID;
                return;
            }
            uint64_t memsz  = phdr[i].p_memsz;
            uint64_t filesz = phdr[i].p_filesz;
            uint64_t offset = phdr[i].p_offset;
            uint64_t start_page = vaddr & PAGE_MASK;
            uint64_t end_page   = (vaddr + memsz + 4095) & PAGE_MASK;
            uint64_t page_count = (end_page - start_page) / 4096;

            if (vaddr + memsz > max_vaddr) {
                max_vaddr = vaddr + memsz;
            }

            for (uint64_t p = 0; p < page_count; p++) {
                uint64_t curr_virt = start_page + (p * 4096);
                uint64_t phys;
                uint64_t existing_phys = vmm_get_phys_from_pml4(proc->page_directory, curr_virt);
                if (existing_phys != 0) {
                    phys = existing_phys;
                } else {
                    phys = pmm_alloc_block();
                    map_to_other_pml4(proc->page_directory, phys, curr_virt,
                                      PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
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

    kernel_free(raw_data);
    result->result = ELF_RESULT_OK;
}

void start_elf_process(elf_load_result_t* res) {
    uint64_t user_stack_virt = 0x0000700000000000;
    uint64_t stack_pages = 8;

    for (uint64_t i = 0; i < stack_pages; i++) {
        uint64_t phys_page = pmm_alloc_block();
        map_to_other_pml4(res->proc->page_directory,
                          phys_page,
                          user_stack_virt - (i * PAGE_SIZE),
                          PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
        kernel_memset((void*)P2V(phys_page), 0, PAGE_SIZE);
    }
    uint64_t user_rsp = user_stack_virt + PAGE_SIZE;
    user_rsp = (user_rsp & ~0xFULL) - 8;
    create_user_thread(res->entry_point, user_rsp, (uint64_t)res->proc->page_directory, res->proc, 0, 0);
}
