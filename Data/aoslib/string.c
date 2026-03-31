#include <stdint.h>
#define AOSLIB_SYSCALLS
#define AOSLIB_STRING
#include "../include/aoslib.h"

void* memset(void* ptr, uint8_t value, uint64_t n) {
    uint64_t pattern = (uint64_t)value;
    pattern |= pattern << 8;
    pattern |= pattern << 16;
    pattern |= pattern << 32;
    uint64_t* ptr_64 = (uint64_t*)ptr;
    uint64_t chunks = n / 8;
    uint64_t remainder = n % 8;
    while (chunks--) {
        *ptr_64++ = pattern;
    }
    uint8_t* ptr_8 = (uint8_t*)ptr_64;
    while (remainder--) {
        *ptr_8++ = value;
    }
    return ptr;
}

void* memcpy(void* dest, const void* src, uint64_t n) {
    uint64_t n1 = n / 8;
    uint64_t n2 = n % 8;
    uint64_t* d1 = (uint64_t*)dest;
    const uint64_t* s1 = (const uint64_t*)src;
    while (n1--) {
        *d1++ = *s1++;
    }
    uint8_t* d2 = (uint8_t*)d1;
    const uint8_t* s2 = (const uint8_t*)s1;
    while (n2--) {
        *d2++ = *s2++;
    }
    return dest;
}

int32_t strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const uint8_t*)s1 - *(const uint8_t*)s2;
}

int32_t strncmp(const char* s1, const char* s2, uint64_t n) {
    for (uint64_t i = 0; i < n; i++) {
        if (s1[i] != s2[i] || s1[i] == '\0')
            return (uint8_t)s1[i] - (uint8_t)s2[i];
    }
    return 0;
}

void* memmove(void* dest, const void* src, uint64_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    if (d < s) {
        for (uint64_t i = 0; i < n; i++) d[i] = s[i];
    } else {
        for (uint64_t i = n; i > 0; i--) d[i-1] = s[i-1];
    }
    return dest;
}

int memcmp(const void* s1, const void* s2, uint64_t n) {
    const uint8_t* a = (const uint8_t*)s1;
    const uint8_t* b = (const uint8_t*)s2;
    for (uint64_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return a[i] - b[i];
    }
    return 0;
}

char *strcpy(char *dest, const char *src) {
    char *start = dest;
    while ((*dest++ = *src++));
    return start;
}

char *strncpy(char *dest, const char *src, uint64_t n) {
    char *start = dest;
    while (n > 0 && *src != '\0') {
        *dest++ = *src++;
        n--;
    }
    while (n > 0) {
        *dest++ = '\0';
        n--;
    }
    return start;
}

uint64_t strlcpy(char *dest, const char *src, uint64_t size) {
    if (src == (void*)0) {
        if (size > 0 && dest != (void*)0) dest[0] = '\0';
        return 0;
    }
    if (dest == (void*)0) return 0;
    uint64_t src_len = 0;
    const char *s = src;
    while (*s++) src_len++;
    if (size != 0) {
        uint64_t copy_len = (src_len >= size) ? (size - 1) : src_len;
        for (uint64_t i = 0; i < copy_len; i++) {
            dest[i] = src[i];
        }
        dest[copy_len] = '\0';
    }
    return src_len; 
}

char* strdup(const char* s) {
    if (s == (void*)0) {
        return (void*)0;
    }
    uint64_t len = 0;
    while (s[len] != '\0') {
        len++;
    }
    char* copy = (char*)malloc(len + 1);
    if (copy == (void*)0) {
        return (void*)0;
    }
    for (uint64_t i = 0; i <= len; i++) {
        copy[i] = s[i];
    }
    return copy;
}

static int32_t is_delim(char c, const char* delim) {
    if (delim == (void*)0) {
        return 0;
    }
    
    while (*delim != '\0') {
        if (c == *delim) {
            return 1;
        }
        delim++;
    }
    return 0;
}

char* strtok_r(char* str, const char* delim, char** saveptr) {
    char* token;

    if (str == (void*)0) {
        str = *saveptr;
    }

    if (str == (void*)0) {
        return (void*)0;
    }

    while (*str != '\0' && is_delim(*str, delim)) {
        str++;
    }

    if (*str == '\0') {
        *saveptr = str;
        return (void*)0;
    }
	
    token = str;

    while (*str != '\0' && !is_delim(*str, delim)) {
        str++;
    }
	
    if (*str != '\0') {
        *str = '\0';
        *saveptr = str + 1;
    } else {
        *saveptr = str;
    }

    return token;
}

