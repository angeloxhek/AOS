#include <stddef.h>
#include <stdint.h>

extern int main(int argc, char** argv);
extern void exit(int code);

__attribute__((noreturn)) void _start(uint64_t arg1, uint64_t arg2) {
    int exit_code = main((int)arg1, (char**)arg2);
    exit(exit_code);
    __builtin_unreachable();
}