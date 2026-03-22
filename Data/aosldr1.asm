[BITS 16]
global _start
extern kernel_main
%define PHYS_LOAD_ADDR   0x1000
%define KERNEL_VIRT_BASE 0xFFFFFFFF80000000

section .boot
_start:
    jmp short entry_point
    nop
    db 'AOSLDR'

entry_point:
	cli
	xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax          ; Стек будет начинаться с 0x0000:...
    mov sp, 0x7C00
	mov si, dskerr
	or dl, dl
	jz stop_err
	mov [boot_drive], dl
	
	mov si, sigerr
	cmp dword [0x1200], "AOSL"
    jne stop_err
    cmp dword [0x1204], "DR64"
    jne stop_err
	
    ; Включаем линию A20 (для доступа к памяти выше 1 МБ)
    in al, 0x92
    or al, 2
    out 0x92, al
    ; Загружаем таблицу дескрипторов (GDT)
	xor ax, ax
    mov ds, ax
    lgdt [gdt32_descriptor]
    ; Переключаем бит PE (Protection Enable) в CR0
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    ; Дальний прыжок в 32-битный сегмент кода
    ; 0x08 - это смещение сегмента кода в нашей GDT
    jmp dword 0x08:init_pm

stop_err:
	call print_string
	hlt
	jmp $

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
	
boot_drive: db 0
align 8
gdt32_start:
    dq 0
    dq 0x00CF9A000000FFFF ; 32-bit Code
    dq 0x00CF92000000FFFF ; 32-bit Data
gdt32_end:
gdt32_descriptor:
    dw gdt32_end - gdt32_start - 1
    dd gdt32_start

; 64-битный GDT
; Бит L (bit 53) = 1 (Long Mode Code)
; Бит D (bit 54) = 0
gdt64_start:
    dq 0
    dq 0x00209A0000000000 ; 64-bit Kernel Code (Execute/Read, Ring 0)
    dq 0x0000920000000000 ; 64-bit Kernel Data (Read/Write, Ring 0)
    dq 0x0020FA0000000000 ; 64-bit User Code   (Execute/Read, Ring 3)
    dq 0x0000F20000000000 ; 64-bit User Data   (Read/Write, Ring 3)
gdt64_end:
gdt64_descriptor:
    dw gdt64_end - gdt64_start - 1
    dd gdt64_start

sigerr: db "LDRSIGERR", 0
dskerr: db "LDRDSKERR", 0

[BITS 32]
init_pm:
    ; Теперь мы в 32-битном защищенном режиме!
    ; Настраиваем сегментные регистры данными (0x10 - смещение сегмента данных)
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Настраиваем стек
    mov ebp, 0x90000
    mov esp, ebp
	
	; 1. Очищаем таблицы страниц (PML4, PDPT, PD)
    ; Физически они находятся в BSS. Вычисляем физ. адрес.
    mov edi, pml4_table - KERNEL_VIRT_BASE
    xor eax, eax
    mov ecx, 4096 * 3 / 4 ; Очищаем 3 страницы (PML4, PDPT, PD)
    rep stosd

    ; 2. Настраиваем PML4 (Level 4)
    ; pml4_table[0] -> pdpt_table (для Identity Mapping нижних 4GB)
    mov eax, pdpt_table - KERNEL_VIRT_BASE
    or eax, 0x3 ; Present | RW
    mov [pml4_table - KERNEL_VIRT_BASE], eax
	

    ; pml4_table[511] -> pdpt_table (для Higher Half 0xFFFFFFFF80...)
    mov [pml4_table - KERNEL_VIRT_BASE + 511 * 8], eax

    ; 3. Настраиваем PDPT (Level 3)
    ; pdpt_table[0] -> pd_table (для Identity Mapping)
    mov eax, pd_table - KERNEL_VIRT_BASE
    or eax, 0x3 ; Present | RW
    mov [pdpt_table - KERNEL_VIRT_BASE], eax
    
    ; pdpt_table[510] -> pd_table (для Higher Half)
    ; Индекс 510 в PDPT соответствует виртуальным адресам -2GB
    mov [pdpt_table - KERNEL_VIRT_BASE + 510 * 8], eax

    ; 4. Настраиваем PD (Level 2) - используем 2MB страницы (Huge Pages)
    ; Мапим первые 2MB физической памяти напрямую.
    mov edi, pd_table - KERNEL_VIRT_BASE
    mov eax, 0x83 ; Present | RW | PS (Page Size = 2MB)
    mov [edi], eax
    
    ; (Опционально) Можно замапить больше, если ядро > 2МБ, циклом.

    ; 5. Включаем PAE (Physical Address Extension) в CR4
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    ; 6. Загружаем PML4 в CR3
    mov eax, pml4_table - KERNEL_VIRT_BASE
    mov cr3, eax

    ; 7. Включаем Long Mode в EFER MSR (0xC0000080)
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8 ; LM-bit
    wrmsr

    ; 8. Включаем Paging в CR0
    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax

[BITS 64]
pm_jump:

    ; 9. Загружаем 64-битный GDT
    lgdt [gdt64_descriptor]

    ; 10. Прыжок в 64-битный код
    jmp 0x08:init_lm

section .text
init_lm_magic:
	db "AOSLDR64"
init_lm:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Стек в Higher Half
    mov rsp, 0xFFFFFFFF80090000 

    ; Передаем аргументы в kernel_main
    ; В 64-bit ABI (System V) первый аргумент идет в RDI!
    xor rdi, rdi
    mov dl, [boot_drive] ; Читаем из Identity map
    mov dil, dl
    
    ; Убираем Identity Mapping (pml4[0] = 0) для безопасности
    ; mov rax, 0
    ; mov [pml4_table + 0], rax
    ; mov rax, cr3
    ; mov cr3, rax ; Flush TLB

    call kernel_main

    cli
.hang:
    hlt
    jmp .hang

extern isr_handler

; В 64 битах нет pusha/popa. Нужно сохранять регистры вручную.
; Структура registers_t в Си должна соответствовать этому порядку!
isr_common_stub:
    ; Сохраняем регистры общего назначения
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

    ; Передаем указатель на структуру регистров (RSP) как первый аргумент (RDI)
    mov rdi, rsp
    
    call isr_handler

    ; Восстанавливаем
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
    
    ; Удаляем код ошибки и номер прерывания (16 байт)
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

; Генерация ISR...
%assign i 0
%rep 48
    %if i == 8 || i == 10 || i == 11 || i == 12 || i == 13 || i == 14 || i == 17
        ISR_ERRCODE i
    %else
        ISR_NOERRCODE i
    %endif
%assign i i+1
%endrep

section .bss
align 4096
global pml4_table
pml4_table: resb 4096
global pdpt_table
pdpt_table: resb 4096
global pd_table
pd_table:   resb 4096