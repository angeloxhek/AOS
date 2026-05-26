ENTRY(start)

KERNEL_VMA = 0xFFFFFFFF80010000;
KERNEL_LMA = 0x10000;

OFFSET = KERNEL_VMA - KERNEL_LMA;

SECTIONS
{
    . = KERNEL_VMA;

    .text : AT(ADDR(.text) - OFFSET) ALIGN(4096)
    {
        *(.text.boot)
        *(.text)
        *(.text.*)
    }

    .rodata : AT(ADDR(.rodata) - OFFSET)
    {
        *(.rodata .rodata.*)
    }

    .data : AT(ADDR(.data) - OFFSET)
    {
        *(.data .data.*)
    }

    .bss : AT(ADDR(.bss) - OFFSET)
    {
        *(.bss)
    }
    
    /DISCARD/ : {
        *(.note.GNU-stack)
		*(.note.gnu.property)
        *(.comment)
		*(.eh_frame)
		*(.eh_frame_hdr)
        *(.note .note.*)
    }
}