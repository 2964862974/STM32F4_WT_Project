/* Bootloader link script for STM32F407VETx
 *
 * Flash Layout:
 * 0x08000000 - 0x0800FFFF (64KB): Bootloader
 *
 * SRAM: 0x20000000 - 0x2002FFFF (192KB)
 *       0x10000000 - 0x1000FFFF (64KB CCM)
 */

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 64K
    RAM (xrw)   : ORIGIN = 0x20000000, LENGTH = 192K
    CCM (xrw)   : ORIGIN = 0x10000000, LENGTH = 64K
}

SECTIONS
{
    /* Code and read-only data */
    .text :
    {
        KEEP(*(.vectors))           /* Vector table must be first */
        *(.text)
        *(.text*)
        *(.rodata)
        *(.rodata*)
        *(.ehframe*)

        /* ARM.exidx should contain index entries for section references,
           and should appear before any C++ exception handling sections.  */
        *(.ARM.exidx*)
        *(.gnu.hash)
        *(.dynsym)
        *(.dynstr)
        *(.gnu.version)
        *(.gnu.version_r)

        . = ALIGN(8);
        PROVIDE(__etext = .);
        /* This is used by the startup in order to initialize the .data secion */
        PROVIDE(_etext = .);
        PROVIDE(_la_data = .);

    } >FLASH AT > FLASH

    /* Initialized data sections goes into RAM, load LMA copy after code */
    .data :
    {
        PROVIDE(__data_start__ = .);
        . = ALIGN(8);
        *(.data)
        *(.data*)

        . = ALIGN(8);
        /* All data end */
        __data_end__ = .;
        PROVIDE(__data_end__ = .);

    } >RAM AT > FLASH

    PROVIDE(_la_data = LOADADDR(.data));

    /* Uninitialized data section */
    . = ALIGN(4);
    .bss :
    {
        /* This is used by the startup in order to initialize the .bss secion */
        PROVIDE(__bss_start__ = .);
        *(.bss)
        *(.bss*)
        *(COMMON)

        . = ALIGN(4);
        /* This is used by the startup in order to initialize the .bss secion */
        PROVIDE(__bss_end__ = .);
        __bss_end__ = .;
    } >RAM

    /* Allocate space for stack */
    .stack_dummy (COPY):
    {
    } >RAM

    /* Remove information from the compiler libraries */
    /DISCARD/ :
    {
        libc.a ( * )
        libm.a ( * )
        libgcc.a ( * )
    }

    .ARM.attributes 0 : { *(.ARM.attributes) }
}
