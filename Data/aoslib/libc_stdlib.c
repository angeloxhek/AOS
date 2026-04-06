#include <stddef.h>
#define AOSLIB_SYSCALLS
#define AOSLIB_STRING
#include "../include/aoslib.h"

__attribute__((noreturn)) void exit(int code) {
    syscall(SYS_EXIT, code, 0, 0, 0, 0);
    while (1) {
        asm volatile("pause");
    }
	__builtin_unreachable();
}

__attribute__((noreturn)) void __stack_chk_fail(void) {
    exit(STAT_STACK_SMASHING);
    __builtin_unreachable();
}

static malloc_header_t* free_list_start = 0;
static int malloc_initialized = 0;
#define ALIGN_PAGE(x) (((x) + 4095) & ~4095)

void* malloc(size_t size) {
    if (size == 0) return (void*)0;

    size = (size + 15) & ~15;
    
    if (!malloc_initialized) {
        size_t initial_size = size + sizeof(malloc_header_t);
        initial_size = ALIGN_PAGE(initial_size);
        
        void* ptr = syscall_sbrk(initial_size);
        if (ptr == (void*)0 || ptr == (void*)-1) return (void*)0;
        
        free_list_start = (malloc_header_t*)ptr;
        free_list_start->size = initial_size - sizeof(malloc_header_t);
        free_list_start->is_free = 1;
        free_list_start->next = (void*)0;
        malloc_initialized = 1;
    }

restart_search:
    
    malloc_header_t* current = free_list_start;
    malloc_header_t* last = (void*)0;
    
    while (current) {
        if (current->is_free && current->size >= size) {
            if (current->size > size + sizeof(malloc_header_t) + 16) {
                malloc_header_t* next_block = (malloc_header_t*)((uint8_t*)current + sizeof(malloc_header_t) + size);
                next_block->size = current->size - size - sizeof(malloc_header_t);
                next_block->is_free = 1;
                next_block->next = current->next;

                current->size = size;
                current->next = next_block;
            }
            current->is_free = 0;
            return (void*)((uint8_t*)current + sizeof(malloc_header_t));
        }
        last = current;
        current = current->next;
    }
    
    size_t total_needed = size + sizeof(malloc_header_t);
    total_needed = ALIGN_PAGE(total_needed);
    
    malloc_header_t* new_block = (malloc_header_t*)syscall_sbrk(total_needed);
    if ((size_t)new_block <= 0) return (void*)0;
    
    new_block->size = total_needed - sizeof(malloc_header_t);
    new_block->is_free = 1;
    new_block->next = (void*)0;
    
    if (last) {
        last->next = new_block;
    } else {
        free_list_start = new_block;
    }

    goto restart_search;
}

void free(void* ptr) {
    if (!ptr) return;
    
    malloc_header_t* header = (malloc_header_t*)((uint8_t*)ptr - sizeof(malloc_header_t));
    header->is_free = 1;
    
    malloc_header_t* current = free_list_start;
    while (current && current->next) {
        if (current->is_free && current->next->is_free) {
            if ((uint8_t*)current + sizeof(malloc_header_t) + current->size == (uint8_t*)current->next) {
                current->size += current->next->size + sizeof(malloc_header_t);
                current->next = current->next->next;
                continue; 
            }
        }
        current = current->next;
    }
}

void* realloc(void* ptr, size_t new_size) {
    if (!ptr) return malloc(new_size);
    if (new_size == 0) {
        free(ptr);
        return 0;
    }
    
    malloc_header_t* header = (malloc_header_t*)((uint8_t*)ptr - sizeof(malloc_header_t));
    if (header->size >= new_size) return ptr;
    
    void* new_ptr = malloc(new_size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, header->size);
        free(ptr);
    }
    return new_ptr;
}

void* calloc(size_t num, size_t size) {
    if (num == 0 || size == 0) {
        return (void*)0;
    }
    size_t max_val = (size_t)-1;
    if (size > max_val / num) {
        return (void*)0;
    }
    size_t total_size = num * size;
    void* ptr = malloc(total_size);
    if (ptr != (void*)0) {
        memset(ptr, 0, total_size);
    }
    return ptr;
}

static inline int char_to_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return 255; // Недопустимый символ
}

