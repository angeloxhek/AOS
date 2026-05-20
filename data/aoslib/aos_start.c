#define AOSLIB_START_ONLY
#include "../include/aoslib.h"

__attribute__((weak)) int main(int argc, char** argv);
__attribute__((weak)) int driver_main(void* reserved1, void* reserved2);

__attribute__((noreturn)) void _start(startup_info_t* info, uint64_t arg2) {
    int exit_code = STAT_NO_ENTRY;
    if (driver_main != (void*)0) {
        exit_code = driver_main(0, 0);
    }
    else if (main != (void*)0) {
        exit_code = main(0, 0);
    }
    exit(exit_code);
    __builtin_unreachable();
}