drivers: $(DRIVERS_DIR)/videodriver.elf

$(DRIVERS_DIR)/videodriver.elf: $(TEMP_DIR)/aos_start.o $(TEMP_DIR)/videodriver.o $(BUILD_DIR)/libs/libaos.a
	$(ECHO) "${YELLOW}[   LD    ]${NC} $@\n"
	$(Q)$(LD) $(LDFLAGS) -N -Map $(TEMP_DIR)/initdriver.map -T $(CURDIR)/data/driver.ld $^ -o $@

$(TEMP_DIR)/%.o: $(CURDIR)/data/drivers/%.c
	@$(MKDIR) -p $(dir $@)
	$(ECHO) "${CYAN}[   CC    ]${NC} $<\n"
	$(Q)$(CC) $(DRV_CFLAGS) -c $< -o $@