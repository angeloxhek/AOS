#!/bin/bash

# Остановить скрипт при любой ошибке
set -e

# Цвета для вывода (для красоты)
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color
STAGES='6'

trap 'echo -e "${RED}Build failed!${NC}"; exit 1' ERR

echo -e "${GREEN}[1/${STAGES}] Preparing directories...${NC}"
cd "$(dirname "$0")"
mkdir -p Build/Volume/Drivers
mkdir -p Build/libs
mkdir -p Temp
rm -f Temp/*

echo -e "${GREEN}[2/${STAGES}] Compiling PBR and Tools...${NC}"
nasm -o Build/pbr.bin Data/pbr.asm

echo -e "${GREEN}[3/${STAGES}] Compiling AOSLDR.BIN...${NC}"

KERNEL_CFLAGS="-m64 -g3 -O0 -Wall -fno-omit-frame-pointer -mcmodel=kernel -mno-red-zone \
        -ffreestanding -mgeneral-regs-only -fno-pic -fno-pie -fstack-protector"

nasm -f elf64 -o Temp/asmaosldr.o Data/aosldr.asm
gcc $KERNEL_CFLAGS -c Data/aosldr.c   -o Temp/caosldr.o
gcc $KERNEL_CFLAGS -c Data/pmm.c      -o Temp/pmm.o
gcc $KERNEL_CFLAGS -c Data/vmm.c      -o Temp/vmm.o
gcc $KERNEL_CFLAGS -c Data/sched.c    -o Temp/sched.o
gcc $KERNEL_CFLAGS -c Data/ipc.c      -o Temp/ipc.o
gcc $KERNEL_CFLAGS -c Data/syscall.c  -o Temp/syscall.o
gcc $KERNEL_CFLAGS -c Data/ide.c      -o Temp/ide.o
gcc $KERNEL_CFLAGS -c Data/elf.c      -o Temp/elf.o
gcc $KERNEL_CFLAGS -c Data/console.c  -o Temp/console.o
gcc $KERNEL_CFLAGS -c Data/shm.c      -o Temp/shm.o

ld -m elf_x86_64 --no-warn-rwx-segments -T Data/aosldr.ld -Map Temp/aosldr.map -o Temp/aosldr.elf \
        Temp/asmaosldr.o \
        Temp/caosldr.o \
        Temp/pmm.o \
        Temp/vmm.o \
        Temp/sched.o \
        Temp/ipc.o \
        Temp/syscall.o \
        Temp/ide.o \
        Temp/elf.o \
        Temp/console.o \
        Temp/shm.o
objcopy -O binary -S -R .bss -R .note -R .comment -R .note.gnu.property Temp/aosldr.elf Build/Volume/AOSLDR.BIN

echo -e "${GREEN}[4/${STAGES}] Compiling AOSLIB...${NC}"

LIB_CFLAGS="-m64 -fno-omit-frame-pointer -ffreestanding -fno-pic -fno-pie -fno-asynchronous-unwind-tables"

gcc $LIB_CFLAGS -c Data/aoslib/aos_syscalls.c -o Temp/aos_syscalls.o
gcc $LIB_CFLAGS -c Data/aoslib/aos_vfs.c      -o Temp/aos_vfs.o
gcc $LIB_CFLAGS -c Data/aoslib/aos_sync.c     -o Temp/aos_sync.o
gcc $LIB_CFLAGS -c Data/aoslib/aos_utils.c    -o Temp/aos_utils.o

gcc $LIB_CFLAGS -c Data/aoslib/libc_stdlib.c  -o Temp/libc_stdlib.o
gcc $LIB_CFLAGS -c Data/aoslib/libc_ctype.c   -o Temp/libc_ctype.o
gcc $LIB_CFLAGS -c Data/aoslib/libc_stdio.c   -o Temp/libc_stdio.o
gcc $LIB_CFLAGS -c Data/aoslib/libc_string.c  -o Temp/libc_string.o

gcc $LIB_CFLAGS -c Data/aoslib/aos_start.c    -o Temp/aos_start.o
gcc $LIB_CFLAGS -c Data/aoslib/linux_start.c   -o Temp/linux_start.o

ar rcs Temp/libaos.a \
    Temp/aos_syscalls.o Temp/aos_vfs.o Temp/aos_sync.o Temp/aos_utils.o \
    Temp/libc_stdlib.o Temp/libc_ctype.o Temp/libc_stdio.o Temp/libc_string.o
cp Temp/libaos.a Build/libs/libaos.a
	
ar rcs Temp/libaoslin.a \
    Temp/aos_syscalls.o Temp/aos_vfs.o Temp/aos_sync.o Temp/aos_utils.o \
    Temp/libc_stdlib.o Temp/libc_ctype.o Temp/libc_stdio.o Temp/libc_string.o
	
cp Temp/libaoslin.a Build/libs/libaoslin.a

echo -e "${GREEN}[5/${STAGES}] Compiling Drivers...${NC}"
gcc -m64 -c Data/drivers/kbddriver.c -o Temp/kbddriver.o -fno-omit-frame-pointer \
        -ffreestanding -fno-pic -fno-pie -fno-asynchronous-unwind-tables
ld -m elf_x86_64 -N --no-warn-rwx-segments -Map Temp/kbddriver.map -T Data/drivers/vfsdriver.ld \
		Temp/aos_start.o Temp/kbddriver.o Temp/libaos.a -o Build/Volume/DRIVERS/KBDDRIVER.ELF

gcc -m64 -c Data/drivers/vfsdriver.c -o Temp/vfsdriver.o -fno-omit-frame-pointer \
        -ffreestanding -fno-pic -fno-pie -fno-asynchronous-unwind-tables
gcc -m64 -c Data/drivers/fat32_vfsmodule.c -o Temp/fat32_vfsmodule.o -fno-omit-frame-pointer \
        -ffreestanding -fno-pic -fno-pie -fno-asynchronous-unwind-tables 
gcc -m64 -c Data/drivers/procfs_vfsmodule.c -o Temp/procfs_vfsmodule.o -fno-omit-frame-pointer \
        -ffreestanding -fno-pic -fno-pie -fno-asynchronous-unwind-tables 
ld -m elf_x86_64 -N --no-warn-rwx-segments -Map Temp/vfsdriver.map -T Data/drivers/vfsdriver.ld \
        Temp/aos_start.o Temp/vfsdriver.o Temp/fat32_vfsmodule.o Temp/procfs_vfsmodule.o Temp/libaos.a \
        -o Build/Volume/DRIVERS/VFSDRIVER.ELF

echo -e "${GREEN}[6/${STAGES}] Compiling User-Space...${NC}"
gcc -m64 -c Data/userspace/tree.c -o Temp/tree.o -fno-omit-frame-pointer -ffreestanding \
        -fno-pic -fno-pie -fno-asynchronous-unwind-tables
ld -m elf_x86_64 -Map Temp/tree.map -N --no-warn-rwx-segments -T Data/drivers/vfsdriver.ld \
		Temp/aos_start.o Temp/tree.o Temp/libaos.a \
        -o Build/Volume/tree.elf

echo -e "${GREEN}Build Successful!${NC}"
