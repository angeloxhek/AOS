# data/arch/x86_64/arch.mk

CROSS_COMPILE ?= 

ARCH_CFLAGS = -m64 -mno-red-zone -mgeneral-regs-only
ARCH_LDFLAGS = -m elf_x86_64

ARCH_KERNEL_CFLAGS = -mcmodel=kernel

ARCH_USER_CFLAGS = -mcmodel=small -mstack-protector-guard=tls -mstack-protector-guard-reg=fs -mstack-protector-guard-offset=0x28

ARCH_OBJS = $(TEMP_DIR)/hal_boot.o $(TEMP_DIR)/hal_asm.o $(TEMP_DIR)/hal_idt.o \
			$(TEMP_DIR)/hal_gdt.o $(TEMP_DIR)/hal_pmm.o $(TEMP_DIR)/hal_vmm.o \
			$(TEMP_DIR)/hal_sched.o $(TEMP_DIR)/hal_syscall.o
			
AOSLIB_ARCH_OBJS = $(TEMP_DIR)/hal_asmlib.o

ARCH_EXTRA_TARGETS = $(BUILD_DIR)/pbr.bin $(DISK_DIR)/PBRLDR.BIN

$(BUILD_DIR)/pbr.bin: $(CURDIR)/data/arch/$(ARCH)/hal_pbr.asm
	$(ECHO) "${GREEN}[   AS    ]${NC} $<\n"
	$(Q)$(NASM) $< -o $@

$(DISK_DIR)/PBRLDR.BIN: $(CURDIR)/data/arch/$(ARCH)/hal_pbrldr.asm
	$(ECHO) "${GREEN}[   AS    ]${NC} $<\n"
	$(Q)$(NASM) $< -o $@
	
$(TEMP_DIR)/hal_boot.o: $(CURDIR)/data/arch/$(ARCH)/hal_boot.asm
	$(ECHO) "${GREEN}[   AS    ]${NC} $<\n"
	$(Q)$(NASM) -f elf64 -w-number-overflow $< -o $@