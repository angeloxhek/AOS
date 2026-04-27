#ifndef AOSLIB

#if defined(AOSLIB_START_ONLY)
#define AOSLIB_START
#elif defined(AOSLIB_SYSCALLS_ONLY)
#define AOSLIB_SYSCALLS
#elif defined(AOSLIB_VFS_ONLY)
#define AOSLIB_VFS
#elif defined(AOSLIB_STRING_ONLY)
#define AOSLIB_STRING
#elif defined(AOSLIB_IO_ONLY)
#define AOSLIB_IO
#elif !defined(AOSKERNEL)
#define AOSLIB
#define AOSLIB_START
#define AOSLIB_SYSCALLS
#define AOSLIB_VFS
#define AOSLIB_STRING
#define AOSLIB_IO
#endif

#ifndef AOSLIB_DEFINE
#define AOSLIB_DEFINE

#include <stdint.h>
#include <limits.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SYS_EXIT                      1
#define SYS_IPC_SEND                  2
#define SYS_IPC_TRYRECV               3
#define SYS_IPC_RECV                  4
#define SYS_REGISTER_DRIVER           5
#define SYS_GET_DRIVER_TID            6
#define SYS_GET_DRIVER_TID_BY_NAME    7
#define SYS_GET_SYSTEM_INFO           8
#define SYS_SBRK                      9
#define SYS_BLOCK_READ               10
#define SYS_BLOCK_WRITE              11
#define SYS_GET_DISK_COUNT           12
#define SYS_GET_DISK_INFO            13
#define SYS_GET_PARTITION_COUNT      14
#define SYS_GET_PARTITION_INFO       15
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

#define SYS_RES_OK                    0
#define SYS_RES_INVALID              -1
#define SYS_RES_NO_PERM              -2
#define SYS_RES_ALREADY              -3
#define SYS_RES_DRV_ERR              -4
#define SYS_RES_QUEUE_EMPTY          -5
#define SYS_RES_DSK_ERR              -6
#define SYS_RES_RANGE                -7
#define SYS_RES_NOTFOUND             -8
#define SYS_RES_KERNEL_ERR          -99

#define DRV_ERR_OK                    0
#define DRV_ERR_NOCOMM             -258
#define DRV_ERR_NOTFOUND           -257
#define DRV_ERR_UNKNOWN            -256

#define VFS_ERR_OK                 DRV_ERR_OK
#define VFS_ERR_PERM                 -1
#define VFS_ERR_ISDIR                -2
#define VFS_ERR_BUSY                 -3
#define VFS_ERR_SYMLINKLOOP          -4
#define VFS_ERR_NOCOMM             DRV_ERR_NOCOMM
#define VFS_ERR_NOTFOUND           DRV_ERR_NOTFOUND
#define VFS_ERR_UNKNOWN            DRV_ERR_UNKNOWN

#define AUTH_ERR_OK                DRV_ERR_OK
#define AUTH_ERR_USER                -1
#define AUTH_ERR_NOCOMM            DRV_ERR_NOCOMM
#define AUTH_ERR_NOTFOUND          DRV_ERR_NOTFOUND
#define AUTH_ERR_UNKNOWN           DRV_ERR_UNKNOWN

#define STAT_OK                       0
#define STAT_STACK_SMASHING        -256
#define STAT_NO_ENTRY              -257
#define STAT_OOM                   -258

#define VFS_FREAD   (1 << 0)
#define VFS_FWRITE  (1 << 1)
#define VFS_FRW     (VFS_FREAD | VFS_FWRITE)
#define VFS_FCREATE (1 << 2)
#define VFS_FAPPEND (1 << 3)
#define VFS_FTRUNC  (1 << 4)

typedef enum {
    VFS_CMD_OPEN  = 1,
	VFS_CMD_OPENAT,
    VFS_CMD_CLOSE,
    VFS_CMD_READ,
    VFS_CMD_WRITE,
    VFS_CMD_STAT,
    VFS_CMD_LIST,
	VFS_CMD_FLOCK,
	VFS_CMD_SEEK
} vfs_cmd_t;

typedef enum {
	VFS_LOCK_UN = 1,
	VFS_LOCK_SH,
	VFS_LOCK_EX
} vfs_lock_type_t;

