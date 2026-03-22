#include <stdint.h>

// -------------------------
//    ELF64 DEFINITIONS
// -------------------------

// Базовые типы ELF64
typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef int32_t  Elf64_Sword;
typedef uint64_t Elf64_Xword;
typedef int64_t  Elf64_Sxword;
typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;

#define ELF_NIDENT 16

// Заголовок ELF файла (64-bit)
typedef struct {
    uint8_t     e_ident[ELF_NIDENT];
    Elf64_Half  e_type;
    Elf64_Half  e_machine;
    Elf64_Word  e_version;
    Elf64_Addr  e_entry;        // 64-bit адрес входа
    Elf64_Off   e_phoff;        // 64-bit смещение заголовков программ
    Elf64_Off   e_shoff;        // 64-bit смещение заголовков секций
    Elf64_Word  e_flags;
    Elf64_Half  e_ehsize;
    Elf64_Half  e_phentsize;
    Elf64_Half  e_phnum;
    Elf64_Half  e_shentsize;
    Elf64_Half  e_shnum;
    Elf64_Half  e_shstrndx;
} Elf64_Ehdr;

// Заголовок программы (Program Header 64-bit)
typedef struct {
    Elf64_Word  p_type;
    Elf64_Word  p_flags;        // Флаги теперь здесь (для выравнивания)
    Elf64_Off   p_offset;       // 64-bit смещение
    Elf64_Addr  p_vaddr;        // 64-bit виртуальный адрес
    Elf64_Addr  p_paddr;
    Elf64_Xword p_filesz;       // 64-bit размер
    Elf64_Xword p_memsz;        // 64-bit размер
    Elf64_Xword p_align;
} Elf64_Phdr;

// Заголовок секции (Section Header 64-bit)
typedef struct {
    Elf64_Word  sh_name;
    Elf64_Word  sh_type;
    Elf64_Xword sh_flags;       // 64-bit флаги
    Elf64_Addr  sh_addr;        // 64-bit адрес
    Elf64_Off   sh_offset;      // 64-bit смещение
    Elf64_Xword sh_size;        // 64-bit размер
    Elf64_Word  sh_link;
    Elf64_Word  sh_info;
    Elf64_Xword sh_addralign;
    Elf64_Xword sh_entsize;
} Elf64_Shdr;

// Таблица символов (Symbol Table 64-bit)
typedef struct {
    Elf64_Word  st_name;
    uint8_t     st_info;
    uint8_t     st_other;
    Elf64_Half  st_shndx;
    Elf64_Addr  st_value;       // 64-bit значение
    Elf64_Xword st_size;        // 64-bit размер
} Elf64_Sym;

#define PT_LOAD     1
#define PT_TLS      7
#define SHT_SYMTAB  2
#define SHT_STRTAB  3

#define ELF_FLAG_KERNEL 1
#define ELF_RESULT_OK 0
#define ELF_RESULT_INVALID 1
#define ELF_RESULT_NO_MEM 2

// Сегменты GDT для x86_64
// В Long Mode сегментация почти отключена, но селекторы важны для CPL (Rings).
// Типичная раскладка для поддержки syscall/sysret:
// 0x00 - Null
// 0x08 - Kernel Code
// 0x10 - Kernel Data
// 0x18 - User Data (или User Code 32-bit compat)
// 0x20 - User Data
// 0x28 - User Code 64-bit
#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_DATA   0x1B
#define GDT_USER_CODE   0x23

typedef struct process_t process_t;
typedef struct {
    uint64_t entry_point;
    process_t* proc;
    uint32_t result;
} elf_load_result_t;