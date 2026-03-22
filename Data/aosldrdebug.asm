[BITS 16]
[ORG 0x1000]        ; ВАЖНО: PBR загружает нас именно сюда!

; --- ЗАГОЛОВОК ДЛЯ PBR ---
jmp short start     ; 2 байта
nop                 ; 1 байт
db 'AOSLDR'         ; Сигнатура по смещению 3 (проверяется PBR)
; -------------------------

start:
    ; 1. Настраиваем сегменты
    ; PBR передал управление, но регистры могут быть "грязными"
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x1000  ; Стек растет вниз от нашего кода
    sti

    ; 2. Выводим сообщение
    mov si, MsgSuccess
    call print_string

    ; 3. Останавливаем процессор
    cli
    hlt
    jmp $

; --- Функция печати (BIOS) ---
print_string:
    mov ah, 0x0E
.loop:
    lodsb
    or al, al
    jz .done
    int 0x10
    jmp .loop
.done:
    ret

MsgSuccess db ' -> Success! Stage 2 is alive!', 0