typedef enum {
	VFS_SEEK_SET = 1,
	VFS_SEEK_CUR,
	VFS_SEEK_END
} vfs_seek_t;

typedef enum {
    VFS_FILE_TYPE_UNKNOWN = 0,
    VFS_FILE_TYPE_REGULAR,
    VFS_FILE_TYPE_DIR,
    VFS_FILE_TYPE_SYMLINK,
    VFS_FILE_TYPE_DEVICE
} vfs_file_type_t;

typedef struct {
    char name[256];
    uint64_t size;
    uint32_t type;
    uint32_t reserved;
} vfs_dirent_t;

typedef struct {
	char name[256];
    uint64_t inode_id;
    uint64_t size_bytes;
    uint32_t attributes;
} vfs_stat_info_t;

typedef enum {
    AUTH_CMD_GET_USER = 1,
	AUTH_CMD_ADD_USER,
	AUTH_CMD_DEL_USER,
	AUTH_CMD_UPDATE_USER
} auth_cmd_t;

typedef enum : uint8_t {
	PGROUP_SUPER = 0,
	PGROUP_ROOT,
	PGROUP_ADMIN,
	PGROUP_USER,
	PGROUP_TEMP
} auth_pgroup_t;

#define ATYPE_NONE         (0 << 0)
#define ATYPE_CHILD        (1 << 0)
#define ATYPE_TOKEN        (1 << 1)
#define ATYPE_PASSWORD     (1 << 2)
#define ATYPE_CHANGE       (1 << 3)

#define ATYPE_SUPER        (ATYPE_CHILD | ATYPE_CHANGE)
#define ATYPE_ROOT         (ATYPE_CHILD | ATYPE_PASSWORD | ATYPE_CHANGE)
#define ATYPE_ADMIN        (ATYPE_CHILD | ATYPE_TOKEN | ATYPE_PASSWORD | ATYPE_CHANGE)
#define ATYPE_USER         (ATYPE_CHILD | ATYPE_TOKEN | ATYPE_PASSWORD | ATYPE_CHANGE)
#define ATYPE_TEMP         (ATYPE_CHILD | ATYPE_TOKEN | ATYPE_PASSWORD)

#define APERM_NONE         (0 << 0)

typedef union {
	struct {
		uint32_t gid; // Dynamic group
		uint32_t uid;
	} user;
	uint64_t raw;
} auth_id_t;

typedef struct {
	auth_id_t id;
	auth_pgroup_t pgroup; // Static group
	uint8_t auth_type; // ATYPE_*
	uint32_t perms;
} auth_idex_t;

typedef enum {
    MSG_TYPE_NONE = 0,
	MSG_TYPE_AUTH,
    MSG_TYPE_VFS,
	MSG_TYPE_KEYBOARD,
    MSG_TYPE_DATA
} msg_type_t;

typedef enum {
    MSG_SUBTYPE_NONE = 0,
    MSG_SUBTYPE_QUERY,
	MSG_SUBTYPE_SEND,
	MSG_SUBTYPE_RESPONSE,
	MSG_SUBTYPE_PING,
	MSG_SUBTYPE_PONG
} msg_subtype_t;

typedef struct message_t {
    uint64_t sender_tid;
    uint32_t type;
	uint32_t subtype;
    uint64_t param1;
    uint64_t param2;
    uint64_t param3;
	uint8_t  data[64];
} __attribute__((packed, aligned(8))) message_t;

typedef enum {
	SEEK_SET = 1,
	SEEK_CUR,
	SEEK_END
} seek_whence_t;

typedef enum {
	DT_NONE = 0,
	DT_AUTH,
	DT_VFS,
	DT_VIDEO,
	DT_KEYBOARD,
	DT_USER = 100
} driver_type_t;

#define CAN_PRINT (1 << 0)
#define CAN_REGISTER_KERNEL_DRIVERS (1 << 1)
#define KERNEL_PANIC (1 << 2)
#define FSGSBASE (1 << 0)

typedef struct {
    uint64_t uptime;
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
    uint32_t pid;
    char     name[32];
    uint8_t  state;
    uint64_t heap_limit;
    uint64_t threads_count;
} proc_info_user_t;

typedef struct {
    uint64_t tid;
	auth_id_t user;
    uint32_t parent_pid;
    uint8_t  state;
    int      waiting_for_msg; 
    uint64_t wake_up_time;
} thread_info_user_t;

