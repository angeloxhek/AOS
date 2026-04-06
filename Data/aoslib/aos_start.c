#define AOSLIB_START_ONLY
#include "../include/aoslib.h"

__attribute__((weak)) int main(int argc, char** argv);
__attribute__((weak)) int driver_main(void* reserved1, void* reserved2);

__attribute__((noreturn)) void _start(uint64_t arg1, uint64_t arg2) {
    int exit_code = STAT_NO_ENTRY;
    if (driver_main != (void*)0) {
        exit_code = driver_main((void*)arg1, (void*)arg2);
    }
    else if (main != (void*)0) {
		vfs_init();
        exit_code = main((int)arg1, (char**)arg2);
    }
    exit(exit_code);
	__builtin_unreachable();
}