ENTRY(start)

/* Виртуальный базовый адрес (стандарт для 64-бит ядер: -2GB) */
KERNEL_VMA = 0xFFFFFFFF80010000;
/* Физический адрес загрузки */
KERNEL_LMA = 0x10000;

OFFSET = KERNEL_VMA - KERNEL_LMA;

SECTIONS
{
    /* Сдвигаем счетчик адресов "." на высокий адрес */
    . = KERNEL_VMA;

    /* Но говорим AT(...), чтобы физически данные шли с 0x10000 */
    .text : AT(ADDR(.text) - OFFSET) ALIGN(4096)
    {
        /* Важно: код bootstrap должен быть в начале, линкер может перетасовать секции */
        *(.text .text.*)
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