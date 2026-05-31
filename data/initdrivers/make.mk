$(TEMP_DIR)/INITDRIVER.ELF: $(TEMP_DIR)/aos_start.o $(TEMP_DIR)/initdriver.o $(BUILD_DIR)/libs/libaos.a
	$(ECHO) "${YELLOW}[   LD    ]${NC} $@\n"
	$(Q)$(LD) $(LDFLAGS) -N -Map $(TEMP_DIR)/initdriver.map -T $(CURDIR)/data/driver.ld $^ -o $@

$(TEMP_DIR)/AUTHDRIVER.ELF: $(TEMP_DIR)/aos_start.o $(TEMP_DIR)/authdriver.o $(BUILD_DIR)/libs/libaos.a
	$(ECHO) "${YELLOW}[   LD    ]${NC} $@\n"
	$(Q)$(LD) $(LDFLAGS) -N -Map $(TEMP_DIR)/authdriver.map -T $(CURDIR)/data/driver.ld $^ -o $@

VFSDRIVER_OBJS = $(TEMP_DIR)/aos_start.o $(TEMP_DIR)/vfsdriver.o $(TEMP_DIR)/fat32_vfsmodule.o \
				 $(TEMP_DIR)/procfs_vfsmodule.o $(TEMP_DIR)/ide_diskmodule.o $(TEMP_DIR)/mbr_partmodule.o \
				 $(TEMP_DIR)/gpt_partmodule.o $(TEMP_DIR)/bsd_partmodule.o $(TEMP_DIR)/iso9660_partmodule.o $(BUILD_DIR)/libs/libaos.a

$(TEMP_DIR)/VFSDRIVER.ELF: $(VFSDRIVER_OBJS)
	$(ECHO) "${YELLOW}[   LD    ]${NC} $@\n"
	$(Q)$(LD) $(LDFLAGS) -N -Map $(TEMP_DIR)/vfsdriver.map -T $(CURDIR)/data/driver.ld $^ -o $@
	
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
	$(Q)$(CC) $(DRV_CFLAGS) -c $< -o $@