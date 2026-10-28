# data/initdrivers/make.mk

$(TEMP_DIR)/INITDRIVER.ELF: $(TEMP_DIR)/aos_start.o $(TEMP_DIR)/initdriver.o $(BUILD_DIR)/libs/libaos.a
	$(ECHO) "${YELLOW}[   LD    ]${NC} $@\n"
	$(Q)$(LD) $(LDFLAGS) -Map $(TEMP_DIR)/initdriver.map -T $(CURDIR)/data/driver.ld $^ -o $@-DEBUG
	$(ECHO) "${PURPLE}[ OBJCOPY ]${NC} Stripping debug info for $@\n"
	$(Q)$(OBJCOPY) --strip-debug $@-DEBUG $@
ifeq ($(DEBUG), 1)
	$(ECHO) "${BROWN}[   CP    ]${NC} $@-DEBUG ${GREEN}->${NC} ${DEBUG_DIR}/INITDRIVER.ELF\n"
	$(Q)$(CP) -r $@-DEBUG $(DEBUG_DIR)
endif

$(TEMP_DIR)/AUTHDRIVER.ELF: $(TEMP_DIR)/aos_start.o $(TEMP_DIR)/authdriver.o $(BUILD_DIR)/libs/libaos.a
	$(ECHO) "${YELLOW}[   LD    ]${NC} $@\n"
	$(Q)$(LD) $(LDFLAGS) -Map $(TEMP_DIR)/authdriver.map -T $(CURDIR)/data/driver.ld $^ -o $@-DEBUG
	$(ECHO) "${PURPLE}[ OBJCOPY ]${NC} Stripping debug info for $@\n"
	$(Q)$(OBJCOPY) --strip-debug $@-DEBUG $@
ifeq ($(DEBUG), 1)
	$(ECHO) "${BROWN}[   CP    ]${NC} $@-DEBUG ${GREEN}->${NC} ${DEBUG_DIR}/AUTHDRIVER.ELF\n"
	$(Q)$(CP) -r $@-DEBUG $(DEBUG_DIR)
endif

VFSDRIVER_OBJS = $(TEMP_DIR)/aos_start.o $(TEMP_DIR)/vfsdriver.o $(TEMP_DIR)/fat32_vfsmodule.o \
				 $(TEMP_DIR)/procfs_vfsmodule.o $(TEMP_DIR)/ide_diskmodule.o $(TEMP_DIR)/mbr_partmodule.o \
				 $(TEMP_DIR)/gpt_partmodule.o $(TEMP_DIR)/bsd_partmodule.o $(TEMP_DIR)/iso9660_partmodule.o \
				 $(TEMP_DIR)/ipcfs_vfsmodule.o $(BUILD_DIR)/libs/libaos.a

$(TEMP_DIR)/VFSDRIVER.ELF: $(VFSDRIVER_OBJS)
	$(ECHO) "${YELLOW}[   LD    ]${NC} $@\n"
	$(Q)$(LD) $(LDFLAGS) -Map $(TEMP_DIR)/vfsdriver.map -T $(CURDIR)/data/driver.ld $^ -o $@-DEBUG
	$(ECHO) "${PURPLE}[ OBJCOPY ]${NC} Stripping debug info for $@\n"
	$(Q)$(OBJCOPY) --strip-debug $@-DEBUG $@
ifeq ($(DEBUG), 1)
	$(ECHO) "${BROWN}[   CP    ]${NC} $@-DEBUG ${GREEN}->${NC} ${DEBUG_DIR}/VFSDRIVER.ELF\n"
	$(Q)$(CP) -r $@-DEBUG $(DEBUG_DIR)
endif
	
$(DISK_DIR)/INITRD.TAR: $(TEMP_DIR)/INITDRIVER.ELF $(TEMP_DIR)/AUTHDRIVER.ELF $(TEMP_DIR)/VFSDRIVER.ELF
	$(ECHO) "${PURPLE}[   TAR   ]${NC} $@\n"
	$(Q)$(TAR) -cf $@ -C $(TEMP_DIR) INITDRIVER.ELF AUTHDRIVER.ELF VFSDRIVER.ELF
	
$(TEMP_DIR)/%.o: $(CURDIR)/data/initdrivers/%.c
	@$(MKDIR) -p $(dir $@)
	$(ECHO) "${CYAN}[   CC    ]${NC} $<\n"
	$(Q)$(CC) $(DRV_CFLAGS) -c $< -o $@

$(TEMP_DIR)/%.o: $(CURDIR)/data/initdrivers/vfs/%.c
	@$(MKDIR) -p $(dir $@)
	$(ECHO) "${CYAN}[   CC    ]${NC} $<\n"
	$(Q)$(CC) $(DRV_CFLAGS) $(KERNEL_INC) -c $< -o $@