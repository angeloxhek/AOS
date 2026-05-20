#include <aos/types.h>
#include <stddef.h>
#include <stdint.h>

extern int main(int argc, char** argv);
extern void exit(int code);

__attribute__((noreturn)) void _start(startup_info_t* info, uint64_t arg2) {
    int exit_code = main(0, 0);
    exit(exit_code);
    __builtin_unreachable();
}