static __thread char* tls_saveptr = (void*)0;
char* strtok(char* str, const char* delim) {
    return strtok_r(str, delim, &tls_saveptr);
}

char* strsep(char** stringp, const char* delim) {
    if (stringp == (void*)0) {
        return (void*)0;
    }

    char* begin = *stringp;
    char* end;
    
    if (begin == (void*)0) {
        return (void*)0;
    }
    
    end = begin;
    while (*end != '\0' && !is_delim(*end, delim)) {
        end++;
    }
    
    if (*end != '\0') {
        *end = '\0';
        *stringp = end + 1;
    } else {
        *stringp = (void*)0;
    }

    return begin;
}

char* strchr(const char* s, int32_t c) {
    while (*s != (char)c) {
        if (*s == '\0') {
            return (void*)0;
        }
        s++;
    }
    return (char*)s;
}

char* strrchr(const char* s, int32_t c) {
    const char* last_occurrence = (void*)0;
    do {
        if (*s == (char)c) {
            last_occurrence = s;
        }
    } while (*s++);
    return (char*)last_occurrence;
}

char* strnchr(const char* s, uint64_t count, int32_t c) {
    while (count--) {
        if (*s == (char)c) {
            return (char*)s;
        }
        if (*s == '\0') {
            break;
        }
        s++;
    }
    return (void*)0;
}

char* strstr(const char* haystack, const char* needle) {
    uint64_t nlen = 0;
    while (needle[nlen] != '\0') nlen++;
    if (nlen == 0) {
        return (char*)haystack;
    }
    while (*haystack != '\0') {
        uint64_t i = 0;
        while (haystack[i] == needle[i] && needle[i] != '\0') {
            i++;
        }
        if (needle[i] == '\0') {
            return (char*)haystack;
        }
        haystack++;
    }
    
    return (void*)0;
}

uint64_t strlen(const char* s) {
    uint64_t len = 0;
    while (s[len] != '\0') {
        len++;
    }
    return len;
}

uint64_t strnlen(const char* s, uint64_t maxlen) {
    uint64_t len = 0;
    while (len < maxlen && s[len] != '\0') {
        len++;
    }
    return len;
}

char* strcat(char* dest, const char* src) {
    char* d = dest;
    while (*d != '\0') {
        d++;
    }
    while (*src != '\0') {
        *d = *src;
        d++;
        src++;
    }
    *d = '\0';
    return dest;
}

char* strncat(char* dest, const char* src, uint64_t n) {
    char* d = dest;
    while (*d != '\0') {
        d++;
    }
    while (n > 0 && *src != '\0') {
        *d = *src;
        d++;
        src++;
        n--;
    }
    *d = '\0';
    return dest;
}

uint64_t strlcat(char* dest, const char* src, uint64_t size) {
    uint64_t dest_len = 0;
    uint64_t src_len = 0;
    while (src[src_len] != '\0') {
        src_len++;
    }
    while (dest_len < size && dest[dest_len] != '\0') {
        dest_len++;
    }
    if (dest_len == size) {
        return size + src_len;
    }
    uint64_t copy_len;
    if (src_len < (size - dest_len - 1)) {
        copy_len = src_len;
    } else {
        copy_len = size - dest_len - 1;
    }
    for (uint64_t i = 0; i < copy_len; i++) {
        dest[dest_len + i] = src[i];
    }
    dest[dest_len + copy_len] = '\0';
    return dest_len + src_len;
}

int32_t isdigit(int32_t c) {
    return (c >= '0' && c <= '9');
}

int32_t islower(int32_t c) {
    return (c >= 'a' && c <= 'z');
}

int32_t isupper(int32_t c) {
    return (c >= 'A' && c <= 'Z');
}

int32_t isalpha(int32_t c) {
    return islower(c) || isupper(c);
}

int32_t isalnum(int32_t c) {
    return isalpha(c) || isdigit(c);
}

int32_t isxdigit(int32_t c) {
    return isdigit(c) || 
           (c >= 'a' && c <= 'f') || 
           (c >= 'A' && c <= 'F');
}

int32_t isspace(int32_t c) {
    return (c == ' ' || c == '\t' || c == '\n' || 
            c == '\r' || c == '\v' || c == '\f');
}

int32_t isprint(int32_t c) {
    return (c >= 0x20 && c <= 0x7E);
}

int32_t iscntrl(int32_t c) {
    return (c >= 0x00 && c <= 0x1F) || (c == 0x7F);
}

