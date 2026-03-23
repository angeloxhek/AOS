#ifndef AOSLIB
#define AOSLIB
#include <stdint.h>

#define SYS_EXIT                     1
#define SYS_IPC_SEND                 2
#define SYS_IPC_RECV                 3
#define SYS_REGISTER_DRIVER          4
#define SYS_GET_DRIVER_TID           5
#define SYS_GET_DRIVER_TID_BY_NAME   6
#define SYS_GET_SYSTEM_INFO          7
#define SYS_SBRK                     8
#define SYS_BLOCK_READ               9
#define SYS_BLOCK_WRITE             10 // Реализовать
#define SYS_GET_DISK_COUNT          11
#define SYS_GET_DISK_INFO           12
#define SYS_GET_PARTITION_COUNT     13
#define SYS_GET_PARTITION_INFO      14
#define SYS_RESERVED1               15
#define SYS_PRINT                   16

#define SYS_RES_OK                   0
#define SYS_RES_INVALID             -1
#define SYS_RES_NO_PERM             -2
#define SYS_RES_ALREADY             -3
#define SYS_RES_BUFFER_TOO_SMALL    -4
#define SYS_RES_QUEUE_EMPTY         -5
#define SYS_RES_DSK_ERR             -6
#define SYS_RES_KERNEL_ERR         -99

#define VFS_ERR_OK                   0
#define VFS_ERR_NOFILE              -1
#define VFS_ERR_SYMLINKLOOP         -2
#define VFS_ERR_PERM                -3
#define VFS_ERR_ISDIR               -4
#define VFS_ERR_NOCOMM              -5
#define VFS_ERR_UNKNOWN            -99

#define STAT_OK                      0
#define STAT_STACK_SMASHING       -256
#define STAT_NO_ENTRY             -257

typedef enum {
    VFS_CMD_OPEN  = 1,
    VFS_CMD_CLOSE = 2,
    VFS_CMD_READ  = 3,
    VFS_CMD_WRITE = 4,
    VFS_CMD_STAT  = 5,
    VFS_CMD_LIST  = 6
} vfs_cmd_t;

typedef enum {
    MSG_TYPE_NONE = 0,
    MSG_TYPE_KEYBOARD,
    MSG_TYPE_VFS,
    MSG_TYPE_DATA
} msg_type_t;

typedef enum {
    MSG_SUBTYPE_NONE = 0,
    MSG_SUBTYPE_QUERY,
	MSG_SUBTYPE_SEND,
	MSG_SUBTYPE_RESPONSE
} msg_subtype_t;

typedef struct message_t {
    uint64_t sender_tid;
    uint32_t type;
	uint32_t subtype;
    uint64_t param1;
    uint64_t param2;
    uint64_t param3;
	uint8_t* payload_ptr;
    uint64_t payload_size;
} __attribute__((packed, aligned(8))) message_t;

typedef enum {
	DT_NONE = 0,
	DT_KEYBOARD,
	DT_VFS,
	DT_VIDEO,
	DT_USER = 100
} driver_type_t;

#define CAN_PRINT (1 << 0)
#define CAN_REGISTER_KERNEL_DRIVERS (1 << 1)
#define KERNEL_PANIC (1 << 2)
#define FSGSBASE (1 << 0)

typedef struct {
    uint64_t uptime; // Ticks
	uint64_t fs_base;
    uint64_t gs_base;
    uint64_t kernel_gs_base;
	uint32_t flags; // CAN_REGISTER_KERNEL_DRIVERS, CAN_PRINT, KERNEL_PANIC
    uint16_t cpu_flags; // FSGSBASE
} system_info_t;

typedef enum {
    DISK_TYPE_UNKNOWN = 0,
    DISK_TYPE_IDE,
    DISK_TYPE_AHCI,
    DISK_TYPE_NVME,
    DISK_TYPE_USB,
    DISK_TYPE_RAM
} disk_connection_type_t;

typedef struct {
    uint64_t id;
    uint64_t total_sectors;
    uint32_t sector_size;
    disk_connection_type_t type;
    char model[40];
    uint8_t is_removable;
} disk_info_t;

typedef struct {
    uint64_t id;
    uint64_t parent_disk_id;
    uint64_t start_lba;
    uint64_t size_sectors;
    uint8_t  partition_type;
    uint8_t  bootable;
} partition_info_t;

typedef struct {
    uint64_t disk_id;
    uint64_t partition_offset_lba;
    uint64_t size_sectors;
} block_dev_t;

typedef enum {
    VFS_FILE_TYPE_UNKNOWN = 0,
    VFS_FILE_TYPE_REGULAR,    // Обычный файл (текст, бинарник)
    VFS_FILE_TYPE_DIR,        // Директория
    VFS_FILE_TYPE_SYMLINK,    // Символическая ссылка
    VFS_FILE_TYPE_DEVICE      // Устройство (наши raw, ctl, kram)
} vfs_file_type_t;

