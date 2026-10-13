ENTRY(_start)

SECTIONS
{
    . = 0x00400000;
	
    .driver_info : {
        KEEP(*(.driver_info))
    }

    . = ALIGN(0x1000);
    .text : {
        *(.text)
        *(.text.*)
    }

    . = ALIGN(0x1000);
    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    . = ALIGN(0x1000);
    .data : {
        *(.data)
        *(.data.*)
    }

    . = ALIGN(0x1000);
    .bss : {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }

    /DISCARD/ : {
        *(.note*)
        *(.comment)
    }
}