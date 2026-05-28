global start
global boot_ver
extern kernel_main
extern kernel_stack

%define KERNEL_VMA 0xFFFFFFFF80000000
%define KERNEL_BASE 0x10000
%define BOOT_INFO_ADDR 0x7000
%define BOOT_STRUCT_VERSION 2
%define BOOT_MAGIC 0xB007CAFE

%define VBE_INFO_BLOCK  0x5000  ; 512 байт
%define MODE_INFO_BLOCK 0x5200  ; 256 байт
%define DESIRED_WIDTH   1024
%define DESIRED_HEIGHT  768
%define DESIRED_BPP     32

%define BOOT_FLAG_VIDEO  1
%define BOOT_FLAG_ACPI   2
%define BOOT_FLAG_SMBIOS 4

%define V2P(x) ((x) - KERNEL_VMA)
%define V2P16(x) ((x) - KERNEL_VMA - KERNEL_BASE)

section .text.boot
bits 16
start:
	jmp short entry
	db "AOSLDR"
	dq V2P(kernel_main)
boot_ver:
	dd BOOT_STRUCT_VERSION
	dq BOOT_INFO_ADDR
	times 0x10-4-8 nop
entry:

    cli
	cld
	
	mov [cs:V2P16(initrd_phys_tmp)], ebx
	mov [cs:V2P16(initrd_size_tmp)], ecx
	mov [cs:V2P16(boot_drive)], dl
	
	mov al, 0xFF
	out 0xA1, al
	out 0x21, al
	
	mov ax, 0x1000
    mov ss, ax
    mov sp, 0xFFFE
	xor ax, ax
    mov ds, ax
	mov es, ax
	
	
    call V2P16(get_memory_map)

    call V2P16(find_acpi)

    call V2P16(find_smbios)
	
	mov edi, BOOT_INFO_ADDR
    xor eax, eax
    mov ecx, 35             ; Очищаем 35 dword-ов = 140 байт (структура стала больше)
    rep stosd
	mov edi, BOOT_INFO_ADDR
	
	; [0] Header
    mov dword [edi + 0], BOOT_MAGIC
    mov dword [edi + 4], BOOT_STRUCT_VERSION
    mov byte  [edi + 8], 1  ; BOOT_TYPE_MBR

    ; [16] Initrd Info (64-bit поля)
    mov eax, [cs:V2P16(initrd_phys_tmp)]
    mov dword [edi + 16], eax
    mov dword [edi + 20], 0      ; High 32 bits (всегда 0 в реальном режиме)
    
    mov eax, [cs:V2P16(initrd_size_tmp)]
    mov dword [edi + 24], eax
    mov dword [edi + 28], 0      ; High 32 bits

    ; [32] Video (Common)
	call V2P16(init_vbe)

    ; [62] MMap (Common)
    mov dword [edi + 62], 0x2004 ; mmap.map_addr low
    mov dword [edi + 66], 0      ; mmap.map_addr high
    
    xor eax, eax
    mov ax, [0x2000]             
    imul eax, 24                 
    mov dword [edi + 70], eax    ; mmap.map_size low
    mov dword [edi + 74], 0      ; mmap.map_size high
    
    mov dword [edi + 78], 24     ; mmap.desc_size
    mov dword [edi + 82], 0      ; mmap.desc_version

    ; [86] ACPI RSDP (Common)
    mov eax, dword [0x3000]      
    mov dword [edi + 86], eax    ; acpi_rsdp low
    mov dword [edi + 90], 0      ; acpi_rsdp high
	
	test eax, eax
    jz .no_acpi
    or dword [edi + 110], BOOT_FLAG_ACPI
.no_acpi:

    ; [94] SMBIOS Entry (Common)
    mov eax, dword [0x4000]      
    mov dword [edi + 94], eax    ; smbios_entry low
    mov dword [edi + 98], 0      ; smbios_entry high
	
	test eax, eax
    jz .no_smbios
    or dword [edi + 110], BOOT_FLAG_SMBIOS
.no_smbios:

    ; [114] Specific (MBR)
    mov al, [cs:V2P16(boot_drive)]
    
    in al, 0x92
    or al, 2
    out 0x92, al

    lgdt [cs:V2P16(gdt32_descriptor)]

    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp dword 0x08:V2P(init_32bit)
	
