# Run from the repository root with: make -C software -j
.DEFAULT_GOAL := all
CROSS_COMPILE ?= arm-none-eabi-
CC := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy
SIZE := $(CROSS_COMPILE)size
SLOT ?= A
BUILD := build/$(SLOT)
LINKER := GCC/stm32f411$(SLOT).ld
TARGET := $(BUILD)/quad
CPU := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
CPPFLAGS := -DUSE_HAL_DRIVER -DSTM32F411xE  -ICore/Inc -ICommon -IOTA -IDrivers/STM32F4xx_HAL_Driver/Inc  -IDrivers/CMSIS/Device/ST/STM32F4xx/Include -IDrivers/CMSIS/Include
CFLAGS := $(CPU) -std=c99 -Os -g3 -Wall -Wextra  -ffunction-sections -fdata-sections -MMD -MP
HAL := Drivers/STM32F4xx_HAL_Driver/Src
HAL_MODULES := adc adc_ex i2c uart rcc rcc_ex flash flash_ex flash_ramfunc  gpio dma_ex dma pwr pwr_ex cortex exti
SOURCES := $(wildcard Core/Src/*.c) GCC/runtime.c OTA/ota_store.c $(HAL)/stm32f4xx_hal.c  $(addprefix $(HAL)/stm32f4xx_hal_,$(addsuffix .c,$(HAL_MODULES)))
OBJECTS := $(addprefix $(BUILD)/,$(SOURCES:.c=.o)) $(BUILD)/GCC/startup_stm32f411xe.o
LDFLAGS := $(CPU) -nostartfiles --specs=nano.specs --specs=nosys.specs  -T$(LINKER) -Wl,--gc-sections,-Map=$(TARGET).map
LDLIBS := -Wl,--start-group -lc -lm -lnosys -Wl,--end-group

.PHONY: all clean size flash
all: $(TARGET).elf $(TARGET).hex $(TARGET).bin size

$(TARGET).elf: $(OBJECTS) $(LINKER) Makefile
	$(CC) $(LDFLAGS) $(OBJECTS) $(LDLIBS) -o $@

$(BUILD)/%.o: %.c Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/Drivers/%.o: CFLAGS += -Wno-unused-parameter

$(BUILD)/GCC/startup_stm32f411xe.o: GCC/startup_stm32f411xe.S Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CPU) -g3 -c $< -o $@

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex $< $@
$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

size: $(TARGET).elf
	$(SIZE) $<

# Explicitly requested by the operator; not part of the default build.
OPENOCD ?= openocd
flash:
	@echo "A/B OTA requires first provisioning boot + metadata + app. See docs/OTA.md."
	@exit 1

clean:
	rm -rf $(BUILD)

-include $(OBJECTS:.o=.d)

.PHONY: banks boot
banks:
	$(MAKE) SLOT=A all
	$(MAKE) SLOT=B all
boot:
	$(MAKE) -f Boot/Makefile all