int32_t ispunct(int32_t c) {
    return isprint(c) && !isalnum(c) && !isspace(c);
}

int32_t tolower(int32_t c) {
    if (isupper(c)) {
        return c + ('a' - 'A');
    }
    return c;
}

int32_t toupper(int32_t c) {
    if (islower(c)) {
        return c - ('a' - 'A');
    }
    return c;
}

int32_t is_digit(const char* str) {
    if (str == (void*)0 || *str == '\0') {
        return 0;
    }
    if (*str == '-') {
        str++;
        if (*str == '\0') return 0;
    }
    while (*str != '\0') {
        if (!isdigit(*str)) {
            return 0;
        }
        str++;
    }
    return 1;
}

char* to_upper(char* s) {
    char* start = s;
    while (*s != '\0') {
        if (*s >= 'a' && *s <= 'z') {
            *s -= ('a' - 'A');
        }
        s++;
    }
    return start;
}

#define va_list            __builtin_va_list
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_end(ap)         __builtin_va_end(ap)

#define PRINTF_BUF_SIZE 256

typedef struct {
    char*    dest_buf;
    uint64_t capacity;
    uint64_t idx;
    int32_t  total_chars;
    int32_t  is_syscall;
} PrintContext;

static void putc_ctx(PrintContext* ctx, char c) {
    ctx->total_chars++;
    if (ctx->is_syscall) {
        ctx->dest_buf[ctx->idx++] = c;
        if (ctx->idx >= ctx->capacity - 1) {
            ctx->dest_buf[ctx->idx] = '\0';
            sysprint(ctx->dest_buf);
            ctx->idx = 0;
        }
    } else {
        if (ctx->capacity > 0 && ctx->idx < ctx->capacity - 1) {
            ctx->dest_buf[ctx->idx++] = c;
        }
    }
}

static void print_number(PrintContext* ctx, uint64_t val, int32_t base, int32_t is_signed, int32_t upper, int32_t width, char pad_char) {
    char temp[65]; // Буфер для ulltoa (до 64 бит в base=2 + '\0')
    int32_t is_neg = 0;

    if (is_signed && base == 10) {
        if ((int64_t)val < 0) {
            is_neg = 1;
            val = ~(uint64_t)val + 1;
        }
    }

    ulltoa(val, temp, base);

    int32_t len = 0;
    while (temp[len] != '\0') {
        if (upper && temp[len] >= 'a' && temp[len] <= 'z') {
            temp[len] = temp[len] - 'a' + 'A'; 
        }
        len++;
    }

    int32_t pad_len = width - len - is_neg;

    if (pad_char == '0' && is_neg) { 
        putc_ctx(ctx, '-'); 
        is_neg = 0;
    }

    while (pad_len-- > 0) {
        putc_ctx(ctx, pad_char);
    }

    if (is_neg) {
        putc_ctx(ctx, '-');
    }

    for (int32_t i = 0; i < len; i++) {
        putc_ctx(ctx, temp[i]);
    }
}

static void format_core(PrintContext* ctx, const char* format, va_list* args) {
    while (*format) {
        if (*format != '%') {
            putc_ctx(ctx, *format++);
            continue;
        }
        format++;
        if (*format == '\0') break;
        char pad_char = ' ';
        if (*format == '0') { pad_char = '0'; format++; }
        int32_t width = 0;
        while (*format >= '0' && *format <= '9') {
            width = width * 10 + (*format - '0');
            format++;
        }
        int32_t is_long = 0;
        if (*format == 'l') {
            is_long = 1; format++;
            if (*format == 'l') { is_long = 2; format++; }
        }
        switch (*format) {
            case 'c': putc_ctx(ctx, (char)va_arg(*args, int32_t)); break;
            case 's': {
                const char* s = va_arg(*args, const char*);
                if (s == (void*)0) s = "(null)";
                while (*s) putc_ctx(ctx, *s++);
                break;
            }
            case 'd':
            case 'i': {
                int64_t num = (is_long >= 1) ? va_arg(*args, int64_t) : va_arg(*args, int32_t);
                print_number(ctx, (uint64_t)num, 10, 1, 0, width, pad_char); break;
            }
            case 'u': {
                uint64_t num = (is_long >= 1) ? va_arg(*args, uint64_t) : va_arg(*args, uint32_t);
                print_number(ctx, num, 10, 0, 0, width, pad_char); break;
            }
            case 'x': 
            case 'X': {
                uint64_t num = (is_long >= 1) ? va_arg(*args, uint64_t) : va_arg(*args, uint32_t);
                print_number(ctx, num, 16, 0, (*format == 'X'), width, pad_char); break;
            }
            case 'p': {
                putc_ctx(ctx, '0'); putc_ctx(ctx, 'x');
                print_number(ctx, (uint64_t)va_arg(*args, void*), 16, 0, 0, 16, '0'); break;
            }
            case '%': putc_ctx(ctx, '%'); break;
            default:  putc_ctx(ctx, '%'); putc_ctx(ctx, *format); break;
        }
        format++;
    }
}

