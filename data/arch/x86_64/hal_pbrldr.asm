[BITS 16]
[ORG 0x0000]

start:
	jmp short entry
	db "AOSLDR"
entry:
    cli
    cld
    mov ax, 0x0900
    mov ds, ax
    mov es, ax
    xor ax, ax
    mov ss, ax
    mov sp, 0x7C00
    mov fs, ax
    sti

    mov [DrvNum], dl

    mov eax, [fs:0x7C1C]
    movzx ebx, word [fs:0x7C0E]
    add eax, ebx
    mov [FATStartLBA], eax

    mov ecx, [fs:0x7C24]
    movzx edx, byte [fs:0x7C10]
    xchg eax, ecx
    mul edx
    add eax, ecx
    mov [DataStartLBA], eax

    mov si, FileKernel
    mov bx, 0x1000
    call LoadFAT32File
    cmp eax, 0
    je error_kernel

	mov ax, 0x0900
    mov ds, ax
    mov ax, 0x1000
    mov es, ax
    mov si, SigText
    mov di, 2
    mov cx, 6
    repe cmpsb
    jne error_sig

    mov ax, 0x0900
    mov ds, ax
    mov si, FileInitrd
    mov bx, 0x4000
    call LoadFAT32File
	cmp eax, 0
    je error_initrd
    mov [InitrdSize], eax

	mov ebx, 0x40000
    mov ecx, [cs:InitrdSize]
    mov dl, [cs:DrvNum]
    jmp 0x1000:0x0000

error_kernel:
    mov si, MsgNoKernel
    jmp halt_sys
error_sig:
    mov si, MsgBadSig
    jmp halt_sys
error_initrd:
    mov si, MsgNoInitrd
    jmp halt_sys

halt_sys:
    mov ax, 0x0900
    mov ds, ax
    mov ah, 0x0E
.loop:
    lodsb
    or al, al
    jz .done
    int 0x10
    jmp .loop
.done:
    cli
    hlt
    jmp .done

LoadFAT32File:
    push bx
    mov [DestSegment], bx
    mov [FileNamePtr], si

    mov eax, [fs:0x7C2C]
    mov [Cluster], eax

.search_dir:
    call ClusterToLBA
    movzx cx, byte [fs:0x7C0D]
    
    push 0x6000
    pop es
    
    xor bx, bx
    call ReadSectors

    movzx cx, byte [fs:0x7C0D]
    shl cx, 4
    mov di, 0

.scan:
    push cx
    push di
    mov si, [cs:FileNamePtr]
    mov cx, 11
    rep cmpsb
    pop di
    pop cx
    je .found
    add di, 32
    loop .scan

    call GetNextCluster
    cmp eax, 0x0FFFFFF8
    jb .search_dir
    
    pop bx
    xor eax, eax
    ret

.found:
    mov dx, [es:di + 0x14]
    shl edx, 16
    mov dx, [es:di + 0x1A]
    mov [Cluster], edx
    
    mov eax, [es:di + 0x1C]
    mov [FileSize], eax

    mov es, [cs:DestSegment]
    xor bx, bx

.load:
    call ClusterToLBA
    movzx cx, byte [fs:0x7C0D]
    call ReadSectors

    movzx ax, byte [fs:0x7C0D]
    shl ax, 5
    mov dx, es
    add dx, ax
    mov es, dx

    call GetNextCluster
    cmp eax, 0x0FFFFFF8
    jb .load

    pop bx
    mov ax, 0x0900
    mov ds, ax
    mov eax, [FileSize]
    ret

ClusterToLBA:
    mov eax, [Cluster]
    sub eax, 2
    movzx ecx, byte [fs:0x7C0D]
    mul ecx
    add eax, [cs:DataStartLBA]
    ret

GetNextCluster:
    mov eax, [Cluster]
    shl eax, 2
    mov edx, eax
    shr eax, 9
    add eax, [cs:FATStartLBA]
    and dx, 511

    push es
    push bx
    
    push 0x07E0
    pop es
    
    xor bx, bx
    mov cx, 1
    call ReadSectors
    pop bx
    pop es

    push 0x07E0
    pop gs
    mov si, dx
    mov eax, [gs:si]
    and eax, 0x0FFFFFFF
    mov [Cluster], eax
    ret

ReadSectors:
    pushad
    mov [DAP_LBA_Low], eax
    mov [DAP_Count], cx
    mov [DAP_Seg], es
    mov [DAP_Off], bx
    mov ah, 0x42
    mov dl, [cs:DrvNum]
    mov si, DAP
    int 0x13
    popad
    ret

SigText      db 'AOSLDR'
FileKernel   db 'AOSLDR  BIN'
FileInitrd   db 'INITRD  TAR'
MsgNoKernel  db 'Kernel missing!', 0
MsgNoInitrd  db 'Initrd missing!', 0
MsgBadSig    db 'Kernel signature invalid!', 0

DrvNum       db 0
Cluster      dd 0
FileNamePtr  dw 0
DestSegment  dw 0
FileSize     dd 0
InitrdSize   dd 0
FATStartLBA  dd 0
DataStartLBA dd 0

DAP:
    db 0x10, 0
DAP_Count dw 0
DAP_Off   dw 0
DAP_Seg   dw 0
DAP_LBA_Low  dd 0
DAP_LBA_High dd 0