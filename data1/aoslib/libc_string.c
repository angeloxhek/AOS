#include <stddef.h>
#include "../include/aoslib.h"

void* memset(void* ptr, int value, size_t n) {
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

void* memcpy(void* dest, const void* src, size_t n) {
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

void* memmove(void* dest, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    if (d < s) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else {
        for (size_t i = n; i > 0; i--) d[i-1] = s[i-1];
    }
    return dest;
}

int memcmp(const void* s1, const void* s2, size_t n) {
    const uint8_t* a = (const uint8_t*)s1;
    const uint8_t* b = (const uint8_t*)s2;
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return a[i] - b[i];
    }
    return 0;
}

int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const uint8_t*)s1 - *(const uint8_t*)s2;
}

int strncmp(const char* s1, const char* s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i] || s1[i] == '\0')
            return (uint8_t)s1[i] - (uint8_t)s2[i];
    }
    return 0;
}

char *strcpy(char *dest, const char *src) {
    char *start = dest;
    while ((*dest++ = *src++));
    return start;
}

char *strncpy(char *dest, const char *src, size_t n) {
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

size_t strlcpy(char *dest, const char *src, size_t size) {
    if (src == (void*)0) {
        if (size > 0 && dest != (void*)0) dest[0] = '\0';
        return 0;
    }
    if (dest == (void*)0) return 0;
    size_t src_len = 0;
    const char *s = src;
    while (*s++) src_len++;
    if (size != 0) {
        size_t copy_len = (src_len >= size) ? (size - 1) : src_len;
        for (size_t i = 0; i < copy_len; i++) {
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
    size_t len = 0;
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

char* strchr(const char* s, int c) {
    while (*s != (char)c) {
        if (*s == '\0') {
            return (void*)0;
        }
        s++;
    }
    return (char*)s;
}

char* strrchr(const char* s, int c) {
    const char* last_occurrence = (void*)0;
    do {
        if (*s == (char)c) {
            last_occurrence = s;
        }
    } while (*s++);
    return (char*)last_occurrence;
}

size_t strlen(const char* s) {
    size_t len = 0;
    while (s[len] != '\0') {
        len++;
    }
    return len;
}

size_t strnlen(const char* s, size_t maxlen) {
    size_t len = 0;
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

char* strncat(char* dest, const char* src, size_t n) {
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

size_t strlcat(char* dest, const char* src, size_t size) {
    size_t dest_len = 0;
    size_t src_len = 0;
    while (src[src_len] != '\0') {
        src_len++;
    }
    while (dest_len < size && dest[dest_len] != '\0') {
        dest_len++;
    }
    if (dest_len == size) {
        return size + src_len;
    }
    size_t copy_len;
    if (src_len < (size - dest_len - 1)) {
        copy_len = src_len;
    } else {
        copy_len = size - dest_len - 1;
    }
    for (size_t i = 0; i < copy_len; i++) {
        dest[dest_len + i] = src[i];
    }
    dest[dest_len + copy_len] = '\0';
    return dest_len + src_len;
}

static int is_delim(char c, const char* delim) {
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

char* strstr(const char* haystack, const char* needle) {
    size_t nlen = 0;
    while (needle[nlen] != '\0') nlen++;
    if (nlen == 0) {
        return (char*)haystack;
    }
    while (*haystack != '\0') {
        size_t i = 0;
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