static bool is_clean_tail(const char *endptr) {
    return (*endptr == '\0' || (*endptr == '\n' && *(endptr + 1) == '\0'));
}

unsigned long long strtoull(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    unsigned long long acc = 0;
    int c;
    bool is_neg = false;
    bool overflowed = false;

    while (isspace(*s)) s++;

    if (*s == '-') {
        is_neg = true;
        s++;
    } else if (*s == '+') {
        s++;
    }

    if ((base == 0 || base == 16) && *s == '0' && (*(s + 1) == 'x' || *(s + 1) == 'X')) {
        s += 2;
        base = 16;
    } else if (base == 0) {
        base = (*s == '0') ? 8 : 10;
    }

    if (base < 2 || base > 36) {
        if (endptr) *endptr = (char *)nptr;
        return 0;
    }

    unsigned long long cutoff = ULLONG_MAX / base;
    unsigned int cutlim = ULLONG_MAX % base;

    const char *digits_start = s;
    while (*s) {
        c = char_to_val(*s);
        if (c >= base) break;

        if (overflowed || acc > cutoff || (acc == cutoff && c > cutlim)) {
            overflowed = true;
        } else {
            acc = acc * base + c;
        }
        s++;
    }

    if (endptr) {
        *endptr = (char *)(s == digits_start ? nptr : s);
    }

    if (overflowed) return ULLONG_MAX;
    return is_neg ? -acc : acc; 
}

long long strtoll(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    while (isspace(*s)) s++;
    
    bool is_neg = (*s == '-');
    
    char *internal_end;
    unsigned long long uval = strtoull(nptr, &internal_end, base);
    
    if (endptr) *endptr = internal_end;

    unsigned long long abs_max = is_neg ? ((unsigned long long)LLONG_MAX + 1) : LLONG_MAX;
    
    if (uval > abs_max) {
        return is_neg ? LLONG_MIN : LLONG_MAX;
    }
    
    return is_neg ? -(long long)uval : (long long)uval;
}

int atoi(const char *str) {
    return (int)strtoll(str, NULL, 10);
}

long atol(const char *str) {
    return (long)strtoll(str, NULL, 10);
}

long long atoll(const char *str) {
    return (long long)strtoll(str, NULL, 10);
}

static void reverse(char *str, int length) {
    int start = 0;
    int end = length - 1;
    while (start < end) {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
        start++;
        end--;
    }
}

char* ulltoa(unsigned long long value, char* str, int base) {
    if (base < 2 || base > 36) {
        *str = '\0';
        return str;
    }

    int i = 0;

    if (value == 0) {
        str[i++] = '0';
        str[i] = '\0';
        return str;
    }

    while (value != 0) {
        unsigned long long rem = value % base;
        str[i++] = (rem > 9) ? (char)((rem - 10) + 'a') : (char)(rem + '0');
        value = value / base;
    }

    str[i] = '\0';

    reverse(str, i);

    return str;
}

char* utoa(unsigned int value, char* str, int base) {
    return ulltoa(value, str, base);
}

char* ultoa(unsigned long value, char* str, int base) {
    return ulltoa(value, str, base);
}

static char* signed_toa(long long value, char* str, int base) {
    int i = 0;
    bool isNegative = false;
    unsigned long long uvalue;

    if (value < 0 && base == 10) {
        isNegative = true;
        uvalue = (unsigned long long)(~value + 1);
    } else {
        uvalue = (unsigned long long)value;
    }

    if (uvalue == 0) {
        str[i++] = '0';
        str[i] = '\0';
        return str;
    }

    while (uvalue != 0) {
        unsigned long long rem = uvalue % base;
        str[i++] = (rem > 9) ? (char)((rem - 10) + 'a') : (char)(rem + '0');
        uvalue = uvalue / base;
    }

    if (isNegative) {
        str[i++] = '-';
    }

    str[i] = '\0';
    reverse(str, i);

    return str;
}

char* itoa(int value, char* str, int base) {
    return signed_toa((long long)value, str, base);
}

char* ltoa(long value, char* str, int base) {
    return signed_toa((long long)value, str, base);
}

char* lltoa(long long value, char* str, int base) {
    return signed_toa(value, str, base);
}