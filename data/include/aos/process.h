#ifndef AOS_PROCESS_H
#define AOS_PROCESS_H

#include "types.h"
#include "auth.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_ZOMBIE
} thread_state_t;

typedef struct {
    apid_t   pid;
    char     name[32];
    uint8_t  state;
    uint64_t heap_limit;
    uint64_t threads_count;
    auth_id_t user;
} proc_info_user_t;

typedef struct {
    uint64_t tid;
    uint32_t parent_pid;
    thread_state_t  state;
    int      waiting_for_msg; 
    uint64_t wake_up_time;
} thread_info_user_t;

typedef struct {
    uint64_t uptime;
    uint64_t boot_time;
    uint64_t frequency;
} time_info_t;

typedef struct {
    void*       tcb_self;
    uint64_t    tid;
    apid_t      pid;
    uint64_t    reserved1[2];
    uint64_t    stack_canary;
} aos_tcb_t;

#define PEB_VIRT_ADDR 0x00007FFFFE000000

typedef struct {
    apid_t pid;
    volatile uint64_t pending_msgs; 
    time_info_t startup_time;
    char process_name[32];
} aos_peb_t;

#define AOS_GET_TCB() ((aos_tcb_t __seg_fs *)0)
#define AOS_GET_PEB() ((aos_peb_t*)PEB_VIRT_ADDR)

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

typedef struct {
    const char* name;
    uint8_t* data;
    uint64_t size;
    startup_info_t* info;
    uint64_t arg_val;
} spawn_args_t;

#ifdef AOSLIB_START
__attribute__((noreturn)) void _start(startup_info_t* arg1, uint64_t arg2);
__attribute__((noreturn)) void exit(int code);
#endif

#ifdef AOSLIB_SYSCALLS
int sysspawn(const char* path, startup_info_t* info, uint64_t arg2);
int sysspawnex(spawn_args_t* args);
uint32_t sysfork(void);
int sysexec(const char* path, startup_info_t* info, uint64_t arg2);
int sysexecex(spawn_args_t* args);

int get_proc_info(apid_t pid, proc_info_user_t* out_info);
int get_thread_info(uint64_t tid, thread_info_user_t* out_info);
int get_pid_list(apid_t* buff, uint64_t* count);
int get_tid_list(apid_t pid, uint64_t* buff, uint64_t* count);

void thread_yield(void);
void syssleep(uint64_t ms);
int sleep_while_zero(int (*func)(void*), void* arg, uint64_t timeout_ms, int* out_result);

int get_time_info(time_info_t* info);
#endif

#ifdef __cplusplus
}
#endif
#endif // AOS_PROCESS_H