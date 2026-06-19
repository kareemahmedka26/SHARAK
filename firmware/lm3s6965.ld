/* Linker script for the TI Stellaris LM3S6965 (QEMU lm3s6965evb).
 * 256 KB FLASH at 0x00000000, 64 KB SRAM at 0x20000000.
 *
 * This script is the memory contract between the firmware image and the chip.
 * It does three jobs:
 *   1. Declares the physical memory regions (MEMORY block).
 *   2. Places each section in flash or RAM (SECTIONS block).
 *   3. Exports the boundary symbols (_sdata, _edata, _sbss, _ebss, _sidata,
 *      _estack) that startup.c reads to copy .data and zero .bss.
 *
 * Key concept used throughout: LMA vs VMA.
 *   - VMA (Virtual/run Memory Address): where a section is ADDRESSED at runtime.
 *   - LMA (Load Memory Address): where its bytes are STORED in the image.
 * For .text/.isr_vector these are the same (run from flash). For .data they
 * DIFFER: it runs in RAM (VMA) but is stored in flash (LMA), and startup.c
 * copies LMA->VMA at boot. `> RAM AT > FLASH` below is what creates that split.
 */

ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 256K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
}

/* Initial stack pointer = top of RAM. The Cortex-M3 stack is full-descending,
 * so it starts at the highest address and grows downward toward .bss/.data.
 * startup.c puts this value in vector-table entry 0; the core loads it on reset. */
_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
    /* Vector table FIRST in flash, i.e. at 0x00000000, because the Cortex-M3
     * fetches the initial SP and reset PC from there. KEEP() prevents
     * --gc-sections from garbage-collecting it (nothing "calls" the table). */
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    /* Code and read-only data live in flash and run from flash. */
    .text :
    {
        . = ALIGN(4);
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH

    /* Capture .data's LOAD address (its place in flash) so startup.c knows
     * where to copy FROM. Must be taken before the .data section is laid out. */
    _sidata = LOADADDR(.data);

    /* Initialized globals: addressed in RAM (VMA) but stored in flash (LMA).
     * `> RAM AT > FLASH` = "run here, but load the bytes there". _sdata/_edata
     * bracket the RAM range startup.c fills from _sidata. */
    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH

    /* Zero-initialized globals: occupy RAM only, no bytes in the image.
     * startup.c clears _sbss.._ebss to zero. *(COMMON) catches tentative
     * definitions the compiler emits into the common section. */
    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM
}
