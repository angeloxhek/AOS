#include <stdint.h>
#define AOSLIB_SYSCALLS
#define AOSLIB_STRING
#include "../include/aoslib.h"

int64_t syscall(uint64_t nr, uint64_t arg1, uint64_t arg2, uint64_t arg3, 
                uint64_t arg4, uint64_t arg5) {
    int64_t ret;

    register uint64_t r10 asm("r10") = arg4;
    register uint64_t r8  asm("r8")  = arg5;

    asm volatile (
        "syscall"
        : "=a" (ret)          // Выход: RAX -> ret
        : "a" (nr),           // Вход: RAX = Номер сисколла
          "D" (arg1),         // Вход: RDI = Аргумент 1
          "S" (arg2),         // Вход: RSI = Аргумент 2
          "d" (arg3),         // Вход: RDX = Аргумент 3
          "r" (r10),          // Вход: R10 = Аргумент 4 (связано через register variable)
          "r" (r8)            // Вход: R8  = Аргумент 5
        : "rcx", "r11", "memory" // Clobbers: syscall портит RCX и R11
    );

    return ret;
}

void sysprint(const char* str) {
    syscall(SYS_PRINT, (uint64_t)str, 0, 0, 0, 0);
}

int64_t __ipc_recv(message_t* out_msg) {
    return syscall(SYS_IPC_RECV, (uint64_t)out_msg, 0, 0, 0, 0);
}

int64_t ipc_send(uint64_t dest_tid, message_t* msg) {
    return (int)syscall(SYS_IPC_SEND, dest_tid, (uint64_t)msg, 0, 0, 0);
}

int64_t register_driver(driver_type_t type, const char* name) {
    return (int64_t)syscall(SYS_REGISTER_DRIVER, (uint64_t)type, (uint64_t)name, 0, 0, 0);
}

uint64_t get_driver_tid(driver_type_t type) {
    return (uint64_t)syscall(SYS_GET_DRIVER_TID, (uint64_t)type, 0, 0, 0, 0);
}

uint64_t get_driver_tid_name(const char* name) {
    return (uint64_t)syscall(SYS_GET_DRIVER_TID_BY_NAME, (uint64_t)name, 0, 0, 0, 0);
}

int get_sysinfo(system_info_t* info) {
    return (int)syscall(SYS_GET_SYSTEM_INFO, (uint64_t)info, 0, 0, 0, 0);
}

void* syscall_sbrk(int64_t increment) {
    return (void*)syscall(SYS_SBRK, increment, 0, 0, 0, 0);
}

#define MAX_PENDING 64

static message_t pending_messages[MAX_PENDING];
static int pending_count = 0;
static int head_idx = 0;
static int tail_idx = 0;

uint64_t get_ipc_count(void) {
    uint64_t kernel_msgs = AOS_GET_TCB()->pending_msgs;
    return (uint64_t)pending_count + kernel_msgs;
}

void ipc_recv(message_t* out_msg) {
    if (pending_count > 0) {
        *out_msg = pending_messages[head_idx];
        head_idx = (head_idx + 1) % MAX_PENDING;
        pending_count--;
        return;
    }

    __ipc_recv(out_msg);
}

void ipc_recv_ex(uint64_t tid, msg_type_t type, msg_subtype_t subtype, message_t* out_msg) {
    int curr_idx = head_idx;
    for (int i = 0; i < pending_count; i++) {
        
        if ((tid == 0 || pending_messages[curr_idx].sender_tid == tid) &&
            (type == MSG_TYPE_NONE || pending_messages[curr_idx].type == type) &&
            (subtype == MSG_SUBTYPE_NONE || pending_messages[curr_idx].subtype == subtype)) {
            
            *out_msg = pending_messages[curr_idx];
            
            int shift_idx = curr_idx;
            while (1) {
                int next_idx = (shift_idx + 1) % MAX_PENDING;
                if (next_idx == tail_idx) break; // Дошли до конца данных
                pending_messages[shift_idx] = pending_messages[next_idx];
                shift_idx = next_idx;
            }
            
            tail_idx = (tail_idx - 1 + MAX_PENDING) % MAX_PENDING;
            pending_count--;
            return;
        }
        curr_idx = (curr_idx + 1) % MAX_PENDING;
    }

    message_t temp_msg;
    while (1) {
        int64_t res = __ipc_recv(&temp_msg);
		
        if ((tid == 0 || temp_msg.sender_tid == tid) && 
            (type == MSG_TYPE_NONE || temp_msg.type == type) && 
            (subtype == MSG_SUBTYPE_NONE || temp_msg.subtype == subtype)) {
            
            *out_msg = temp_msg;
            return;
        } else {
            if (pending_count < MAX_PENDING) {
                pending_messages[tail_idx] = temp_msg;
                tail_idx = (tail_idx + 1) % MAX_PENDING;
                pending_count++;
            } else {
                sysprint("[!] Warning: IPC pending buffer overflow, dropping msg\n");
            }
        }
    }
}

uint64_t __kbd_driver_tid_cache = 0;

uint8_t get_scancode() {
    if (__kbd_driver_tid_cache == 0) {
        __kbd_driver_tid_cache = get_driver_tid(DT_KEYBOARD);
        if (__kbd_driver_tid_cache == 0) return 0;
    }
    message_t msg;
    msg.type = MSG_TYPE_KEYBOARD;
    msg.subtype = MSG_SUBTYPE_QUERY;
    msg.param1 = 0;
    int res = ipc_send(__kbd_driver_tid_cache, &msg);
    if (res < 0) {
        __kbd_driver_tid_cache = 0;
        return 0;
    }
    message_t response;
    ipc_recv_ex(__kbd_driver_tid_cache, MSG_TYPE_KEYBOARD, MSG_SUBTYPE_RESPONSE, &response);
    return (uint8_t)(response.param1 & 0xFF);
}

