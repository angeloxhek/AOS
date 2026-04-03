#include "include/kernel_internal.h"

// -------------------------
//       Shared Memory
// -------------------------

void shm_init() {
    shm_global_list = 0;
    next_shm_id = 1;
}

shm_object_t* shm_find_by_id(uint64_t id) {
    if (id == 0) return 0;

    shm_object_t* current = shm_global_list;
    while (current != 0) {
        if (current->id == id) {
            return current;
        }
        current = current->next;
    }
    return 0;
}

void shm_add(shm_object_t* obj) {
    obj->next = shm_global_list;
    shm_global_list = obj;
}

void shm_remove(shm_object_t* obj) {
    if (shm_global_list == obj) {
        shm_global_list = obj->next;
        return;
    }

    shm_object_t* current = shm_global_list;
    while (current != 0 && current->next != 0) {
        if (current->next == obj) {
            current->next = obj->next;
            return;
        }
        current = current->next;
    }
}

uint64_t shm_alloc(uint64_t size_bytes, uint64_t* out_vaddr) {
    if (size_bytes == 0 || !out_vaddr) return 0;

    uint64_t page_count = (size_bytes + 4095) / 4096;

    shm_object_t* obj = (shm_object_t*)kernel_malloc(sizeof(shm_object_t));
    if (!obj) return 0;
    kernel_memset(obj, 0, sizeof(shm_object_t));

    obj->id = next_shm_id++;
    obj->owner_pid = current_thread->owner->id;
    obj->page_count = page_count;

    obj->phys_pages = (uint64_t*)kernel_malloc(page_count * sizeof(uint64_t));
    if (!obj->phys_pages) { kernel_free(obj); return 0; }

    uint64_t alloced_pages = 0;
    for (uint64_t i = 0; i < page_count; i++) {
        uint64_t phys = pmm_alloc_block();
        if (!phys) goto oom_cleanup;

        uint64_t* temp = (uint64_t*)temp_map(phys);
        kernel_memset(temp, 0, 4096);
        temp_unmap(temp);

        obj->phys_pages[i] = phys;
        alloced_pages++;
    }

    uint64_t my_vaddr = current_thread->owner->next_shm_vaddr;
    current_thread->owner->next_shm_vaddr += (page_count * 4096);
    obj->owner_vaddr = my_vaddr;

    int flags = PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    for (uint64_t i = 0; i < page_count; i++) {
        map_to_other_pml4(current_thread->owner->page_directory, obj->phys_pages[i], my_vaddr + (i * 4096), flags);
    }

    uint64_t current_cr3;
    asm volatile("mov %%cr3, %0" : "=r"(current_cr3));
    asm volatile("mov %0, %%cr3" :: "r"(current_cr3));

    *out_vaddr = my_vaddr;
    shm_add(obj);
    return obj->id;

oom_cleanup:
    for (uint64_t i = 0; i < alloced_pages; i++) { pmm_free_block(obj->phys_pages[i]); }
    kernel_free(obj->phys_pages);
    kernel_free(obj);
    return 0;
}

int shm_allow(uint64_t shm_id, uint64_t target_tid) {
    shm_object_t* obj = shm_find_by_id(shm_id);
    if (!obj || obj->owner_pid != current_thread->owner->id) return SYS_RES_NO_PERM;

    shm_allow_node_t* node = (shm_allow_node_t*)kernel_malloc(sizeof(shm_allow_node_t));
    if (!node) return SYS_RES_KERNEL_ERR;

    node->tid = target_tid;
    node->next = obj->allow_list;
    obj->allow_list = node;

    return SYS_RES_OK;
}

uint64_t shm_map(uint64_t shm_id) {
    shm_object_t* obj = shm_find_by_id(shm_id);
    if (!obj) return 0;

    int has_access = (obj->owner_pid == current_thread->owner->id);
    shm_allow_node_t* an = obj->allow_list;
    while (an && !has_access) {
        if (an->tid == current_thread->tid) {
            has_access = 1; break;
        }
        an = an->next;
    }
    if (!has_access) return 0;

    uint64_t target_vaddr = current_thread->owner->next_shm_vaddr;
    current_thread->owner->next_shm_vaddr += (obj->page_count * 4096);

    int flags = PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    for (uint64_t i = 0; i < obj->page_count; i++) {
        map_to_other_pml4(current_thread->owner->page_directory, obj->phys_pages[i], target_vaddr + (i * 4096), flags);
    }

    uint64_t current_cr3;
    asm volatile("mov %%cr3, %0" : "=r"(current_cr3));
    asm volatile("mov %0, %%cr3" :: "r"(current_cr3));

    shm_map_node_t* mn = (shm_map_node_t*)kernel_malloc(sizeof(shm_map_node_t));
    if (!mn) return 0;

    mn->pid = current_thread->owner->id;
    mn->vaddr = target_vaddr;
    mn->next = obj->map_list;
    obj->map_list = mn;

    return target_vaddr;
}

int shm_free(uint64_t shm_id) {
    shm_object_t* obj = shm_find_by_id(shm_id);
    if (!obj) return -1;
    if (obj->owner_pid != current_thread->owner->id) return -2;

    shm_map_node_t* mn = obj->map_list;
    while (mn != NULL) {
        process_t* target_proc = get_process_by_id(mn->pid);

        if (target_proc && target_proc->page_directory) {
            for (uint64_t i = 0; i < obj->page_count; i++) {
                map_to_other_pml4(target_proc->page_directory, 0, mn->vaddr + (i * 4096), 0);
            }
        }

        shm_map_node_t* to_free = mn;
        mn = mn->next;
        kernel_free(to_free);
    }

    for (uint64_t i = 0; i < obj->page_count; i++) {
        map_to_other_pml4(current_thread->owner->page_directory, 0, obj->owner_vaddr + (i * 4096), 0);
    }

    uint64_t current_cr3;
    asm volatile("mov %%cr3, %0" : "=r"(current_cr3));
    asm volatile("mov %0, %%cr3" :: "r"(current_cr3));

    for (uint64_t i = 0; i < obj->page_count; i++) {
        pmm_free_block(obj->phys_pages[i]);
    }

    shm_allow_node_t* an = obj->allow_list;
    while (an != NULL) {
        shm_allow_node_t* to_free = an;
        an = an->next;
        kernel_free(to_free);
    }

    kernel_free(obj->phys_pages);
    shm_remove(obj);
    kernel_free(obj);

    return 0;
}
