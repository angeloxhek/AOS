CC      := gcc
LD      := ld
NASM    := nasm
OBJCOPY := objcopy

CFLAGS_KERNEL := -m64 -g3 -O0 -Wall -fno-omit-frame-pointer \
                 -mcmodel=kernel -mno-red-zone -ffreestanding \
                 -mgeneral-regs-only -fno-pic -fno-pie -fstack-protector
CFLAGS_USER   := -m64 -Wall -fno-omit-frame-pointer \
                 -ffreestanding -fno-pic -fno-pie -fno-asynchronous-unwind-tables
DEPFLAGS       = -MMD -MP -MF $(@:.o=.d)
LDFLAGS       := -m elf_x86_64 --no-warn-rwx-segments

SRCDIR  := Data
BUILDDIR:= Build
TEMPDIR := Temp
DRVDIR  := $(BUILDDIR)/FirstVolume/Drivers

KERNEL_MODS := pmm vmm sched ipc syscall ide elf console shm
KERNEL_MOD_OBJS := $(patsubst %,$(TEMPDIR)/%.o,$(KERNEL_MODS))

.PHONY: all clean dirs

all: dirs $(BUILDDIR)/pbr.bin $(BUILDDIR)/FirstVolume/AOSLDR.BIN \
     $(DRVDIR)/KBDDRIVER.ELF $(DRVDIR)/VFSDRIVER.ELF \
     $(BUILDDIR)/FirstVolume/tree.elf $(BUILDDIR)/FirstVolume/SHELL.ELF

dirs:
	@mkdir -p $(BUILDDIR)/Debug $(DRVDIR) $(TEMPDIR)

# PBR
$(BUILDDIR)/pbr.bin: $(SRCDIR)/pbr.asm | dirs
	$(NASM) -o $@ $<

# Kernel
$(TEMPDIR)/asmaosldr.o: $(SRCDIR)/aosldr.asm | dirs
	$(NASM) -f elf64 -o $@ $<

$(TEMPDIR)/caosldr.o: $(SRCDIR)/aosldr.c | dirs
	$(CC) $(CFLAGS_KERNEL) $(DEPFLAGS) -c $< -o $@

$(TEMPDIR)/%.o: $(SRCDIR)/%.c | dirs
	$(CC) $(CFLAGS_KERNEL) $(DEPFLAGS) -c $< -o $@

$(TEMPDIR)/aosldr.elf: $(TEMPDIR)/asmaosldr.o $(TEMPDIR)/caosldr.o $(KERNEL_MOD_OBJS) | dirs
	$(LD) $(LDFLAGS) -T $(SRCDIR)/aosldr.ld -Map $(TEMPDIR)/aosldr.map -o $@ $^

$(BUILDDIR)/FirstVolume/AOSLDR.BIN: $(TEMPDIR)/aosldr.elf | dirs
	$(OBJCOPY) -O binary -S -R .bss -R .note -R .comment -R .note.gnu.property $< $@

# AOSLIB
$(TEMPDIR)/syscalls.o: $(SRCDIR)/aoslib/syscalls.c | dirs
	$(CC) $(CFLAGS_USER) $(DEPFLAGS) -c $< -o $@
$(TEMPDIR)/filesystem.o: $(SRCDIR)/aoslib/filesystem.c | dirs
	$(CC) $(CFLAGS_USER) $(DEPFLAGS) -c $< -o $@
$(TEMPDIR)/string.o: $(SRCDIR)/aoslib/string.c | dirs
	$(CC) $(CFLAGS_USER) $(DEPFLAGS) -c $< -o $@
$(TEMPDIR)/start.o: $(SRCDIR)/aoslib/start.c | dirs
	$(CC) $(CFLAGS_USER) $(DEPFLAGS) -c $< -o $@

AOSLIB := $(TEMPDIR)/syscalls.o $(TEMPDIR)/string.o $(TEMPDIR)/filesystem.o $(TEMPDIR)/start.o

# Drivers
$(TEMPDIR)/kbddriver.o: $(SRCDIR)/drivers/kbddriver.c | dirs
	$(CC) $(CFLAGS_USER) $(DEPFLAGS) -c $< -o $@
$(DRVDIR)/KBDDRIVER.ELF: $(TEMPDIR)/kbddriver.o $(AOSLIB) | dirs
	$(LD) $(LDFLAGS) -N -T $(SRCDIR)/drivers/vfsdriver.ld $^ -o $@

$(TEMPDIR)/vfsdriver.o: $(SRCDIR)/drivers/vfsdriver.c | dirs
	$(CC) $(CFLAGS_USER) $(DEPFLAGS) -c $< -o $@
$(TEMPDIR)/fat32_vfsmodule.o: $(SRCDIR)/drivers/fat32_vfsmodule.c | dirs
	$(CC) $(CFLAGS_USER) $(DEPFLAGS) -c $< -o $@
$(DRVDIR)/VFSDRIVER.ELF: $(TEMPDIR)/vfsdriver.o $(TEMPDIR)/fat32_vfsmodule.o $(AOSLIB) | dirs
	$(LD) $(LDFLAGS) -N -T $(SRCDIR)/drivers/vfsdriver.ld $^ -o $@

# Userspace
$(TEMPDIR)/tree.o: $(SRCDIR)/userspace/tree.c | dirs
	$(CC) $(CFLAGS_USER) $(DEPFLAGS) -c $< -o $@
$(BUILDDIR)/FirstVolume/tree.elf: $(TEMPDIR)/tree.o $(AOSLIB) | dirs
	$(LD) $(LDFLAGS) -N -T $(SRCDIR)/drivers/vfsdriver.ld $^ -o $@

$(TEMPDIR)/shell.o: $(SRCDIR)/userspace/shell.c | dirs
	$(CC) $(CFLAGS_USER) $(DEPFLAGS) -c $< -o $@
$(BUILDDIR)/FirstVolume/SHELL.ELF: $(TEMPDIR)/shell.o $(AOSLIB) | dirs
	$(LD) $(LDFLAGS) -N -T $(SRCDIR)/drivers/vfsdriver.ld $^ -o $@

clean:
	rm -rf $(BUILDDIR) $(TEMPDIR)

-include $(wildcard $(TEMPDIR)/*.d)
