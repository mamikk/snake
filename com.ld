/* https://stackoverflow.com/questions/34997577/linker-script-allocation-of-bss-section */
OUTPUT_FORMAT(binary)
SECTIONS
{
    . = 0x0100;
    .text :
    {
        *(.text*);
    }
    .data :
    {
        *(.data);
        *(.rodata*);
        *(COMMON)
    }
    .bss :
    {
        *(.bss);
    }
    /*_heap = ALIGN(4);*/

    /DISCARD/ : { *(.comment); }
}
