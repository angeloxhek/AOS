#ifndef AOS_SYSCALLS_H
#define AOS_SYSCALLS_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SYS_EXIT                      1
#define SYS_IPC_SEND                  2
#define SYS_IPC_TRYRECV               3
#define SYS_IPC_RECV                  4
#define SYS_SLEEP                     5
#define SYS_GET_DRIVER_PID            6
#define SYS_GET_DRIVER_PID_BY_NAME    7
#define SYS_GET_SYSTEM_INFO           8
#define SYS_SBRK                      9
#define SYS_EDIT_SYSTEM_FLAGS        10 // Driver only
#define SYS_MAP_PHYS                 11 // Driver only
#define SYS_GET_SPEC_INFO            12 // Driver only
#define SYS_RESERVED5                13
#define SYS_RESERVED6                14
#define SYS_RESERVED7                15
#define SYS_YIELD                    16
#define SYS_PRINT                    17
#define SYS_SHM_ALLOC                18
#define SYS_SHM_ALLOW                19
#define SYS_SHM_MAP                  20
#define SYS_SHM_FREE                 21
#define SYS_GET_PID_LIST             22
#define SYS_GET_PROC_INFO            23
#define SYS_GET_TID_LIST             24
#define SYS_GET_THREAD_INFO          25
#define SYS_GET_TIME_INFO            26
#define SYS_SPAWN                    27
#define SYS_FORK                     28
#define SYS_EXEC                     29

typedef enum {
    MSG_TYPE_NONE = 0, MSG_TYPE_AUTH, MSG_TYPE_VFS,
    MSG_TYPE_VIDEO, MSG_TYPE_HARDWARE, MSG_TYPE_INPUT,
    MSG_TYPE_WND,
	MSG_TYPE_DATA
} msg_type_t;

typedef enum {
    MSG_SUBTYPE_NONE = 0, MSG_SUBTYPE_QUERY, MSG_SUBTYPE_SEND,
    MSG_SUBTYPE_RESPONSE, MSG_SUBTYPE_PING, MSG_SUBTYPE_PONG
} msg_subtype_t;

typedef enum {
    HW_EVT_IRQ = 1 
} hardware_cmd_t;

typedef struct message_t {
    apid_t   sender_pid;
    uint32_t type;
    uint32_t subtype;
    uint64_t param1;
    uint64_t param2;
    uint64_t param3;
    uint8_t  data[64];
} __attribute__((packed, aligned(8))) message_t;

typedef enum { SEEK_SET = 1, SEEK_CUR, SEEK_END } seek_whence_t;

#define AOS_HANDLE_SUBTYPE_CHECK(st) do { \
    _Static_assert(__builtin_types_compatible_p(typeof(*(in)), message_t), "AOS_HANDLE_SUBTYPE_CHECK: 'in' must be message_t*"); \
    _Static_assert(__builtin_types_compatible_p(typeof(*(out)), message_t), "AOS_HANDLE_SUBTYPE_CHECK: 'out' must be message_t*"); \
    if (in->subtype != (st)) { out->param1 = DRV_ERR_NOCOMM; break; } \
} while(0)
	
#ifdef AOSLIB_SYSCALLS

int64_t syscall(uint64_t nr, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5);
void sysprint(const char* str);

int64_t __ipc_recv(message_t* out_msg);
int64_t __ipc_tryrecv(message_t* out_msg);
int64_t ipc_send(apid_t dest_pid, message_t* msg);
uint64_t get_ipc_count(void);
void ipc_recv(message_t* out_msg);
int64_t ipc_tryrecv(message_t* out_msg);
void ipc_recv_ex(apid_t pid, msg_type_t type, msg_subtype_t subtype, message_t* out_msg);
int ipc_tryrecv_ex(apid_t pid, msg_type_t type, msg_subtype_t subtype, message_t* out_msg);
void ipc_seek(int64_t offset, seek_whence_t whence);
int ipc_get_at(uint64_t index, message_t* out);

apid_t get_driver_pid(driver_type_t type);
int get_driver_pid_sleep_wrapper(void* arg);
apid_t get_driver_pid_name(const char* name);
driver_type_t dt_from_str(const char* str);

uint8_t get_scancode();
int get_sysinfo(system_info_t* info);
uint64_t get_system_ticks(void);
void* syscall_sbrk(int64_t increment);

uint64_t shm_alloc(uint64_t size_bytes, void** out_vaddr);
int shm_allow(uint64_t shm_id, apid_t target_pid);
void* shm_map(uint64_t shm_id);
int shm_free(uint64_t shm_id);

int sysedit_sys_flags(uint32_t flags);
int sysmap_phys(uint64_t phys_addr, uint64_t size_bytes, uint64_t* out_vaddr);
int sysget_spec_info(uint64_t info_id, void* out_buffer);

#endif

#ifdef __cplusplus
}
#endif
#endif // AOS_SYSCALLS_H