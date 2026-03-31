[BITS 16]
[ORG 0x7C00]

jmp short start
nop

; --- BPB (Место под параметры, НЕ ЗАПИСЫВАЙТЕ ЕГО ПОВЕРХ ДИСКА!) ---
OEMName         db 'MSWIN4.1'
BytesPerSec     dw 512
SecPerClust     db 0    
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
DrvNum          db 0
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

    mov [DrvNum], dl
	
    ; Use HiddSec from BPB instead of hardcoded value

    ; Приветствие
    mov si, MsgStart
    call print_string

    ; Проверка BPB (Если FATSz32 = 0, значит параметры не считались)
    mov eax, [FATSz32]
    or eax, eax
    jz fatal_bpb_error

    ; ------------------------------------------------------------------
    ; 1. Вычисляем адрес начала данных
    ; ------------------------------------------------------------------
    mov eax, [FATSz32]
    movzx ebx, byte [NumFATs]
    mul ebx                 ; EAX = FATSz32 * NumFATs
    add eax, [HiddSec]      ; Добавляем скрытые сектора (ВАЖНО!)
    movzx ebx, word [RsvdSecCnt]
    add eax, ebx
    mov [DataStartLBA], eax

    ; ------------------------------------------------------------------
    ; 2. Вычисляем адрес корня
    ; ------------------------------------------------------------------
    mov eax, [RootClus]
    sub eax, 2
    movzx ebx, byte [SecPerClust]
    mul ebx
    add eax, [DataStartLBA]
    
    ; EAX = LBA первого сектора корня
    ; Сохраним его, будем инкрементировать в цикле
    mov [CurrentLBA], eax

    ; ------------------------------------------------------------------
    ; 3. Цикл сканирования (читаем 16 секторов подряд)
    ; ------------------------------------------------------------------
    mov cx, 16              ; Количество секторов для просмотра

sector_loop:
    push cx                 ; Сохраняем счетчик секторов
    
    ; Читаем 1 сектор в буфер 0x1000
    mov eax, [CurrentLBA]
    mov bx, 0x1000
    mov es, bx
    xor bx, bx              ; ES:BX = 0x1000:0000
    call ReadSector

    ; Сканируем записи внутри сектора
    call ScanBuffer

    ; Переходим к следующему сектору
    mov eax, [CurrentLBA]
    inc eax
    mov [CurrentLBA], eax
    
    pop cx
    loop sector_loop

    ; Конец работы
    mov si, MsgDone
    call print_string
    jmp stop

; ----------------------------------------------------------------------
; Процедура сканирования буфера (512 байт)
; ----------------------------------------------------------------------
ScanBuffer:
    push ds
    mov ax, 0x1000
    mov ds, ax          ; DS указывает на буфер с данными
    xor si, si          ; SI = 0 (начало буфера)
    
    mov cx, 16          ; 16 записей в секторе (512 / 32)
.entry_loop:
    push cx
    
    ; Проверка первого байта имени
    mov al, [si]
    
    cmp al, 0           ; 00 = Конец директории (дальше пусто)
    je .end_of_dir
    
    cmp al, 0xE5        ; E5 = Удаленный файл
    je .deleted_file
    
    ; Обычный файл - выводим имя
    call print_filename_raw
    jmp .next_entry

.deleted_file:
    ; Если хотите видеть удаленные, раскомментируйте:
    ; call print_filename_raw 
    jmp .next_entry

.end_of_dir:
    ; Можно написать [END], но мы просто пойдем дальше, 
    ; вдруг там мусор, который нам интересен.
    jmp .next_entry

.next_entry:
    pop cx
    add si, 32          ; Следующая запись
    loop .entry_loop
    
    pop ds
    ret

; ----------------------------------------------------------------------
; Вспомогательные функции
; ----------------------------------------------------------------------

print_filename_raw:
    ; Печатает "[ИМЯ]" из DS:SI
    push ax
    push si
    push cx
    
    ; Печатаем скобку [
    mov al, '['
    mov ah, 0x0E
    int 0x10

    mov cx, 11
.ploop:
    lodsb
    ; Защита от непечатаемых символов
    cmp al, 32
    jb .dot
    cmp al, 126
    ja .dot
    jmp .print
.dot:
    mov al, '.' ; Заменяем странные символы на точки
.print:
    mov ah, 0x0E
    int 0x10
    loop .ploop

    ; Печатаем скобку ] и пробел
    mov al, ']'
    int 0x10
    mov al, ' '
    int 0x10
    
    pop cx
    pop si
    pop ax
    ret

ReadSector:
    mov [DAP_LBA_Low], eax
    mov [DAP_LBA_High], dword 0
    mov ah, 0x42
    mov dl, [DrvNum]
    mov si, DAP
    int 0x13
    jc disk_error
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

disk_error:
    mov si, MsgDiskErr
    call print_string
    jmp stop
fatal_bpb_error:
    mov si, MsgBPBErr
    call print_string
stop:
    cli
    hlt
    jmp stop

; ----------------------------------------------------------------------
; Данные
; ----------------------------------------------------------------------
MsgStart    db 'SCANNER START...', 13, 10, 0
MsgDone     db 13, 10, 'SCAN DONE.', 0
MsgDiskErr  db 'Disk Err!', 0
MsgBPBErr   db 'BPB invalid (Zeros)!', 0

DataStartLBA dd 0
CurrentLBA   dd 0

align 4
DAP:
    db 0x10
    db 0
    dw 1
    dw 0x0000
    dw 0x1000
DAP_LBA_Low  dd 0
DAP_LBA_High dd 0

times 510-($-$$) db 0
dw 0xAA55