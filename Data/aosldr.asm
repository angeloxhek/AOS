global start
global boot_ver
extern kernel_main

; Константы для отображения
%define KERNEL_VMA 0xFFFFFFFF80000000
%define KERNEL_BASE 0x10000
%define BOOT_INFO_ADDR 0x7000
%define BOOT_STRUCT_VERSION 1
%define BOOT_MAGIC 0xB007CAFE

%define VBE_INFO_BLOCK  0x5000  ; 512 байт
%define MODE_INFO_BLOCK 0x5200  ; 256 байт
%define DESIRED_WIDTH   1024
%define DESIRED_HEIGHT  768
%define DESIRED_BPP     32

%define BOOT_FLAG_VIDEO  1
%define BOOT_FLAG_ACPI   2
%define BOOT_FLAG_SMBIOS 4

; Макрос: вычитает смещение, чтобы получить физический адрес текущей метки
%define V2P(x) ((x) - KERNEL_VMA)
%define V2P16(x) ((x) - KERNEL_VMA - KERNEL_BASE)

section .text
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
    ; Мы загружены по 0x10000, но линкер думает, что мы в 0xFFFFFFFF80010000.
    ; Все обращения к памяти здесь нужно корректировать макросом v!

    cli
	cld
	
	mov al, 0xFF
	out 0xA1, al
	out 0x21, al
	
	mov ax, 0x1000
    mov ss, ax
    mov sp, 0xFFFE
	xor ax, ax
    mov ds, ax
	mov es, ax
	
	mov [cs:V2P16(boot_drive)], dl
	
	; 1. Получаем карту памяти
    call V2P16(get_memory_map)

    ; 2. Ищем ACPI RSDP (сканирование BIOS)
    call V2P16(find_acpi)

    ; 3. Ищем SMBIOS (сканирование BIOS)
    call V2P16(find_smbios)
	
	mov edi, BOOT_INFO_ADDR
    xor eax, eax
    mov ecx, 30             ; 30 dword-ов = 120 байт
    rep stosd
	mov edi, BOOT_INFO_ADDR
	
	; [0] Header
    mov dword [edi + 0], BOOT_MAGIC
    mov dword [edi + 4], BOOT_STRUCT_VERSION
    mov byte  [edi + 8], 1  ; BOOT_TYPE_MBR

    ; [9] Video (Common)
	call V2P16(init_vbe)

    ; [39] MMap (Common)
    mov dword [edi + 39], 0x2004 ; mmap.map_addr low
    mov dword [edi + 43], 0      ; mmap.map_addr high
    
    xor eax, eax
    mov ax, [0x2000]             ; Количество записей, полученное в get_memory_map
    imul eax, 24                 ; Умножаем на размер записи (desc_size)
    mov dword [edi + 47], eax    ; mmap.map_size low
    mov dword [edi + 51], 0      ; mmap.map_size high
    
    mov dword [edi + 55], 24     ; mmap.desc_size
    mov dword [edi + 59], 0      ; mmap.desc_version

    ; [63] ACPI RSDP (Common)
    mov eax, dword [0x3000]      ; Адрес был сохранен функцией find_acpi сюда
    mov dword [edi + 63], eax    ; acpi_rsdp low
    mov dword [edi + 67], 0      ; acpi_rsdp high
	
	test eax, eax
    jz .no_acpi
    or dword [edi + 87], BOOT_FLAG_ACPI
.no_acpi:

    ; [71] SMBIOS Entry (Common)
    mov eax, dword [0x4000]      ; Адрес был сохранен функцией find_smbios сюда
    mov dword [edi + 71], eax    ; smbios_entry low
    mov dword [edi + 75], 0      ; smbios_entry high
	
	test eax, eax
    jz .no_smbios
    or dword [edi + 87], BOOT_FLAG_SMBIOS
