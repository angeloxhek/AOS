# data/drivers/make.mk

drivers: $(DRIVERS_DIR)/videodriver.elf $(DRIVERS_DIR)/wnddriver.elf $(DRIVERS_DIR)/inputdriver.elf $(DRIVERS_DIR)/pcidriver.elf

$(DRIVERS_DIR)/%.elf: $(TEMP_DIR)/aos_start.o $(TEMP_DIR)/%.o $(BUILD_DIR)/libs/libaos.a
	$(ECHO) "${YELLOW}[   LD    ]${NC} $@\n"
	$(Q)$(LD) $(LDFLAGS) -Map $(TEMP_DIR)/$*.map -T $(CURDIR)/data/driver.ld $^ -o $(TEMP_DIR)/$*.elf
	$(ECHO) "${PURPLE}[ OBJCOPY ]${NC} Stripping debug info for $@\n"
	$(Q)$(OBJCOPY) --strip-debug $(TEMP_DIR)/$*.elf $@
ifeq ($(DEBUG), 1)
	$(ECHO) "${BROWN}[   CP    ]${NC} ${TEMP_DIR}/$*.elf ${GREEN}->${NC} ${DEBUG_DIR}/$*.elf\n"
	$(Q)$(CP) -r $(TEMP_DIR)/$*.elf $(DEBUG_DIR)
endif

$(TEMP_DIR)/%.o: $(CURDIR)/data/drivers/%.c
	@$(MKDIR) -p $(dir $@)
	$(ECHO) "${CYAN}[   CC    ]${NC} $<\n"
	$(Q)$(CC) $(DRV_CFLAGS) -c $< -o $@