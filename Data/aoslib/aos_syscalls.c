#include <stdint.h>
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
    return syscall(SYS_IPC_SEND, dest_tid, (uint64_t)msg, 0, 0, 0);
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

typedef struct msg_node {
    message_t msg;
    struct msg_node* next;
} msg_node_t;

static msg_node_t *pending_head = NULL;
static msg_node_t *pending_tail = NULL;

static void queue_message(message_t msg) {
    msg_node_t* node = (msg_node_t*)malloc(sizeof(msg_node_t));
    if (!node) {
        sysprint("[!] Critical: IPC buffer malloc failed, message dropped\n");
        return;
    }
    node->msg = msg;
    node->next = NULL;
    
    if (pending_tail) {
        pending_tail->next = node;
        pending_tail = node;
    } else {
        pending_head = pending_tail = node;
    }
}

uint64_t get_ipc_count(void) {
    uint64_t count = 0;
    msg_node_t *curr = pending_head;
    while (curr) {
        count++;
        curr = curr->next;
    }
    return count + AOS_GET_TCB()->pending_msgs;
}

void ipc_recv(message_t* out_msg) {
    if (pending_head) {
        *out_msg = pending_head->msg;
        msg_node_t* temp = pending_head;
        pending_head = pending_head->next;
        if (!pending_head) pending_tail = NULL;
        free(temp);
        return;
    }
    __ipc_recv(out_msg);
}

void ipc_recv_ex(uint64_t tid, msg_type_t type, msg_subtype_t subtype, message_t* out_msg) {
    msg_node_t *curr = pending_head;
    msg_node_t *prev = NULL;

    while (curr) {
        if ((tid == 0 || curr->msg.sender_tid == tid) &&
            (type == MSG_TYPE_NONE || curr->msg.type == type) &&
            (subtype == MSG_SUBTYPE_NONE || curr->msg.subtype == subtype)) {
            
            *out_msg = curr->msg;
            
            if (prev) prev->next = curr->next;
            else pending_head = curr->next;
            
            if (curr == pending_tail) pending_tail = prev;
            
            free(curr);
            return;
        }
        prev = curr;
        curr = curr->next;
    }

    while (1) {
        message_t temp_msg;
        __ipc_recv(&temp_msg);
        
        if ((tid == 0 || temp_msg.sender_tid == tid) && 
            (type == MSG_TYPE_NONE || temp_msg.type == type) && 
            (subtype == MSG_SUBTYPE_NONE || temp_msg.subtype == subtype)) {
            
            *out_msg = temp_msg;
            return;
        } else {
            queue_message(temp_msg);
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

int get_proc_info(uint32_t pid, proc_info_user_t* out_info) {
    return syscall(SYS_GET_PROC_INFO, (uint64_t)pid, (uint64_t)out_info, 0, 0, 0);
}

int get_thread_info(uint32_t pid, thread_info_user_t* out_info) {
    return syscall(SYS_GET_THREAD_INFO, (uint64_t)pid, (uint64_t)out_info, 0, 0, 0);
}

int get_pid_list(uint32_t* buff, uint64_t count) {
    return syscall(SYS_GET_PID_LIST, (uint64_t)buff, count, 0, 0, 0);
}

int get_tid_list(uint32_t pid, uint32_t* buff, uint64_t count) {
    return syscall(SYS_GET_TID_LIST, (uint64_t)pid, (uint64_t)buff, count, 0, 0);
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

void thread_yield(void) {
	syscall(SYS_YIELD, 0, 0, 0, 0, 0);
}

int get_time_info(time_info_t* info) {
	return (int)syscall(SYS_GET_TIME_INFO, (uint64_t)info, 0, 0, 0, 0);
}