.no_smbios:

    ; [79] Kernel Size (Common)
    mov dword [edi + 79], 0x100000 ; kernel_size low
    mov dword [edi + 83], 0        ; kernel_size high

    ; [91] Specific (MBR)
    mov al, [cs:V2P16(boot_drive)]
    mov byte [edi + 91], al
    
    ; -- Включаем A20 --
    in al, 0x92
    or al, 2
    out 0x92, al

    ; -- Загружаем GDT 32 --
    lgdt [cs:V2P16(gdt32_descriptor)]

    ; -- Переход в Protected Mode --
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; Прыжок в 32 бита
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
    xor ebx, ebx            ; продолжение (0 для начала)
    mov edx, 0x534D4150    ; 'SMAP'
    mov di, 0x2004          ; оставляем первые 4 байта под счетчик
    xor bp, bp              ; тут будем считать количество записей

.loop:
    mov eax, 0xE820
    mov edx, 0x534D4150    ; reload 'SMAP' (some BIOS clobber edx)
    mov ecx, 24             ; размер одной записи
    int 0x15
    jc .done                ; если Carry Flag, значит конец
    add di, 24              ; сдвигаем указатель
    inc bp                  ; увеличиваем счетчик
    test ebx, ebx           ; если ebx=0, значит всё
    jne .loop

.done:
    mov [0x2000], bp        ; сохраняем количество записей в начало
    ret
	
init_vbe:
    pusha
    mov ax, 0
    mov es, ax
    
    ; 1. Получаем VBE Controller Info
    mov di, VBE_INFO_BLOCK
    mov ax, 0x4F00
    mov dword [di], 0x32454256 ; Сигнатура "VBE2" (желательно для LFB)
    int 0x10
    cmp ax, 0x004F
    jne .vbe_fail

    ; Получаем указатель на массив режимов (segment:offset)
    mov ax, [di + 14] ; Offset
    mov fs, [di + 16] ; Segment (помещаем в FS)
    mov si, ax        ; SI теперь указывает на список режимов

.find_mode_loop:
    ; Читаем номер режима из FS:SI
    mov dx, [fs:si]
    add si, 2
    cmp dx, 0xFFFF    ; Конец списка?
    je .vbe_fail      ; Не нашли подходящий режим

    ; 2. Получаем Mode Info для текущего режима (DX)
    mov ax, 0x4F01
    mov cx, dx        ; Номер режима
    mov di, MODE_INFO_BLOCK
    int 0x10
    cmp ax, 0x004F
    jne .find_mode_loop

    ; 3. Проверяем характеристики
    ; Проверяем LFB support (бит 7 в ModeAttributes, смещение 0)
    test byte [di], 0x80
    jz .find_mode_loop

    ; Проверяем разрешение и цвет
    mov ax, [di + 18] ; X Resolution
    cmp ax, DESIRED_WIDTH
    jne .find_mode_loop

    mov ax, [di + 20] ; Y Resolution
    cmp ax, DESIRED_HEIGHT
    jne .find_mode_loop

    mov al, [di + 25] ; Bits Per Pixel
    cmp al, DESIRED_BPP
    jne .find_mode_loop

    ; --- РЕЖИМ НАЙДЕН (в DX) ---
    
    ; 4. Устанавливаем режим с LFB
    mov bx, dx
    or bx, 0x4000     ; Bit 14 = Use Linear Framebuffer
    mov ax, 0x4F02
    int 0x10
    cmp ax, 0x004F
    jne .vbe_fail
	
	or dword [BOOT_INFO_ADDR + 87], BOOT_FLAG_VIDEO

    ; 5. Заполняем структуру boot_video_t по адресу BOOT_INFO_ADDR + 9
    ; Структура boot_video_t (packed):
    ; +0  addr (8)
    ; +8  width (4)
    ; +12 height (4)
    ; +16 pitch (4)
    ; +20 bpp (4)
    ; +24 masks...
    
    mov bx, BOOT_INFO_ADDR + 9
    
    ; Framebuffer Phys Address (offset 40 in ModeInfo)
    mov eax, [di + 40]
    mov [bx + 0], eax  ; Low 32
    mov dword [bx + 4], 0      ; High 32 (обычно VBE < 4GB)

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
    
    lea si, [di + 31]  ; Источник масок
    lea di, [bx + 24]  ; Приемник в структуре
    mov cx, 6          ; Копируем 6 байт масок
    rep movsb

    popa
    ret