typedef struct {
    char name[256];
    uint64_t size;
    uint32_t type;
    uint32_t reserved;
} vfs_dirent_t;

typedef struct malloc_header {
    uint64_t size;
    uint64_t is_free;
    struct malloc_header* next;
    uint64_t padding;
} __attribute__((aligned(16))) malloc_header_t;

typedef struct {
    void*    tcb_self;      // 0x00: Указатель на себя (требование ABI)
    uint64_t tid;           // 0x08: Идентификатор потока (TID)
    uint64_t pid;           // 0x10: Идентификатор процесса (PID)
    int32_t  thread_errno;  // 0x18: Потокобезопасная переменная ошибки             // Reserved
    uint32_t pending_msgs;  // 0x1C: Количество непрочитанных IPC-сообщений
    void*    local_heap;    // 0x20: Указатель для быстрого malloc                  // Reserved
    uint64_t stack_canary;  // 0x28: Канарейка для безопасности (-fstack-protector)
} aos_tcb_t;

#ifndef offsetof
#define offsetof(TYPE, MEMBER) __builtin_offsetof(TYPE, MEMBER)
#endif

#ifndef AOSKERNEL
int64_t syscall(uint64_t nr, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5);
__attribute__((noreturn)) void exit(int code);
__attribute__((noreturn)) void _start(uint64_t arg1, uint64_t arg2);

#define AOS_GET_TCB() ((aos_tcb_t __seg_fs *)0)

#ifdef AOSLIB_START_ONLY

void vfs_init();

#else

void syscall_system_print(const char* str);

int64_t __syscall_ipc_recv(message_t* out_msg);
int64_t syscall_ipc_send(uint64_t dest_tid, message_t* msg);
uint64_t syscall_get_msg_count(void);
void syscall_ipc_recv(message_t* out_msg);
void syscall_ipc_recv_filtered(uint64_t tid, msg_type_t type, msg_subtype_t subtype, message_t* out_msg);

int64_t syscall_register_driver(driver_type_t type, const char* name);
uint64_t syscall_get_driver_tid(driver_type_t type);
uint64_t syscall_get_driver_tid_by_name(const char* name);

uint8_t syscall_get_kbd_scancode();
int syscall_get_sysinfo(system_info_t* info);

void* syscall_sbrk(int64_t increment);
void* malloc(uint64_t size);
void free(void* ptr);
void* realloc(void* ptr, uint64_t new_size);
void* calloc(uint64_t num, uint64_t size);

void* memset(void* ptr, uint8_t value, uint64_t n);
void* memcpy(void* dest, const void* src, uint64_t n);

int32_t strcmp(const char* s1, const char* s2);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, uint64_t n);
uint64_t strlcpy(char *dest, const char *src, uint64_t n);
char* strdup(const char* s);
char* strtok_r(char* str, const char* delim, char** saveptr);
char* strtok(char* str, const char* delim);
char* strsep(char** stringp, const char* delim);
char* strchr(const char* s, int32_t c);
char* strrchr(const char* s, int32_t c);
char* strnchr(const char* s, uint64_t count, int32_t c);
char* strstr(const char* haystack, const char* needle);
uint64_t strlen(const char* s);
uint64_t strnlen(const char* s, uint64_t maxlen);
char* strcat(char* dest, const char* src);
char* strncat(char* dest, const char* src, uint64_t n);
uint64_t strlcat(char* dest, const char* src, uint64_t size);

int32_t isdigit(int32_t c);
int32_t islower(int32_t c);
int32_t isupper(int32_t c);
int32_t isalpha(int32_t c);
int32_t isalnum(int32_t c);
int32_t isxdigit(int32_t c);
int32_t isspace(int32_t c);
int32_t isprint(int32_t c);
int32_t iscntrl(int32_t c);
int32_t ispunct(int32_t c);

int32_t tolower(int32_t c);
int32_t toupper(int32_t c);

int32_t is_digit(const char* str);
char* to_upper(char* s);

int32_t printf(const char* format, ...);
int32_t snprintf(char* str, uint64_t size, const char* format, ...);
int32_t sprintf(char* str, const char* format, ...);

#ifndef AOSLIB_SYSCALLS_ONLY

int64_t block_read(block_dev_t* dev, uint64_t lba, uint64_t count, void* buffer);
int64_t block_write(block_dev_t* dev, uint64_t lba, uint64_t count, void* buffer);
void vfs_init();
int vfs_open(const char* path);
int vfs_close(int fd);
int vfs_read(int fd, void* buf, int count);
int vfs_write(int fd, const void* buf, int count);
int vfs_readdir(int fd, int index, vfs_dirent_t* out_entry);

#endif
#endif
#endif
#endif