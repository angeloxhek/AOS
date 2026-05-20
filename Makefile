CC = gcc
NASM = nasm
LD = ld
AR = ar
OBJCOPY = objcopy
CP = cp
MKDIR = mkdir
RM = rm

ifeq ($(V),1)
    Q :=
    ECHO := @true
else
    Q := @
    ECHO := @printf
endif

CYAN   := \033[0;36m
YELLOW := \033[1;33m
GREEN  := \033[0;32m
RED    := \033[1;31m
PURPLE := \033[0;35m
LCYAN  := \033[1;36m
DRED   := \033[0;31m
BROWN  := \033[0;33m
GRAY   := \033[0;37m
NC     := \033[0m

KERNEL_CFLAGS = -m64 -g3 -O0 -Wall -fno-omit-frame-pointer -mcmodel=kernel -mno-red-zone \
				-ffreestanding -mgeneral-regs-only -fno-pic -fno-pie -fstack-protector
AOSLIB_CFLAGS = -I $(CURDIR)/data/include
LIBC_CFLAGS = -I $(CURDIR)/aosliblin/include
LIB_CFLAGS = -m64 -fno-omit-frame-pointer -ffreestanding -fno-pic -fno-pie -fno-asynchronous-unwind-tables -nostdinc
DRV_CFLAGS = -m64 -fno-omit-frame-pointer -ffreestanding -fno-pic -fno-pie -fno-asynchronous-unwind-tables
USR_CFLAGS = -m64 -fno-omit-frame-pointer -ffreestanding -fno-pic -fno-pie -fno-asynchronous-unwind-tables
LDFLAGS = -m elf_x86_64 --no-warn-rwx-segments

BUILD_DIR = $(CURDIR)/build
TEMP_DIR = $(CURDIR)/temp
DISK_DIR = $(BUILD_DIR)/volume
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
				 $(TEMP_DIR)/libc_sys_stat.o $(TEMP_DIR)/libc_pwd.o $(TEMP_DIR)/libc_grp.o
			  
.PHONY: all clean kernel drivers configs userspace userlinux

all: prepare kernel libs drivers configs userspace userlinux
	@echo "Build Successful!"
	
prepare:
	$(ECHO) "${RED}[  MKDIR  ]${NC} ${DRIVERS_DIR}\n"
	$(Q)$(MKDIR) -p $(DRIVERS_DIR)
	$(ECHO) "${RED}[  MKDIR  ]${NC} ${DISK_DIR}/configs\n"
	$(Q)$(MKDIR) -p $(DISK_DIR)/configs
	$(ECHO) "${RED}[  MKDIR  ]${NC} ${BUILD_DIR}/libs\n"
	$(Q)$(MKDIR) -p $(BUILD_DIR)/libs
	$(ECHO) "${RED}[  MKDIR  ]${NC} ${TEMP_DIR}\n"
	$(Q)$(MKDIR) -p $(TEMP_DIR)
	
kernel: $(BUILD_DIR)/pbr.bin $(BUILD_DIR)/volume/AOSLDR.BIN

$(BUILD_DIR)/pbr.bin: $(CURDIR)/data/pbr.asm
	$(ECHO) "${GREEN}[   AS    ]${NC} $<\n"
	$(Q)$(NASM) $< -o $@

$(DISK_DIR)/AOSLDR.BIN: $(KERNEL_OBJS)
	$(ECHO) "${YELLOW}[   LD    ]${NC} $@\n"
	$(Q)$(LD) $(LDFLAGS) -T $(CURDIR)/data/aosldr.ld -Map $(TEMP_DIR)/aosldr.map -o $(TEMP_DIR)/aosldr.elf $(KERNEL_OBJS)
	$(ECHO) "${PURPLE}[ OBJCOPY ]${NC} $@\n"
	$(Q)$(OBJCOPY) -O binary -S -R .bss -R .note -R .comment -R .note.gnu.property $(TEMP_DIR)/aosldr.elf $@
	
libs: $(BUILD_DIR)/libs/libaos.a $(BUILD_DIR)/libs/libaoslin.a $(TEMP_DIR)/aos_start.o $(TEMP_DIR)/libc_start.o

$(BUILD_DIR)/libs/libaos.a: $(AOSLIB_OBJS)
	$(ECHO) "${LCYAN}[   AR    ]${NC} $@\n"
	$(Q)$(AR) rcs $@ $^

$(BUILD_DIR)/libs/libaoslin.a: $(AOSLIBLIN_OBJS)
	$(ECHO) "${LCYAN}[   AR    ]${NC} $@\n"
	$(Q)$(AR) rcs $@ $^
	
drivers: $(DRIVERS_DIR)/INITDRIVER.ELF $(DRIVERS_DIR)/AUTHDRIVER.ELF $(DRIVERS_DIR)/VFSDRIVER.ELF

$(DRIVERS_DIR)/INITDRIVER.ELF: $(TEMP_DIR)/aos_start.o $(TEMP_DIR)/initdriver.o $(BUILD_DIR)/libs/libaos.a
	$(ECHO) "${YELLOW}[   LD    ]${NC} $@\n"
	$(Q)$(LD) $(LDFLAGS) -N -Map $(TEMP_DIR)/initdriver.map -T $(CURDIR)/data/drivers/driver.ld $^ -o $@

$(DRIVERS_DIR)/AUTHDRIVER.ELF: $(TEMP_DIR)/aos_start.o $(TEMP_DIR)/authdriver.o $(BUILD_DIR)/libs/libaos.a
	$(ECHO) "${YELLOW}[   LD    ]${NC} $@\n"
	$(Q)$(LD) $(LDFLAGS) -N -Map $(TEMP_DIR)/authdriver.map -T $(CURDIR)/data/drivers/driver.ld $^ -o $@
	
$(DRIVERS_DIR)/VFSDRIVER.ELF: $(TEMP_DIR)/aos_start.o $(TEMP_DIR)/vfsdriver.o $(TEMP_DIR)/fat32_vfsmodule.o $(TEMP_DIR)/procfs_vfsmodule.o $(BUILD_DIR)/libs/libaos.a
	$(ECHO) "${YELLOW}[   LD    ]${NC} $@\n"
	$(Q)$(LD) $(LDFLAGS) -N -Map $(TEMP_DIR)/vfsdriver.map -T $(CURDIR)/data/drivers/driver.ld $^ -o $@
	
configs:
	$(ECHO) "${BROWN}[   CP    ]${NC} ${CURDIR}/configs ${GREEN}->${NC} ${DISK_DIR}/configs\n"
	$(Q)$(CP) -r $(CURDIR)/configs $(DISK_DIR)

userspace: $(DISK_DIR)/tree.elf

$(DISK_DIR)/tree.elf: $(TEMP_DIR)/aos_start.o $(TEMP_DIR)/tree.o $(BUILD_DIR)/libs/libaos.a
	$(ECHO) "${YELLOW}[   LD    ]${NC} $@\n"
	$(Q)$(LD) $(LDFLAGS) -N -T $(CURDIR)/data/drivers/driver.ld $^ -o $@
	
userlinux: $(DISK_DIR)/tree_linux.elf

$(DISK_DIR)/tree_linux.elf:
	$(ECHO) "${GRAY}[  MAKE   ]${NC} ${CYAN}${CURDIR}/userlinux/tree${NC} clean\n"
	$(Q)$(MAKE) -s -C $(CURDIR)/userlinux/tree clean
	$(ECHO) "${GRAY}[  MAKE   ]${NC} ${CYAN}${CURDIR}/userlinux/tree${NC}\n"
	$(Q)$(MAKE) -s -C $(CURDIR)/userlinux/tree \
		CC="$(CC)" \
		CFLAGS="-O2 -Wall -m64 -nostdinc -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables $(LIBC_CFLAGS)" \
		LDFLAGS="-nostdlib -static -m64 -T $(CURDIR)/data/drivers/driver.ld $(TEMP_DIR)/libc_start.o $(TEMP_DIR)/libaoslin.a"
	$(ECHO) "${BROWN}[   CP    ]${NC} ${CURDIR}/userlinux/tree/tree ${GREEN}->${NC} ${DISK_DIR}/tree_linux\n"
	$(Q)$(CP) $(CURDIR)/userlinux/tree/tree $(DISK_DIR)/tree_linux

$(TEMP_DIR)/aos_%.o: $(CURDIR)/data/aoslib/aos_%.c
	@$(MKDIR) -p $(dir $@)
	$(ECHO) "${CYAN}[   CC    ]${NC} $<\n"
	$(Q)$(CC) $(LIB_CFLAGS) $(AOSLIB_CFLAGS) -c $< -o $@
	
$(TEMP_DIR)/libc_%.o: $(CURDIR)/data/aoslib/libc_%.c
	@$(MKDIR) -p $(dir $@)
	$(ECHO) "${CYAN}[   CC    ]${NC} $<\n"
	$(Q)$(CC) $(LIB_CFLAGS) $(LIBC_CFLAGS) -c $< -o $@	

$(TEMP_DIR)/%.o: $(CURDIR)/data/drivers/%.c
	@$(MKDIR) -p $(dir $@)
	$(ECHO) "${CYAN}[   CC    ]${NC} $<\n"
	$(Q)$(CC) $(DRV_CFLAGS) -c $< -o $@

$(TEMP_DIR)/%.o: $(CURDIR)/data/userspace/%.c
	@$(MKDIR) -p $(dir $@)
	$(ECHO) "${CYAN}[   CC    ]${NC} $<\n"
	$(Q)$(CC) $(USR_CFLAGS) -c $< -o $@

$(TEMP_DIR)/%.o: $(CURDIR)/data/%.c
	@$(MKDIR) -p $(dir $@)
	$(ECHO) "${CYAN}[   CC    ]${NC} $<\n"
	$(Q)$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(TEMP_DIR)/%.o: $(CURDIR)/data/%.asm
	$(ECHO) "${GREEN}[   AS    ]${NC} $<\n"
	$(Q)$(NASM) -f elf64 $< -o $@
	
clean:
	$(ECHO) "${DRED}[   RM    ]${NC} ${TEMP_DIR}/*\n"
	$(Q)$(RM) -rf $(TEMP_DIR)/*
	$(ECHO) "${DRED}[   RM    ]${NC} ${BUILD_DIR}/*\n"
	$(Q)$(RM) -rf $(BUILD_DIR)/*