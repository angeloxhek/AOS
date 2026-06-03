# data/drivers/make.mk

drivers: $(DRIVERS_DIR)/videodriver.elf $(DRIVERS_DIR)/wnddriver.elf $(DRIVERS_DIR)/inputdriver.elf

$(DRIVERS_DIR)/videodriver.elf: $(TEMP_DIR)/aos_start.o $(TEMP_DIR)/videodriver.o $(BUILD_DIR)/libs/libaos.a
	$(ECHO) "${YELLOW}[   LD    ]${NC} $@\n"
	$(Q)$(LD) $(LDFLAGS) -N -Map $(TEMP_DIR)/videodriver.map -T $(CURDIR)/data/driver.ld $^ -o $@

$(DRIVERS_DIR)/wnddriver.elf: $(TEMP_DIR)/aos_start.o $(TEMP_DIR)/wnddriver.o $(BUILD_DIR)/libs/libaos.a
	$(ECHO) "${YELLOW}[   LD    ]${NC} $@\n"
	$(Q)$(LD) $(LDFLAGS) -N -Map $(TEMP_DIR)/wnddriver.map -T $(CURDIR)/data/driver.ld $^ -o $@

$(DRIVERS_DIR)/inputdriver.elf: $(TEMP_DIR)/aos_start.o $(TEMP_DIR)/inputdriver.o $(BUILD_DIR)/libs/libaos.a
	$(ECHO) "${YELLOW}[   LD    ]${NC} $@\n"
	$(Q)$(LD) $(LDFLAGS) -N -Map $(TEMP_DIR)/inputdriver.map -T $(CURDIR)/data/driver.ld $^ -o $@

$(TEMP_DIR)/%.o: $(CURDIR)/data/drivers/%.c
	@$(MKDIR) -p $(dir $@)
	$(ECHO) "${CYAN}[   CC    ]${NC} $<\n"
	$(Q)$(CC) $(DRV_CFLAGS) -c $< -o $@