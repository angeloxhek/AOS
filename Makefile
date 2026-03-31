# AOS Build System
# Replaces aosbuild.sh with proper Make dependency tracking.

# ── Tools ────────────────────────────────────────────────────────────
CC      := gcc
LD      := ld
NASM    := nasm
OBJCOPY := objcopy

# ── Flags ────────────────────────────────────────────────────────────
CFLAGS_KERNEL := -m64 -g3 -O0 -Wall -fno-omit-frame-pointer \
                 -mcmodel=kernel -mno-red-zone -ffreestanding \
                 -mgeneral-regs-only -fno-pic -fno-pie -fstack-protector

CFLAGS_USER   := -m64 -Wall -fno-omit-frame-pointer \
                 -ffreestanding -fno-pic -fno-pie \
                 -fno-asynchronous-unwind-tables

DEPFLAGS       = -MMD -MP -MF $(@:.o=.d)

LDFLAGS       := -m elf_x86_64 --no-warn-rwx-segments

# ── Directories ──────────────────────────────────────────────────────
SRCDIR   := Data
BUILDDIR := Build
TEMPDIR  := Temp
DEBUGDIR := $(BUILDDIR)/Debug
VOLDIR   := $(BUILDDIR)/FirstVolume
DRVDIR   := $(VOLDIR)/Drivers

# ── Output files ─────────────────────────────────────────────────────
PBR_BIN          := $(BUILDDIR)/pbr.bin
PBRSCAN_BIN      := $(DEBUGDIR)/pbrscan.bin
AOSLDR_BIN       := $(VOLDIR)/AOSLDR.BIN
KBDDRIVER_ELF    := $(DRVDIR)/KBDDRIVER.ELF
VFSDRIVER_ELF    := $(DRVDIR)/VFSDRIVER.ELF
TREE_ELF         := $(VOLDIR)/tree.elf

# ── Intermediate objects ─────────────────────────────────────────────
# Kernel / loader
AOSLDR_ASM_OBJ := $(TEMPDIR)/asmaosldr.o
AOSLDR_C_OBJ   := $(TEMPDIR)/caosldr.o
AOSLDR_ELF     := $(TEMPDIR)/aosldr.elf
AOSLDR_MAP     := $(TEMPDIR)/aosldr.map

# aoslib
AOSLIB_SRCS := $(SRCDIR)/aoslib/syscalls.c \
               $(SRCDIR)/aoslib/filesystem.c \
               $(SRCDIR)/aoslib/string.c \
               $(SRCDIR)/aoslib/start.c
AOSLIB_OBJS := $(TEMPDIR)/syscalls.o \
               $(TEMPDIR)/filesystem.o \
               $(TEMPDIR)/string.o \
               $(TEMPDIR)/start.o

# Drivers
KBDDRIVER_OBJ    := $(TEMPDIR)/kbddriver.o
VFSDRIVER_OBJ    := $(TEMPDIR)/vfsdriver.o
FAT32_VFS_OBJ   := $(TEMPDIR)/fat32_vfsmodule.o

# Userspace
TREE_OBJ := $(TEMPDIR)/tree.o

# Linker scripts
KERNEL_LD  := $(SRCDIR)/aosldr.ld
DRIVER_LD  := $(SRCDIR)/drivers/vfsdriver.ld

