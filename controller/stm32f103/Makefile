.DEFAULT_GOAL := all
CROSS_COMPILE ?= arm-none-eabi-
CC := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy
SIZE := $(CROSS_COMPILE)size
OPENOCD ?= openocd
# This controller reports SW-DP ID 0x2ba01477; keep exact ID checking on.
# For a genuine STM32F103 board: make flash CPUTAPID=0x1ba01477
CPUTAPID ?= 0x2ba01477
BUILD := build
TARGET := $(BUILD)/controller
CPU := -mcpu=cortex-m3 -mthumb
CPPFLAGS := -ICore/Inc -ICommon
CFLAGS := $(CPU) -std=c99 -Os -g3 -Wall -Wextra -Werror -fno-builtin -ffunction-sections -fdata-sections -MMD -MP
SOURCES := $(wildcard Core/Src/*.c)
OBJECTS := $(addprefix $(BUILD)/,$(SOURCES:.c=.o)) $(BUILD)/GCC/startup_stm32f103c8.o
LDFLAGS := $(CPU) -nostdlib -TGCC/stm32f103c8.ld -Wl,--gc-sections,-Map=$(TARGET).map

.PHONY: all flash clean size
all: $(TARGET).elf $(TARGET).hex $(TARGET).bin size

$(TARGET).elf: $(OBJECTS) GCC/stm32f103c8.ld Makefile
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@
$(BUILD)/%.o: %.c Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/GCC/startup_stm32f103c8.o: GCC/startup_stm32f103c8.S Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CPU) -c $< -o $@
$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex $< $@
$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@
size: $(TARGET).elf
	$(SIZE) $<
flash: $(TARGET).elf
	$(OPENOCD) -f interface/stlink.cfg -c "set CPUTAPID $(CPUTAPID)" \
	 -f target/stm32f1x.cfg \
	 -c "adapter speed 1000; program $(TARGET).elf verify reset exit"
clean:
	rm -rf $(BUILD)

-include $(OBJECTS:.o=.d)
