[BITS 16]
[ORG 0x7C00]

jmp short start
nop

; ----------------------------------------------------------------------
; BIOS Parameter Block (BPB)
; ----------------------------------------------------------------------
OEMName         db 'MSWIN4.1'
BytesPerSec     dw 512
SecPerClust     db 8
RsvdSecCnt      dw 32
NumFATs         db 2
RootEntCnt      dw 0
TotSec16        dw 0
Media           db 0xF8
FATSz16         dw 0
SecPerTrk       dw 63
NumHeads        dw 255
HiddSec         dd 0
TotSec32        dd 0
FATSz32         dd 0
ExtFlags        dw 0
FSVer           dw 0
RootClus        dd 2
FSInfo          dw 1
BkBootSec       dw 6
Reserved        times 12 db 0
DrvNum          db 0x80
Reserved1       db 0
BootSig         db 0x29
VolID           dd 0
VolLab          db 'NO NAME    '
FilSysType      db 'FAT32   '

; ----------------------------------------------------------------------
; Точка входа
; ----------------------------------------------------------------------
start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [DrvNum], dl

    mov eax, [HiddSec]
    movzx ebx, word [RsvdSecCnt]
    add eax, ebx
    mov [FATStartLBA], eax

    mov ecx, [FATSz32]
    movzx edx, byte [NumFATs]
    xchg eax, ecx
    mul edx
    add eax, ecx
    mov [DataStartLBA], eax

    mov eax, [RootClus]
    mov [Cluster], eax

search_dir_loop:
    call ClusterToLBA
    movzx cx, byte [SecPerClust]
    mov bx, 0x8000
    call ReadSectors

    movzx cx, byte [SecPerClust]
    shl cx, 4               ; CX = sectors * 16 записей
    mov di, 0x8000

.scan_entries:
    push cx
    push di
    mov si, FileName
    mov cx, 11
    rep cmpsb
    pop di
    pop cx
    je file_found
    add di, 32
    loop .scan_entries

    call GetNextCluster
    cmp eax, 0x0FFFFFF8
    jb search_dir_loop

    mov si, MsgErrFile
    jmp boot_error

file_found:
    mov dx, [di + 0x14]
    shl edx, 16
    mov dx, [di + 0x1A]
    mov [Cluster], edx

    mov ax, 0x1000
    mov es, ax
    xor bx, bx

load_file_loop:
    call ClusterToLBA
    movzx cx, byte [SecPerClust]
    call ReadSectors

    movzx ax, byte [SecPerClust]
    shl ax, 5
    mov dx, es
    add dx, ax
    mov es, dx

    call GetNextCluster
    cmp eax, 0x0FFFFFF8
    jb load_file_loop

    ; ------------------------------------------------------------------
    ; Проверка сигнатуры загрузчика и прыжок
    ; ------------------------------------------------------------------
    mov ax, 0x1000
    mov es, ax
    xor ax, ax
    mov ds, ax
    mov si, SigText
    mov di, 2
    mov cx, 6
    repe cmpsb
    jne sig_error

    mov dl, [DrvNum]
    jmp 0x1000:0x0000

sig_error:
    mov si, MsgErrSig
    jmp boot_error

boot_error:
    xor ax, ax
    mov ds, ax
.print_loop:
    lodsb
    or al, al
    jz .halt
    mov ah, 0x0E
    int 0x10
    jmp .print_loop
.halt:
    hlt
    jmp .halt

ClusterToLBA:
    mov eax, [Cluster]
    sub eax, 2
    movzx ecx, byte [SecPerClust]
    mul ecx
    add eax, [DataStartLBA]
    ret

GetNextCluster:
    mov eax, [Cluster]
    shl eax, 2
    mov edx, eax
    shr eax, 9              ; EAX = Смещение сектора
    add eax, [FATStartLBA]
    and dx, 511             ; DX = Смещение байта в секторе

    push es
    push bx
    xor bx, bx
    mov es, bx
    mov bx, 0x7E00
    mov cx, 1
    call ReadSectors
    pop bx
    pop es

    mov si, 0x7E00
    add si, dx
    mov eax, [si]
    and eax, 0x0FFFFFFF
    mov [Cluster], eax
    ret

ReadSectors:
    pusha
    mov [DAP_LBA_Low], eax
    mov [DAP_Count], cx
    mov [DAP_Seg], es
    mov [DAP_Off], bx
    mov ah, 0x42
    mov dl, [DrvNum]
    mov si, DAP
    int 0x13
    jc read_error
    popa
    ret

read_error:
    mov si, MsgErrRead
    jmp boot_error

; ----------------------------------------------------------------------
; Данные
; ----------------------------------------------------------------------
FileName     db 'AOSLDR  BIN'
SigText      db 'AOSLDR'
MsgErrRead   db 'RE', 0
MsgErrFile   db 'NF', 0
MsgErrSig    db 'BS', 0

Cluster      dd 0
FATStartLBA  dd 0
DataStartLBA dd 0

DAP:
    db 0x10
    db 0
DAP_Count dw 0
DAP_Off   dw 0
DAP_Seg   dw 0
DAP_LBA_Low  dd 0
DAP_LBA_High dd 0

times 510-($-$$) db 0
dw 0xAA55