[BITS 16]
[ORG 0x7C00]

jmp short start
nop

; --- BPB (BIOS Parameter Block) ---
OEMName         db 'MSWIN4.1'
BytesPerSec     dw 512
SecPerClust     db 0    ; Будет заполнено BOOTICE
RsvdSecCnt      dw 0
NumFATs         db 0
RootEntCnt      dw 0
TotSec16        dw 0
Media           db 0
FATSz16         dw 0
SecPerTrk       dw 0
NumHeads        dw 0
HiddSec         dd 0
TotSec32        dd 0
FATSz32         dd 0
ExtFlags        dw 0
FSVer           dw 0
RootClus        dd 0
FSInfo          dw 0
BkBootSec       dw 0
Reserved        times 12 db 0
DrvNum          db 0    ; <--- МЕТКА ОПРЕДЕЛЕНА ЗДЕСЬ
Reserved1       db 0
BootSig         db 0x29
VolID           dd 0
VolLab          db 'NO NAME    '
FilSysType      db 'FAT32   '

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    
    ; Сохраняем номер диска в переменную внутри BPB
    mov [DrvNum], dl

    ; --- 1. Вывод приветствия ---
    mov si, MsgDebug
    call print_string

    ; --- 2. Расчет LBA (Root Directory) ---
    ; Проверим FATSz32 на ноль (защита от затертого BPB)
    cmp dword [FATSz32], 0
    jz fatal_no_bpb

    mov eax, [FATSz32]
    movzx ebx, byte [NumFATs]
    mul ebx                 ; EAX = FATs * Size
    add eax, [HiddSec]
    movzx ebx, word [RsvdSecCnt]
    add eax, ebx
    mov [DataStartLBA], eax
    
    ; RootLBA = DataStart + (RootClus - 2) * SecPerClust
    mov eax, [RootClus]
    sub eax, 2
    movzx ebx, byte [SecPerClust]
    mul ebx
    add eax, [DataStartLBA]
    
    ; EAX = Адрес начала корневой папки.
    mov [CurrentLBA], eax

    ; --- 3. Чтение 1 сектора корня ---
    mov bx, 0x1000
    mov es, bx
    xor bx, bx          ; ES:BX = 0x1000:0000
    call ReadSector
    
    ; --- 4. Вывод первых найденных имен ---
    mov si, MsgFound
    call print_string

    ; -- Файл 1 --
    mov ax, 0x1000
    mov ds, ax          ; DS теперь указывает на буфер с файлами (0x10000)
    mov si, 0           ; Смещение 0 (первый файл)
    call print_filename_raw
    
    ; Выведем разделитель " | "
    xor ax, ax
    mov ds, ax          ; Вернем DS=0 для сообщения
    mov si, MsgPipe
    call print_string
    
    ; -- Файл 2 --
    mov ax, 0x1000
    mov ds, ax
    mov si, 32          ; Смещение 32 (второй файл)
    call print_filename_raw

    ; --- Стоп ---
    xor ax, ax
    mov ds, ax
    mov si, MsgDone
    call print_string
    
    cli
    hlt
    jmp $

fatal_no_bpb:
    mov si, MsgNoBPB
    call print_string
    cli
    hlt

; ----------------------------------------------------------------------
; Функции
; ----------------------------------------------------------------------

ReadSector:
    mov [DAP_LBA_Low], eax
    mov [DAP_LBA_High], dword 0
    mov ah, 0x42
    mov dl, [DrvNum]    ; Используем переменную из BPB
    mov si, DAP
    int 0x13
    jc read_err
    ret

read_err:
    mov si, MsgReadErr
    call print_string
    cli
    hlt

print_filename_raw:
    ; Печатает 11 байт по адресу DS:SI
    mov cx, 11
.ploop:
    lodsb
    ; Фильтр символов
    cmp al, 32
    jb .bad_char
    cmp al, 126
    ja .bad_char
    jmp .ok_char
.bad_char:
    mov al, '?'
.ok_char:
    mov ah, 0x0E
    int 0x10
    loop .ploop
    ret

print_string:
    push ax
    push si
.loop:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    int 0x10
    jmp .loop
.done:
    pop si
    pop ax
    ret

; ----------------------------------------------------------------------
; Данные
; ----------------------------------------------------------------------
MsgDebug    db 'DEBUG PBR START', 13, 10, 0
MsgNoBPB    db 'BPB ZERO ERROR!', 0
MsgReadErr  db 'READ ERROR!', 0
MsgFound    db 'Names: [', 0
MsgPipe     db '] [', 0
MsgDone     db ']', 13, 10, 0

; DrvNum удален отсюда, так как он есть в начале файла
DataStartLBA dd 0
CurrentLBA   dd 0

align 4
DAP:
    db 0x10
    db 0
    dw 1
    dw 0x0000
    dw 0x1000       ; Segment
DAP_LBA_Low  dd 0
DAP_LBA_High dd 0

times 510-($-$$) db 0
dw 0xAA55