typedef struct {
    uint64_t disk_id;
    uint64_t partition_offset_lba;
    uint64_t size_sectors;
} block_dev_t;

typedef struct malloc_header {
    uint64_t size;
    uint64_t is_free;
    struct malloc_header* next;
    uint64_t padding;
} __attribute__((aligned(16))) malloc_header_t;

typedef struct {
    uint64_t uptime;
    uint64_t boot_time;
    uint64_t frequency;
} time_info_t;

typedef struct {
    void*       tcb_self;
    uint64_t    tid;
    uint64_t    pid;
    int32_t     thread_errno; // Reserved
    uint32_t    pending_msgs;
    void*       local_heap;   // Reserved
    uint64_t    stack_canary;
	time_info_t startup_time;
} aos_tcb_t;

typedef enum : uint8_t {
	STARTUP_MAIN = 1,
	STARTUP_DRIVERMAIN
} startup_type_t;

typedef struct {
	startup_type_t type;
	union {
		struct {
			int argc;
			int envc;
			char** argv;
			char** envp;
		} main;
		struct {
			void* reserved1;
			void* reserved2;
		} driver;
	} data;
} startup_info_t;

#define AOS_GET_TCB() ((aos_tcb_t __seg_fs *)0)

#define AOS_HANDLE_SUBTYPE_CHECK(st) do { \
_Static_assert(__builtin_types_compatible_p(typeof(*(in)), message_t), "AOS_HANDLE_SUBTYPE_CHECK: 'in' must be message_t*"); \
_Static_assert(__builtin_types_compatible_p(typeof(*(out)), message_t), "AOS_HANDLE_SUBTYPE_CHECK: 'out' must be message_t*"); \
if (in->subtype != (st)) { out->param1 = DRV_ERR_NOCOMM; break; } \
} while(0)

typedef struct {
    volatile int locked;
} mutex_t;

#ifndef offsetof
    #define offsetof(TYPE, MEMBER)  __builtin_offsetof(TYPE, MEMBER)
#endif

#ifndef _SIZE_T_DEFINED
#define _SIZE_T_DEFINED
    #ifdef __SIZE_TYPE__
        typedef __SIZE_TYPE__ size_t;
    #else
        typedef unsigned long size_t;
    #endif
#endif

#ifndef UNUSED
    #define UNUSED(x) (void)(x)
#endif

#undef NULL

#ifdef __cplusplus
    #define NULL __null
#else
    #define NULL ((void *)0)
#endif

#endif

#if defined(AOSLIB_START) || defined(AOSLIB_VFS)
void vfs_init();
#endif

#ifdef AOSLIB_START
int64_t syscall(uint64_t nr, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5);
__attribute__((noreturn)) void exit(int code);
__attribute__((noreturn)) void _start(startup_info_t* arg1, uint64_t arg2);
#endif

#ifdef AOSLIB_SYSCALLS
void sysprint(const char* str);

int64_t __ipc_recv(message_t* out_msg);
int64_t ipc_tryrecv(message_t* out_msg);
int64_t ipc_send(uint64_t dest_tid, message_t* msg);
uint64_t get_ipc_count(void);
void ipc_recv(message_t* out_msg);
void ipc_recv_ex(uint64_t tid, msg_type_t type, msg_subtype_t subtype, message_t* out_msg);
void ipc_seek(int64_t offset, seek_whence_t whence);
int ipc_get_at(uint64_t index, message_t* out);

int64_t register_driver(driver_type_t type, const char* name);
uint64_t get_driver_tid(driver_type_t type);
uint64_t get_driver_tid_name(const char* name);

uint8_t get_scancode();
int get_sysinfo(system_info_t* info);
void* syscall_sbrk(int64_t increment);

uint64_t get_disk_count(void);
uint64_t get_partition_count(void);
uint64_t get_disk_info(uint64_t index, disk_info_t* pinfo);
uint64_t get_partition_info(uint64_t index, partition_info_t* pinfo);

int get_proc_info(uint32_t pid, proc_info_user_t* out_info);
int get_thread_info(uint64_t tid, thread_info_user_t* out_info);
int get_pid_list(uint32_t* buff, uint64_t count);
int get_tid_list(uint32_t pid, uint32_t* buff, uint64_t count);

