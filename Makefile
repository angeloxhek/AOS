CC = gcc
NASM = nasm
LD = ld
AR = ar
OBJCOPY = objcopy
CP = cp

KERNEL_CFLAGS = -m64 -g3 -O0 -Wall -fno-omit-frame-pointer -mcmodel=kernel -mno-red-zone \
				-ffreestanding -mgeneral-regs-only -fno-pic -fno-pie -fstack-protector
AOSLIB_CFLAGS = -I $(CURDIR)/Data/include
LIBC_CFLAGS = -I $(CURDIR)/aosliblin/include
LIB_CFLAGS = -m64 -fno-omit-frame-pointer -ffreestanding -fno-pic -fno-pie -fno-asynchronous-unwind-tables -nostdinc
DRV_CFLAGS = -m64 -fno-omit-frame-pointer -ffreestanding -fno-pic -fno-pie -fno-asynchronous-unwind-tables
USR_CFLAGS = -m64 -fno-omit-frame-pointer -ffreestanding -fno-pic -fno-pie -fno-asynchronous-unwind-tables
LDFLAGS = -m elf_x86_64 --no-warn-rwx-segments

BUILD_DIR = $(CURDIR)/Build
TEMP_DIR = $(CURDIR)/Temp
DISK_DIR = $(BUILD_DIR)/Volume
DRIVERS_DIR = $(DISK_DIR)/DRIVERS

KERNEL_OBJS = $(TEMP_DIR)/asmaosldr.o $(TEMP_DIR)/caosldr.o $(TEMP_DIR)/pmm.o \
              $(TEMP_DIR)/vmm.o $(TEMP_DIR)/sched.o $(TEMP_DIR)/ipc.o \
              $(TEMP_DIR)/syscall.o $(TEMP_DIR)/ide.o $(TEMP_DIR)/elf.o \
              $(TEMP_DIR)/console.o $(TEMP_DIR)/shm.o $(TEMP_DIR)/vfs.o
			  
AOSLIB_OBJS = $(TEMP_DIR)/aos_syscalls.o $(TEMP_DIR)/aos_vfs.o $(TEMP_DIR)/aos_sync.o \
			  $(TEMP_DIR)/aos_utils.o $(TEMP_DIR)/aos_stdio.o $(TEMP_DIR)/aos_auth.o \
			  $(TEMP_DIR)/libc_stdlib.o $(TEMP_DIR)/libc_ctype.o $(TEMP_DIR)/libc_stdio.o \
			  $(TEMP_DIR)/libc_string.o $(TEMP_DIR)/libc_strings.o

AOSLIBLIN_OBJS = $(AOSLIB_OBJS) \
				 $(TEMP_DIR)/libc_unistd.o $(TEMP_DIR)/libc_time.o $(TEMP_DIR)/libc_sys_time.o \
				 $(TEMP_DIR)/libc_sys_stat.o $(TEMP_DIR)/libc_pwd.o
			  
.PHONY: all clean kernel drivers userspace userlinux

all: prepare kernel libs drivers userspace userlinux
	@echo "Build Successful!"
	
prepare:
	mkdir -p $(BUILD_DIR)/Volume/DRIVERS $(BUILD_DIR)/Volume/Configs $(BUILD_DIR)/libs $(TEMP_DIR)
	
kernel: $(BUILD_DIR)/Volume/AOSLDR.BIN

$(BUILD_DIR)/Volume/AOSLDR.BIN: $(KERNEL_OBJS) Data/aosldr.ld
	$(LD) $(LDFLAGS) -T Data/aosldr.ld -Map $(TEMP_DIR)/aosldr.map -o $(TEMP_DIR)/aosldr.elf $(KERNEL_OBJS)
	$(OBJCOPY) -O binary -S -R .bss -R .note -R .comment -R .note.gnu.property $(TEMP_DIR)/aosldr.elf $@
	
libs: $(BUILD_DIR)/libs/libaos.a $(BUILD_DIR)/libs/libaoslin.a $(TEMP_DIR)/aos_start.o $(TEMP_DIR)/libc_start.o