uint64_t get_disk_count(void) {
	return syscall(SYS_GET_DISK_COUNT, 0, 0, 0, 0, 0);
}

uint64_t get_partition_count(void) {
	return syscall(SYS_GET_PARTITION_COUNT, 0, 0, 0, 0, 0);
}

uint64_t get_disk_info(uint64_t index, disk_info_t* pinfo) {
	return syscall(SYS_GET_DISK_INFO, index, (uint64_t)pinfo, 0, 0, 0);
}

uint64_t get_partition_info(uint64_t index, partition_info_t* pinfo) {
	return syscall(SYS_GET_PARTITION_INFO, index, (uint64_t)pinfo, 0, 0, 0);
}

typedef struct malloc_header {
    uint64_t size;
    uint64_t is_free;
    struct malloc_header* next;
    uint64_t padding; // Выравнивание 16 байт
} __attribute__((aligned(16))) malloc_header_t;

static malloc_header_t* free_list_start = 0;
static int malloc_initialized = 0;
#define ALIGN_PAGE(x) (((x) + 4095) & ~4095)

void* malloc(uint64_t size) {
    if (size == 0) return (void*)0;

    size = (size + 15) & ~15;
    
    if (!malloc_initialized) {
        uint64_t initial_size = size + sizeof(malloc_header_t);
        initial_size = ALIGN_PAGE(initial_size);
        
        void* ptr = syscall_sbrk(initial_size);
        if ((uint64_t)ptr <= 0) return (void*)0;
        
        free_list_start = (malloc_header_t*)ptr;
        free_list_start->size = initial_size - sizeof(malloc_header_t);
        free_list_start->is_free = 1;
        free_list_start->next = (void*)0;
        malloc_initialized = 1;
    }

restart_search:
    
    malloc_header_t* current = free_list_start;
    malloc_header_t* last = (void*)0;
    
    while (current) {
        if (current->is_free && current->size >= size) {
            /*if (current->size > size + sizeof(malloc_header_t) + 16) {
                malloc_header_t* next_block = (malloc_header_t*)((uint8_t*)current + sizeof(malloc_header_t) + size);
                next_block->size = current->size - size - sizeof(malloc_header_t);
                next_block->is_free = 1;
                next_block->next = current->next;
                
                current->size = size;
                current->next = next_block;
            }*/
            current->is_free = 0;
            return (void*)((uint8_t*)current + sizeof(malloc_header_t));
        }
        last = current;
        current = current->next;
    }
    
    uint64_t total_needed = size + sizeof(malloc_header_t);
    total_needed = ALIGN_PAGE(total_needed);
    
    malloc_header_t* new_block = (malloc_header_t*)syscall_sbrk(total_needed);
    if ((uint64_t)new_block <= 0) return (void*)0;
    
    new_block->size = total_needed - sizeof(malloc_header_t);
    new_block->is_free = 1;
    new_block->next = (void*)0;
    
    if (last) {
        last->next = new_block;
    } else {
        free_list_start = new_block;
    }

    goto restart_search;
}

void free(void* ptr) {
    if (!ptr) return;
    
    malloc_header_t* header = (malloc_header_t*)((uint8_t*)ptr - sizeof(malloc_header_t));
    header->is_free = 1;
    
    malloc_header_t* current = free_list_start;
    while (current && current->next) {
        if (current->is_free && current->next->is_free) {
            if ((uint8_t*)current + sizeof(malloc_header_t) + current->size == (uint8_t*)current->next) {
                current->size += current->next->size + sizeof(malloc_header_t);
                current->next = current->next->next;
                continue; 
            }
        }
        current = current->next;
    }
}

void* realloc(void* ptr, uint64_t new_size) {
    if (!ptr) return malloc(new_size);
    if (new_size == 0) {
        free(ptr);
        return 0;
    }
    
    malloc_header_t* header = (malloc_header_t*)((uint8_t*)ptr - sizeof(malloc_header_t));
    if (header->size >= new_size) return ptr;
    
    void* new_ptr = malloc(new_size);
    if (new_ptr) {
        // memcpy нужно реализовать или подключить string.h
        uint8_t* d = (uint8_t*)new_ptr;
        uint8_t* s = (uint8_t*)ptr;
        for(uint64_t i=0; i<header->size; i++) d[i] = s[i];
        
        free(ptr);
    }
    return new_ptr;
}

void* calloc(uint64_t num, uint64_t size) {
    if (num == 0 || size == 0) {
        return (void*)0;
    }
    uint64_t max_val = (uint64_t)-1;
    if (size > max_val / num) {
        return (void*)0;
    }
    uint64_t total_size = num * size;
    void* ptr = malloc(total_size);
    if (ptr != (void*)0) {
        memset(ptr, 0, total_size);
    }
    return ptr;
}

uint64_t shm_alloc(uint64_t size_bytes, void** out_vaddr) {
    return syscall(SYS_SHM_ALLOC, size_bytes, (uint64_t)out_vaddr, 0, 0, 0);
}

int shm_allow(uint64_t shm_id, uint64_t target_tid) {
    return (int)syscall(SYS_SHM_ALLOW, shm_id, target_tid, 0, 0, 0);
}

void* shm_map(uint64_t shm_id) {
    return (void*)syscall(SYS_SHM_MAP, shm_id, 0, 0, 0, 0);
}

int shm_free(uint64_t shm_id) {
    return (int)syscall(SYS_SHM_FREE, shm_id, 0, 0, 0, 0);
}