int32_t printf(const char* format, ...) {
    char local_buf[PRINTF_BUF_SIZE];
    PrintContext ctx = { local_buf, PRINTF_BUF_SIZE, 0, 0, 1 };
    va_list args;
    va_start(args, format);
    format_core(&ctx, format, &args);
    va_end(args);
    if (ctx.idx > 0) {
        ctx.dest_buf[ctx.idx] = '\0';
        sysprint(ctx.dest_buf);
    }
    return ctx.total_chars;
}

int32_t snprintf(char* str, uint64_t size, const char* format, ...) {
    PrintContext ctx = { str, size, 0, 0, 0 };
    va_list args;
    va_start(args, format);
    format_core(&ctx, format, &args);
    va_end(args);
    if (size > 0) {
        ctx.dest_buf[ctx.idx] = '\0';
    }
    return ctx.total_chars;
}

int32_t sprintf(char* str, const char* format, ...) {
    uint64_t infinite_size = (uint64_t)-1;
    PrintContext ctx = { str, infinite_size, 0, 0, 0 };
    
    va_list args;
    va_start(args, format);
    format_core(&ctx, format, &args);
    va_end(args);

    ctx.dest_buf[ctx.idx] = '\0';
    return ctx.total_chars;
}

#undef va_list
#undef va_start
#undef va_arg
#undef va_end

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

int kstrtoull(const char *s, int base, unsigned long long *res) {
    char *endptr;
    
    const char *check_sign = s;
    while (isspace(*check_sign)) check_sign++;
    if (*check_sign == '-') {
        return SYS_RES_INVALID; 
    }

    unsigned long long val = strtoull(s, &endptr, base);

    if (endptr == s || !is_clean_tail(endptr)) {
        return SYS_RES_INVALID;
    }

    if (val == ULLONG_MAX) {
        return SYS_RES_RANGE;
    }

    *res = val;
    return SYS_RES_OK;
}

int kstrtoll(const char *s, int base, long long *res) {
    char *endptr;
    long long val = strtoll(s, &endptr, base);

    if (endptr == s || !is_clean_tail(endptr)) {
        return SYS_RES_INVALID;
    }

    if (val == LLONG_MAX || val == LLONG_MIN) {
        return SYS_RES_RANGE;
    }

    *res = val;
    return SYS_RES_OK;
}

int kstrtoint(const char *s, int base, int *res) {
    long long val;
    int err = kstrtoll(s, base, &val);
    
    if (err != SYS_RES_OK) {
        return err;
    }

    if (val < INT_MIN || val > INT_MAX) {
        return SYS_RES_RANGE;
    }

    *res = (int)val;
    return SYS_RES_OK;
}

int kstrtobool(const char *s, bool *res) {
    while (isspace(*s)) s++;

    switch (*s) {
        case '1':
            *res = true;
            return SYS_RES_OK;
        case '0':
            *res = false;
            return SYS_RES_OK;
        case 'y': case 'Y':
            *res = true;
            return SYS_RES_OK;
        case 'n': case 'N':
            *res = false;
            return SYS_RES_OK;
        case 't': case 'T':
            if ((s[1] == 'r' || s[1] == 'R') && (s[2] == 'u' || s[2] == 'U') && (s[3] == 'e' || s[3] == 'E')) {
                *res = true; return SYS_RES_OK;
            }
            break;
        case 'f': case 'F':
            if ((s[1] == 'a' || s[1] == 'A') && (s[2] == 'l' || s[2] == 'L') && 
                (s[3] == 's' || s[3] == 'S') && (s[4] == 'e' || s[4] == 'E')) {
                *res = false; return SYS_RES_OK;
            }
            break;
        case 'o': case 'O':
            if (s[1] == 'n' || s[1] == 'N') { *res = true; return SYS_RES_OK; }
            if ((s[1] == 'f' || s[1] == 'F') && (s[2] == 'f' || s[2] == 'F')) { *res = false; return SYS_RES_OK; }
            break;
    }

    return SYS_RES_INVALID;
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