find_acpi:
    ; Search 0xE0000-0xFFFFF (128KB) per ACPI spec
    mov ax, 0xE000
    mov es, ax
    xor di, di

.search_loop:
    cmp dword [es:di], 0x20445352 ; "RSD "
    jne .next
    cmp dword [es:di+4], 0x20525450 ; "PTR "
    jne .next

    ; Found RSDP
    xor eax, eax
    mov ax, es
    shl eax, 4
    movzx ecx, di
    add eax, ecx
    mov [0x3000], eax
    ret

.next:
    add di, 16
    cmp di, 0
    jne .search_loop
    ; If ES was 0xE000, try 0xF000 segment too
    mov ax, es
    cmp ax, 0xE000
    jne .not_found
    mov ax, 0xF000
    mov es, ax
    xor di, di
    jmp .search_loop
.not_found:
    ret

find_smbios:
    mov ax, 0xF000
    mov es, ax
    xor di, di

.search_loop:
    cmp dword [es:di], 0x5F4D535F
    jne .next
    
    xor eax, eax
    mov ax, es
    shl eax, 4
    movzx ecx, di
    add eax, ecx
    mov [0x4000], eax
    ret

.next:
    add di, 16
    cmp di, 0
    jne .search_loop
    ret

	
get_memory_map:
    xor ebx, ebx
    mov edx, 0x534D4150     ; 'SMAP'
    mov di, 0x2004
    xor bp, bp

.loop:
    mov eax, 0xE820
    mov edx, 0x534D4150     ; reload 'SMAP' (some BIOS clobber edx)
    mov ecx, 24
    int 0x15
    jc .done
    add di, 24
    inc bp
    test ebx, ebx
    jne .loop

.done:
    mov [0x2000], bp
    ret
	
init_vbe:
    pusha
    mov ax, 0
    mov es, ax
    
    ; 1. VBE Controller Info
    mov di, VBE_INFO_BLOCK
    mov ax, 0x4F00
    mov dword [di], 0x32454256 ; VBE2
    int 0x10
    cmp ax, 0x004F
    jne .vbe_fail

    mov ax, [di + 14] ; Offset
    mov fs, [di + 16] ; Segment
    mov si, ax

.find_mode_loop:
    mov dx, [fs:si]
    add si, 2
    cmp dx, 0xFFFF
    je .vbe_fail

    mov ax, 0x4F01
    mov cx, dx
    mov di, MODE_INFO_BLOCK
    int 0x10
    cmp ax, 0x004F
    jne .find_mode_loop

    ; LFB support
    test byte [di], 0x80
    jz .find_mode_loop

    mov ax, [di + 18] ; X Resolution
    cmp ax, DESIRED_WIDTH
    jne .find_mode_loop

    mov ax, [di + 20] ; Y Resolution
    cmp ax, DESIRED_HEIGHT
    jne .find_mode_loop

    mov al, [di + 25] ; Bits Per Pixel
    cmp al, DESIRED_BPP
    jne .find_mode_loop
    
    mov bx, dx
    or bx, 0x4000     ; Bit 14 = Use Linear Framebuffer
    mov ax, 0x4F02
    int 0x10
    cmp ax, 0x004F
    jne .vbe_fail
	
	or dword [BOOT_INFO_ADDR + 110], BOOT_FLAG_VIDEO

    ; boot_video_t:
    ; +0  addr (8)
    ; +8  width (4)
    ; +12 height (4)
    ; +16 pitch (4)
    ; +20 bpp (4)
    ; +24 masks...
    
    mov bx, BOOT_INFO_ADDR + 32
    
    ; Framebuffer Phys Address (offset 40 in ModeInfo)
    mov eax, [di + 40]
    mov [bx + 0], eax  ; Low 32
    mov dword [bx + 4], 0      ; High 32 (VBE < 4GB)

    ; Width / Height
    xor eax, eax
    mov ax, [di + 18]
    mov [bx + 8], eax  ; width
    mov ax, [di + 20]
    mov [bx + 12], eax ; height

    ; Pitch (Bytes Per ScanLine) - offset 16
    mov ax, [di + 16]
    mov [bx + 16], eax ; pitch

    ; BPP - offset 25
    xor eax, eax
    mov al, [di + 25]
    mov [bx + 20], eax ; bpp

    ; Masks (Red, Green, Blue) - offset 31..36 in ModeInfo
    ; boot_video_t: r_size, r_pos, g_size, g_pos, b_size, b_pos
    ; ModeInfo:     r_size(31), r_pos(32), ...
    
    lea si, [di + 31]
    lea di, [bx + 24]
    mov cx, 6
    rep movsb

    popa
    ret

