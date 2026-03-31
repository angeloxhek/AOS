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
mkdir -p Build/Debug
mkdir -p Build/FirstVolume/Drivers
mkdir -p Temp
rm -f Temp/*

echo -e "${GREEN}[2/${STAGES}] Compiling PBR and Tools...${NC}"
nasm -o Build/pbr.bin Data/pbr.asm
nasm -o Build/Debug/pbrdebug.bin Data/pbrdebug.asm
nasm -o Build/Debug/pbrscan.bin Data/fat32scan.asm

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
objcopy -O binary -S -R .bss -R .note -R .comment -R .note.gnu.property Temp/aosldr.elf Build/FirstVolume/AOSLDR.BIN
nasm -o Build/Debug/AOSLDRDEBUG.BIN Data/aosldrdebug.asm

echo -e "${GREEN}[4/${STAGES}] Compiling AOSLIB...${NC}"
gcc -m64 -c Data/aoslib/syscalls.c -o Temp/syscalls.o -fno-omit-frame-pointer \
         -ffreestanding -fno-pic -fno-pie -fno-asynchronous-unwind-tables
gcc -m64 -c Data/aoslib/filesystem.c -o Temp/filesystem.o -fno-omit-frame-pointer \
        -ffreestanding -fno-pic -fno-pie -fno-asynchronous-unwind-tables
gcc -m64 -c Data/aoslib/string.c -o Temp/string.o -fno-omit-frame-pointer \
        -ffreestanding -fno-pic -fno-pie -fno-asynchronous-unwind-tables
gcc -m64 -c Data/aoslib/start.c -o Temp/start.o -fno-omit-frame-pointer \
        -ffreestanding -fno-pic -fno-pie -fno-asynchronous-unwind-tables

echo -e "${GREEN}[5/${STAGES}] Compiling Drivers...${NC}"
gcc -m64 -c Data/drivers/kbddriver.c -o Temp/kbddriver.o -fno-omit-frame-pointer \
        -ffreestanding -fno-pic -fno-pie -fno-asynchronous-unwind-tables
ld -m elf_x86_64 -N --no-warn-rwx-segments -Map Temp/kbddriver.map -T Data/drivers/vfsdriver.ld Temp/kbddriver.o \
        Temp/syscalls.o Temp/string.o Temp/start.o -o Build/FirstVolume/DRIVERS/KBDDRIVER.ELF

gcc -m64 -c Data/drivers/vfsdriver.c -o Temp/vfsdriver.o -fno-omit-frame-pointer \
        -ffreestanding -fno-pic -fno-pie -fno-asynchronous-unwind-tables
gcc -m64 -c Data/drivers/fat32_vfsmodule.c -o Temp/fat32_vfsmodule.o -fno-omit-frame-pointer \
        -ffreestanding -fno-pic -fno-pie -fno-asynchronous-unwind-tables
ld -m elf_x86_64 -N --no-warn-rwx-segments -Map Temp/vfsdriver.map -T Data/drivers/vfsdriver.ld Temp/vfsdriver.o \
        Temp/syscalls.o Temp/string.o Temp/filesystem.o Temp/fat32_vfsmodule.o Temp/start.o \
        -o Build/FirstVolume/DRIVERS/VFSDRIVER.ELF

echo -e "${GREEN}[6/${STAGES}] Compiling User-Space...${NC}"
gcc -m64 -c Data/userspace/tree.c -o Temp/tree.o -fno-omit-frame-pointer -ffreestanding \
        -fno-pic -fno-pie -fno-asynchronous-unwind-tables
ld -m elf_x86_64 -Map Temp/tree.map -N --no-warn-rwx-segments -T Data/drivers/vfsdriver.ld Temp/tree.o \
        Temp/syscalls.o Temp/string.o Temp/filesystem.o Temp/start.o \
        -o Build/FirstVolume/tree.elf

echo -e "${GREEN}Build Successful!${NC}"
