ENTRY(_start) /* Указываем точку входа */

SECTIONS
{
    /* Например, загружаем драйвер по адресу 0x00400000 */
    . = 0x00400000;

    .text : {
        *(.text)
    }

    .data : {
        *(.data)
    }

    .rodata : {
        *(.rodata)
    }

    .bss : {
        *(.bss)
    }
}