# Collect all dependency files
ALL_DEPS := $(wildcard $(TEMPDIR)/*.d)

# ── Phony targets ────────────────────────────────────────────────────
.PHONY: all clean pbr kernel aoslib drivers userspace dirs

# ── Default target ───────────────────────────────────────────────────
all: dirs pbr kernel drivers userspace

# ── Directory creation ───────────────────────────────────────────────
dirs:
	@mkdir -p $(DEBUGDIR) $(DRVDIR) $(TEMPDIR)

# ══════════════════════════════════════════════════════════════════════
#  PBR & debug tools
# ══════════════════════════════════════════════════════════════════════
pbr: dirs $(PBR_BIN) $(PBRSCAN_BIN)

$(PBR_BIN): $(SRCDIR)/pbr.asm | dirs
	$(NASM) -o $@ $<

$(PBRSCAN_BIN): $(SRCDIR)/fat32scan.asm | dirs
	$(NASM) -o $@ $<

# ══════════════════════════════════════════════════════════════════════
#  Kernel (AOSLDR)
# ══════════════════════════════════════════════════════════════════════
kernel: dirs $(AOSLDR_BIN)

# Assembly object
$(AOSLDR_ASM_OBJ): $(SRCDIR)/aosldr.asm | dirs
	$(NASM) -f elf64 -o $@ $<

# C object (kernel flags)
$(AOSLDR_C_OBJ): $(SRCDIR)/aosldr.c | dirs
	$(CC) $(CFLAGS_KERNEL) $(DEPFLAGS) -c $< -o $@

# Link ELF
$(AOSLDR_ELF): $(AOSLDR_ASM_OBJ) $(AOSLDR_C_OBJ) $(KERNEL_LD) | dirs
	$(LD) $(LDFLAGS) -T $(KERNEL_LD) -Map $(AOSLDR_MAP) -o $@ \
		$(AOSLDR_ASM_OBJ) $(AOSLDR_C_OBJ)

# Extract flat binary
$(AOSLDR_BIN): $(AOSLDR_ELF) | dirs
	$(OBJCOPY) -O binary -S -R .bss -R .note -R .comment -R .note.gnu.property $< $@

# ══════════════════════════════════════════════════════════════════════
#  AOSLIB (shared user-space library objects)
# ══════════════════════════════════════════════════════════════════════
aoslib: dirs $(AOSLIB_OBJS)

$(TEMPDIR)/syscalls.o: $(SRCDIR)/aoslib/syscalls.c | dirs
	$(CC) $(CFLAGS_USER) $(DEPFLAGS) -c $< -o $@

$(TEMPDIR)/filesystem.o: $(SRCDIR)/aoslib/filesystem.c | dirs
	$(CC) $(CFLAGS_USER) $(DEPFLAGS) -c $< -o $@

$(TEMPDIR)/string.o: $(SRCDIR)/aoslib/string.c | dirs
	$(CC) $(CFLAGS_USER) $(DEPFLAGS) -c $< -o $@

$(TEMPDIR)/start.o: $(SRCDIR)/aoslib/start.c | dirs
	$(CC) $(CFLAGS_USER) $(DEPFLAGS) -c $< -o $@

# ══════════════════════════════════════════════════════════════════════
#  Drivers
# ══════════════════════════════════════════════════════════════════════
drivers: dirs aoslib $(KBDDRIVER_ELF) $(VFSDRIVER_ELF)

# -- kbddriver --
$(KBDDRIVER_OBJ): $(SRCDIR)/drivers/kbddriver.c | dirs
	$(CC) $(CFLAGS_USER) $(DEPFLAGS) -c $< -o $@

$(KBDDRIVER_ELF): $(KBDDRIVER_OBJ) $(TEMPDIR)/syscalls.o $(TEMPDIR)/string.o $(TEMPDIR)/start.o $(DRIVER_LD) | dirs
	$(LD) $(LDFLAGS) -N -Map $(TEMPDIR)/kbddriver.map -T $(DRIVER_LD) \
		$(KBDDRIVER_OBJ) $(TEMPDIR)/syscalls.o $(TEMPDIR)/string.o $(TEMPDIR)/start.o \
		-o $@

# -- vfsdriver --
$(VFSDRIVER_OBJ): $(SRCDIR)/drivers/vfsdriver.c | dirs
	$(CC) $(CFLAGS_USER) $(DEPFLAGS) -c $< -o $@

$(FAT32_VFS_OBJ): $(SRCDIR)/drivers/fat32_vfsmodule.c | dirs
	$(CC) $(CFLAGS_USER) $(DEPFLAGS) -c $< -o $@

$(VFSDRIVER_ELF): $(VFSDRIVER_OBJ) $(TEMPDIR)/syscalls.o $(TEMPDIR)/string.o $(TEMPDIR)/filesystem.o $(FAT32_VFS_OBJ) $(TEMPDIR)/start.o $(DRIVER_LD) | dirs
	$(LD) $(LDFLAGS) -N -Map $(TEMPDIR)/vfsdriver.map -T $(DRIVER_LD) \
		$(VFSDRIVER_OBJ) $(TEMPDIR)/syscalls.o $(TEMPDIR)/string.o \
		$(TEMPDIR)/filesystem.o $(FAT32_VFS_OBJ) $(TEMPDIR)/start.o \
		-o $@

# ══════════════════════════════════════════════════════════════════════
#  User-Space programs
# ══════════════════════════════════════════════════════════════════════
userspace: dirs aoslib $(TREE_ELF)

$(TREE_OBJ): $(SRCDIR)/userspace/tree.c | dirs
	$(CC) $(CFLAGS_USER) $(DEPFLAGS) -c $< -o $@

$(TREE_ELF): $(TREE_OBJ) $(TEMPDIR)/syscalls.o $(TEMPDIR)/string.o $(TEMPDIR)/filesystem.o $(TEMPDIR)/start.o $(DRIVER_LD) | dirs
	$(LD) $(LDFLAGS) -N -Map $(TEMPDIR)/tree.map -T $(DRIVER_LD) \
		$(TREE_OBJ) $(TEMPDIR)/syscalls.o $(TEMPDIR)/string.o \
		$(TEMPDIR)/filesystem.o $(TEMPDIR)/start.o \
		-o $@

# ── Clean ────────────────────────────────────────────────────────────
clean:
	rm -rf $(BUILDDIR) $(TEMPDIR)

# ── Include auto-generated dependencies ──────────────────────────────
-include $(ALL_DEPS)
