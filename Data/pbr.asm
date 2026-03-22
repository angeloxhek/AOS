[BITS 16]
[ORG 0x7C00]

jmp short start
nop

; ----------------------------------------------------------------------
; BIOS Parameter Block (BPB) для FAT32
; Значения здесь - заглушки. При форматировании они перезаписываются.
; ----------------------------------------------------------------------
OEMName         db 'MSWIN4.1'
BytesPerSec     dw 0
SecPerClust     db 8
RsvdSecCnt      dw 32
NumFATs         db 2
RootEntCnt      dw 0
TotSec16        dw 0
Media           db 0xF8
FATSz16         dw 0
SecPerTrk       dw 63
NumHeads        dw 255
HiddSec         dd 0        ; Смещение раздела (LBA start)
TotSec32        dd 0
FATSz32         dd 0        ; Размер одной FAT в секторах
ExtFlags        dw 0
FSVer           dw 0
RootClus        dd 2        ; Кластер корневой директории
FSInfo          dw 1
BkBootSec       dw 6
Reserved        times 12 db 0
DrvNum          db 0x80
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
	push dx
	mov ax, 0003h
    int 10h
	xor ax, ax
	pop dx
    mov [DrvNum], dl
	mov eax, [HiddSec]
    mov ah, 0x41
    mov bx, 0x55AA
    int 0x13
    ;mov si, MsgErrLBA
    ;jc boot_error
    mov eax, [FATSz32]
    movzx ebx, byte [NumFATs]
    mul ebx                  ; EAX = FATSz32 * NumFATs
    add eax, [HiddSec]
    movzx ebx, word [RsvdSecCnt]
    add eax, ebx
    mov [DataStartLBA], eax
    mov eax, [RootClus]
    mov [Cluster], eax
    call ClusterToLBA
	mov dx, 32
	push dx
    push eax
    mov bx, 0x8000
    mov di, bx
    call ReadSector
    mov cx, 16
search_loop:
    push cx
    push di
    mov si, FileName
    mov cx, 11
    rep cmpsb
    pop di
    pop cx
    je file_found
    add di, 32
    loop search_loop
	pop eax
    pop dx
    inc eax
    dec dx
	jnz search_loop
    mov si, MsgErrFile
    jmp boot_error
file_found:
	mov si, SigText
    call print_string
    mov ax, [di + 0x14]
    shl eax, 16
    mov ax, [di + 0x1A] 
    mov [Cluster], eax
	mov eax, [di + 0x1C]
    add eax, 511
    shr eax, 9
	mov [DAP_Count], al
	call ClusterToLBA
	push eax
    call print_hex
    mov si, MsgNewline
    call print_string
    pop eax
    mov bx, 0x1000
    mov es, bx
	xor bx, bx
    call ReadSector
	mov si, SigText
    mov di, 0x0002
    mov cx, 6
    repe cmpsb
    mov si, MsgErrSig
    jne boot_error
    mov dl, [DrvNum]
    jmp 0x1000:0x0000
boot_error:
    call print_string
    xor ax, ax
    int 0x16
    int 0x19
    hlt
    jmp $
ClusterToLBA:
    mov eax, [Cluster]
    sub eax, 2
    movzx ecx, byte [SecPerClust]
    mul ecx
    add eax, [DataStartLBA]
    ret
ReadSector:
    mov [DAP_LBA_Low], eax
    mov [DAP_LBA_High], dword 0
	mov [DAP_Seg], es
    mov [DAP_Off], bx
    mov ah, 0x42
    mov dl, [DrvNum]
    mov si, DAP
    int 0x13
    jnc .success
    mov si, MsgErrRead
    jmp boot_error
.success:
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
print_hex:
    pushad
    mov cx, 8
.loop:
    rol eax, 4
    push eax
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .print
    add al, 7
.print:
    mov ah, 0x0E
    int 0x10
    pop eax
    loop .loop
    popad
    ret
FileName     db 'AOSLDR  BIN'
SigText      db 'AOSLDR'
MsgNewline   db 13, 10, 0
;MsgErrLBA    db 'NoLBA', 0
MsgErrRead   db 'RdErr', 0
MsgErrFile   db 'NoFile', 0
MsgErrSig    db 'BadSig', 0
Cluster      dd 0
DataStartLBA dd 0
DAP:
    db 0x10
    db 0
DAP_Count dw 1
DAP_Off dw 0
DAP_Seg dw 0
DAP_LBA_Low  dd 0
DAP_LBA_High dd 0
times 510-($-$$) db 0
dw 0xAA55