/* STM32F411CE: 512 KiB flash, 128 KiB SRAM. Standalone image, no bootloader. */
ENTRY(Reset_Handler)
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
    RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 128K
}
_estack = ORIGIN(RAM) + LENGTH(RAM);
_stack_size = 4K;
_heap_size = 2K;

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(512);
        KEEP(*(.isr_vector))
    } > FLASH
    .text :
    {
        *(.text .text.*)
        *(.rodata .rodata.*)
        KEEP(*(.init))
        KEEP(*(.fini))
        . = ALIGN(4);
    } > FLASH
    .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FLASH
    .ARM.exidx :
    {
        __exidx_start = .;
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        __exidx_end = .;
    } > FLASH
    .preinit_array :
    {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > FLASH
    .init_array :
    {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > FLASH
    .fini_array :
    {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } > FLASH
    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data .data.*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH
    _sidata = LOADADDR(.data);
    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM
    .noinit (NOLOAD) :
    {
        . = ALIGN(8);
        *(.noinit .noinit.*)
    } > RAM
    .heap (NOLOAD) :
    {
        . = ALIGN(8);
        _end = .;
        end = .;
        _heap_start = .;
        . += _heap_size;
        _heap_end = .;
    } > RAM
    .stack _estack - _stack_size (NOLOAD) :
    {
        _stack_limit = .;
        . += _stack_size;
    } > RAM
    ASSERT(_heap_end <= _stack_limit, "SRAM overflow: heap overlaps stack")
    /DISCARD/ : { *(.note.GNU-stack) }
}