uint64_t shm_alloc(uint64_t size_bytes, void** out_vaddr);
int shm_allow(uint64_t shm_id, uint64_t target_tid);
void* shm_map(uint64_t shm_id);
int shm_free(uint64_t shm_id);

void thread_yield(void);

int get_time_info(time_info_t* info);

int sysspawn(const char* path, startup_info_t* info, uint64_t arg2);
uint32_t sysfork(void);
int sysexec(const char* path, startup_info_t* info, uint64_t arg2);
#endif

#ifdef AOSLIB_STRING
void* malloc(size_t size);
void free(void* ptr);
void* realloc(void* ptr, size_t new_size);
void* calloc(size_t num, size_t size);

void* memset(void* ptr, int value, size_t n);
void* memcpy(void* dest, const void* src, size_t n);
void* memmove(void* dest, const void* src, size_t n);
int memcmp(const void* s1, const void* s2, size_t n);

int strcmp(const char* s1, const char* s2);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
size_t strlcpy(char *dest, const char *src, size_t n);
char* strdup(const char* s);
char* strtok_r(char* str, const char* delim, char** saveptr);
char* strtok(char* str, const char* delim);
char* strsep(char** stringp, const char* delim);
char* strchr(const char* s, int c);
char* strrchr(const char* s, int c);
char* strnchr(const char* s, size_t count, int c);
char* strstr(const char* haystack, const char* needle);
size_t strlen(const char* s);
size_t strnlen(const char* s, size_t maxlen);
char* strcat(char* dest, const char* src);
char* strncat(char* dest, const char* src, size_t n);
size_t strlcat(char* dest, const char* src, size_t size);

int isdigit(int c);
int islower(int c);
int isupper(int c);
int isalpha(int c);
int isalnum(int c);
int isxdigit(int c);
int isspace(int c);
int isprint(int c);
int iscntrl(int c);
int ispunct(int c);

int tolower(int c);
int toupper(int c);

int is_digit(const char* str);
char* to_upper(char* s);

int printf(const char* format, ...);
int snprintf(char* str, size_t size, const char* format, ...);
int sprintf(char* str, const char* format, ...);

int atoi(const char *str);
long atol(const char *str);
long long atoll(const char *str);

unsigned long long strtoull(const char *nptr, char **endptr, int base);
long long strtoll(const char *nptr, char **endptr, int base);

int kstrtoull(const char *s, int base, unsigned long long *res);
int kstrtoll(const char *s, int base, long long *res);
int kstrtoint(const char *s, int base, int *res);
int kstrtobool(const char *s, bool *res);

char* itoa(int value, char* str, int base);
char* utoa(unsigned int value, char* str, int base);
char* ltoa(long value, char* str, int base);
char* ultoa(unsigned long value, char* str, int base);
char* lltoa(long long value, char* str, int base);
char* ulltoa(unsigned long long value, char* str, int base);

int strcasecmp(const char *s1, const char *s2);
int strncasecmp(const char *s1, const char *s2, size_t n);

void bcopy(const void *src, void *dest, size_t n);
void bzero(void *s, size_t n);
int  bcmp(const void *s1, const void *s2, size_t n);
#endif

#ifdef AOSLIB_VFS

int64_t block_read(block_dev_t* dev, uint64_t lba, uint64_t count, void* buffer);
int64_t block_write(block_dev_t* dev, uint64_t lba, uint64_t count, void* buffer);
int vfs_open(const char* path, uint32_t flags);
int vfs_openat(int dir_fd, const char* name, uint32_t flags);
int vfs_close(int fd);
int vfs_read(int fd, void* buf, int count);
int vfs_write(int fd, const void* buf, int count);
int vfs_readdir(int fd, vfs_dirent_t* out_entries, int max_entries);
int vfs_flock(int fd, vfs_lock_type_t lock_type);
int64_t vfs_seek(int fd, int64_t offset, vfs_seek_t whence);
int vfs_stat(int fd, vfs_stat_info_t* out_stat);

#endif

#ifdef AOSLIB_IO

void mutex_init(mutex_t* m);
void mutex_lock(mutex_t* m);
void mutex_unlock(mutex_t* m);

#endif

#ifdef __cplusplus
}
#endif

#endif