.vbe_fail:
    ; Если не удалось, можно просто выйти, видео поля останутся 0
    ; Ядро поймет, что графики нет
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

    ; --- Настройка Paging (Identity + Higher Half) ---
    %define PML4_ADDR 0x80000
    %define PDPT_ADDR 0x81000
    %define PD_ADDR   0x82000
    %define PD_VIDEO  0x83000
    
    ; 1. Очищаем область таблиц (4 таблицы по 4КБ = 16 КБ)
    mov edi, PML4_ADDR
    xor eax, eax
    mov ecx, 4 * 1024 
    rep stosd

    ; 2. Настраиваем PML4
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

    ; 4. Настраиваем PD_ADDR (Маппинг физической памяти ядра)
    ; Нам нужно замапить физический адрес 0 (где лежит ядро 0x10000)
    ; в виртуальный (identity) и верхний.
    mov edi, PD_ADDR
    mov eax, 0x83       ; Phys=0 | PS=1 (2MB Huge Page) | RW | Present
    mov [edi], eax      ; PD[0] мапит 0..2MB физической памяти
    
    ; Если ядро больше 2МБ, добавь еще:
    ; add eax, 0x200000
    ; mov [edi + 8], eax 
    ; -----------------------------------

    ; 5. Настраиваем PD_VIDEO (Маппинг LFB)
    mov edi, PD_VIDEO
    
    mov esi, BOOT_INFO_ADDR + 9 ; video structure
    mov eax, [esi]              ; framebuffer_addr (low 32)
    
    test eax, eax
    jz .skip_video_map

    ; Заполняем 16 страниц по 2МБ (32МБ видеопамяти)
    or eax, 0x93     ; Present | RW | Huge | Cache Disable (PCD)
    mov ecx, 16      
.video_loop:
    mov [edi], eax
    add eax, 0x200000
    add edi, 8
    loop .video_loop

    ; Обновляем адрес в структуре на виртуальный
    mov esi, BOOT_INFO_ADDR + 9
    mov dword [esi], 0xC0000000     
    mov dword [esi+4], 0xFFFFFFFF   

.skip_video_map:

    ; --- Включаем 64-бит ---
    
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

    ; Загружаем GDT64 (обращаемся физически)
    lgdt [V2P(gdt64_ptr32)]

    ; Прыжок в 64 бита
    ; Обратите внимание: мы все еще прыгаем по физическому (низкому) адресу метки,
    ; так как Identity Mapping активен (PML4[0] -> ... -> 0x0000).
    jmp 0x08:V2P(init_64bit)

bits 64
init_64bit:
    ; Сейчас RIP низкий (например, 0x00000000000010xx)
    ; Нам нужно "взлететь" в верхнюю память.
    
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    ; Загружаем метку с ВЫСОКИМ адресом в регистр
    mov rax, higher_half_entry
    jmp rax

higher_half_entry:
    ; ТЕПЕРЬ RIP = 0xFFFFFFFF8000xxxx
    ; Мы официально в Higher Half Kernel.
    
    ; Теперь можно настроить стек по виртуальному адресу
    mov rsp, 0xFFFFFFFF80090000 ; Стек где-то в отображенной области
	
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

    ; Готовим аргументы и вызываем C
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
    push r15            ; [offset 0]   <-- RSP указывает сюда
	
    ; Передаем указатель на структуру регистров (RSP) как первый аргумент (RDI)
	mov rdi, rsp

    sti             ; Включаем прерывания (если обработчик долгий)
    call syscall_handler
    cli             ; Выключаем прерывания перед восстановлением стека
	

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
    pop rax         ; Возвращаемое значение из handler (regs->rax)

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

; --- Данные ---
boot_drive: db 0
acpi_found_addr: dd 0
smbios_found_addr: dd 0

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

; Специальный указатель для загрузки GDT64 из 32-битного режима
gdt64_ptr32:
    dw gdt64_end - gdt64_start - 1
    dd V2P(gdt64_start)
	