.vbe_fail:
    popa
    ret

bits 32
init_32bit:
    mov ax, 0x10
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov esp, 0x90000

    %define PML4_ADDR 0x80000
    %define PDPT_ADDR 0x81000
    %define PD_ADDR   0x82000
    %define PD_VIDEO  0x83000
    
    mov edi, PML4_ADDR
    xor eax, eax
    mov ecx, 4 * 1024 
    rep stosd

    mov edi, PML4_ADDR
    
    ; PML4[0] -> PDPT (Identity mapping)
    mov eax, PDPT_ADDR
    or eax, 0x3 ; Present | RW
    mov [edi], eax

    ; PML4[511] -> PDPT (Higher Half mapping)
    mov eax, PDPT_ADDR
    or eax, 0x3
    mov [edi + 511 * 8], eax 

    ; 3. Настраиваем PDPT
    mov edi, PDPT_ADDR
    
    ; PDPT[0] -> PD_ADDR (Identity 0..1GB)
    mov eax, PD_ADDR
    or eax, 0x3
    mov [edi], eax

    ; PDPT[510] -> PD_ADDR (Higher Half Kernel -2GB)
    mov eax, PD_ADDR
    or eax, 0x3
    mov [edi + 510 * 8], eax
    
    ; PDPT[511] -> PD_VIDEO (Higher Half Video)
    mov eax, PD_VIDEO
    or eax, 0x3
    mov [edi + 511 * 8], eax

    mov edi, PD_ADDR
    mov eax, 0x83       ; Phys=0 | PS=1 (2MB Huge Page) | RW | Present
    mov [edi], eax

    mov edi, PD_VIDEO
    
    mov esi, BOOT_INFO_ADDR + 32
    mov eax, [esi]
    
    test eax, eax
    jz .skip_video_map

    or eax, 0x93
    mov ecx, 16      
.video_loop:
    mov [edi], eax
    add eax, 0x200000
    add edi, 8
    loop .video_loop

    mov esi, BOOT_INFO_ADDR + 32
    mov dword [esi], 0xC0000000     
    mov dword [esi+4], 0xFFFFFFFF   

.skip_video_map:
    
    ; PAE
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    ; EFER LME
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    ; Load CR3
    mov eax, PML4_ADDR
    mov cr3, eax

    ; Enable Paging
    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax

    ; GDT64
    lgdt [V2P(gdt64_ptr32)]

    jmp 0x08:V2P(init_64bit)

bits 64
DEFAULT REL
init_64bit:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    mov rax, higher_half_entry
    jmp rax

higher_half_entry:
    mov rsp, kernel_stack + 16384
	
	mov rax, cr0
	and rax, ~((1 << 2) | (1 << 3)) ; Сбросить EM и TS
	or rax, (1 << 1)                ; Установить MP
	mov cr0, rax

	mov rax, cr4
	or rax, (3 << 9)                ; OSFXSR | OSXMMEXCPT
	mov cr4, rax
	
	sub rsp, 10
    mov word [rsp], 0
    mov qword [rsp+2], 0
    lidt [rsp]
    add rsp, 10

    mov rdi, KERNEL_VMA + BOOT_INFO_ADDR

    call kernel_main
    
    cli
.hlt:
    hlt
    jmp .hlt
	
extern isr_handler

isr_common_stub:
	test qword [rsp + 24], 3
    jz .kernel_entry
    swapgs
.kernel_entry:
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rbp
    push rdi
    push rsi
    push rdx
    push rcx
    push rbx
    push rax
	
    mov rdi, rsp
    
    call isr_handler
    pop rax
    pop rbx
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    pop rbp
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15
	
	test qword [rsp + 24], 3
    jz .kernel_exit
    swapgs