$(BUILD_DIR)/libs/libaos.a: $(AOSLIB_OBJS)
	$(AR) rcs $@ $^

$(BUILD_DIR)/libs/libaoslin.a: $(AOSLIBLIN_OBJS)
	$(AR) rcs $@ $^
	
drivers: $(DRIVERS_DIR)/INITDRIVER.ELF $(DRIVERS_DIR)/AUTHDRIVER.ELF $(DRIVERS_DIR)/VFSDRIVER.ELF

$(DRIVERS_DIR)/INITDRIVER.ELF: $(TEMP_DIR)/aos_start.o $(TEMP_DIR)/initdriver.o $(BUILD_DIR)/libs/libaos.a
	$(LD) $(LDFLAGS) -N -Map $(TEMP_DIR)/initdriver.map -T Data/drivers/driver.ld $^ -o $@

$(DRIVERS_DIR)/AUTHDRIVER.ELF: $(TEMP_DIR)/aos_start.o $(TEMP_DIR)/authdriver.o $(BUILD_DIR)/libs/libaos.a
	$(LD) $(LDFLAGS) -N -Map $(TEMP_DIR)/authdriver.map -T Data/drivers/driver.ld $^ -o $@
	
$(DRIVERS_DIR)/VFSDRIVER.ELF: $(TEMP_DIR)/aos_start.o $(TEMP_DIR)/vfsdriver.o $(TEMP_DIR)/fat32_vfsmodule.o $(TEMP_DIR)/procfs_vfsmodule.o $(BUILD_DIR)/libs/libaos.a
	$(LD) $(LDFLAGS) -N -Map $(TEMP_DIR)/vfsdriver.map -T Data/drivers/driver.ld $^ -o $@
	
configs: $(CP) -r Configs $(DISK_DIR)

userspace: $(DISK_DIR)/tree.elf

$(DISK_DIR)/tree.elf: $(TEMP_DIR)/aos_start.o $(TEMP_DIR)/tree.o $(BUILD_DIR)/libs/libaos.a
	$(LD) $(LDFLAGS) -N -T Data/drivers/driver.ld $^ -o $@
	
userlinux: $(DISK_DIR)/tree_linux.elf

$(DISK_DIR)/tree_linux.elf:
	$(MAKE) -C userlinux/tree clean
	$(MAKE) -C userlinux/tree \
		CC="$(CC)" \
		CFLAGS="-O2 -Wall -m64 -nostdinc -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables $(LIBC_CFLAGS)" \
		LDFLAGS="-nostdlib -static -m64 -T $(CURDIR)/Data/drivers/driver.ld $(TEMP_DIR)/libc_start.o $(TEMP_DIR)/libaoslin.a"
	cp userlinux/tree/tree $(DISK_DIR)/tree_linux

$(TEMP_DIR)/aos_%.o: Data/aoslib/aos_%.c
	@mkdir -p $(dir $@)
	$(CC) $(LIB_CFLAGS) $(AOSLIB_CFLAGS) -c $< -o $@
	
$(TEMP_DIR)/libc_%.o: Data/aoslib/libc_%.c
	@mkdir -p $(dir $@)
	$(CC) $(LIB_CFLAGS) $(LIBC_CFLAGS) -c $< -o $@	

$(TEMP_DIR)/%.o: Data/drivers/%.c
	@mkdir -p $(dir $@)
	$(CC) $(DRV_CFLAGS) -c $< -o $@

$(TEMP_DIR)/%.o: Data/userspace/%.c
	@mkdir -p $(dir $@)
	$(CC) $(USR_CFLAGS) -c $< -o $@

$(TEMP_DIR)/%.o: Data/%.c
	@mkdir -p $(dir $@)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(TEMP_DIR)/%.o: Data/%.asm
	$(NASM) -f elf64 $< -o $@
	

clean:
	rm -rf $(TEMP_DIR)/* $(BUILD_DIR)/*