.kernel_exit:
    add rsp, 16
    iretq

%macro ISR_NOERRCODE 1
global isr%1
isr%1:
    push 0                  ; Dummy error code
    push %1                 ; Interrupt number
    jmp isr_common_stub
%endmacro

%macro ISR_ERRCODE 1
global isr%1
isr%1:
    push %1                 ; Interrupt number (Error code уже в стеке)
    jmp isr_common_stub
%endmacro

%assign i 0
%rep 48
    %if i == 8 || i == 10 || i == 11 || i == 12 || i == 13 || i == 14 || i == 17
        ISR_ERRCODE i
    %else
        ISR_NOERRCODE i
    %endif
%assign i i+1
%endrep

global syscall_entry
extern syscall_handler

syscall_entry:
	cli
    swapgs 
    mov [gs:0x0], rsp 
    mov rsp, [gs:0x8] 

    push qword [gs:0x0] ; [offset 120] User RSP
    push r11            ; [offset 112] User RFLAGS
    push rcx            ; [offset 104] User RIP
    push rax            ; [offset 96]  RAX (Syscall Number)
    push rbx            ; [offset 88]
    push rbp            ; [offset 80]
    push rdx            ; [offset 72]  Arg 3
    push rsi            ; [offset 64]  Arg 2
    push rdi            ; [offset 56]  Arg 1
    push r8             ; [offset 48]  Arg 5
    push r9             ; [offset 40]  Arg 6
    push r10            ; [offset 32]  Arg 4 (Syscall convention)
    push r12            ; [offset 24]
    push r13            ; [offset 16]
    push r14            ; [offset 8]
    push r15            ; [offset 0]
	
	mov rdi, rsp

    sti
    call syscall_handler
    cli
	

    pop r15
    pop r14
    pop r13
    pop r12
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rdx
    pop rbp
    pop rbx
    pop rax

    pop rcx         ; RIP
    pop r11         ; RFLAGS

	pop rsp
    swapgs
	
    o64 sysret
	
global switch_to_task
extern state

; void switch_to_task(thread_t* current, thread_t* next);
; RDI = current
; RSI = next
switch_to_task:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    pushfq

    mov [rdi + 8], rsp 

	fxsave [rdi + 112]

    mov rsp, [rsi + 8]
	
	fxrstor [rsi + 112]

    mov rax, [rsi + 24] 
    mov rcx, cr3
    cmp rax, rcx
    je .done_cr3
    mov cr3, rax
.done_cr3:
	mov r8, [rsi + 32] 
    lea rax, [rel state]
    test word [rax + 4], 1
    jz .use_msr
    wrfsbase r8
    jmp .done_fs
.use_msr:
    mov ecx, 0xC0000100
    mov eax, r8d
    mov rdx, r8
    shr rdx, 32
    wrmsr
.done_fs:
    popfq
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx

    ret

global trampoline_enter_user
global trampoline_enter_kernel
trampoline_enter_user:
	pop rdi
	pop rsi
    mov ax, 0x1B
    mov ds, ax
    mov es, ax

    ; [SS, RSP, RFLAGS, CS, RIP]
    iretq

extern kill_thread
extern current_thread
extern schedule
trampoline_enter_kernel:
    pop rax
    sti
    call rax
    mov rdi, [current_thread]
	mov rsi, 0
	call kill_thread
.loop:
	call schedule
	jmp .loop

boot_drive: db 0
acpi_found_addr: dd 0
smbios_found_addr: dd 0
initrd_phys_tmp: dd 0
initrd_size_tmp: dd 0

align 16
gdt32_start:
    dq 0
    dq 0x00CF9A000000FFFF ; 32-bit Code
    dq 0x00CF92000000FFFF ; 32-bit Data
gdt32_end:
gdt32_descriptor:
    dw gdt32_end - gdt32_start - 1
    dd V2P(gdt32_start)

align 16
gdt64_start:
    dq 0 ; Null
    dq 0x00209A0000000000 ; 0x08: Code 64-bit
    dq 0x0000920000000000 ; 0x10: Data 64-bit
gdt64_end:

gdt64_ptr32:
    dw gdt64_end - gdt64_start - 1
    dd V2P(gdt64_start)
	