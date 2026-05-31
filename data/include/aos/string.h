#ifndef AOS_STRING_H
#define AOS_STRING_H

#include "types.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct malloc_header {
    uint64_t size;
    uint64_t is_free;
    struct malloc_header* next;
    uint64_t padding;
} __attribute__((aligned(16))) malloc_header_t;

#ifdef AOSLIB_STRING
void* malloc(size_t size);
void free(void* ptr);
void* realloc(void* ptr, size_t new_size);
void* calloc(size_t num, size_t size);
void* syscall_sbrk(int64_t increment);

void* memset(void* ptr, int value, size_t n);
void* memcpy(void* dest, const void* src, size_t n);
void* memmove(void* dest, const void* src, size_t n);
int memcmp(const void* s1, const void* s2, size_t n);

int strcmp(const char* s1, const char* s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
size_t strlcpy(char *dest, const char *src, size_t n);
char* strdup(const char* s);
char* strtok_r(char* str, const char* delim, char** saveptr);
char* strtok(char* str, const char* delim);
char* strsep(char** stringp, const char* delim);
char* strchr(const char* s, int c);
char* strrchr(const char* s, int c);
char* strnchr(const char* s, size_t count, int c);
char* strstr(const char* haystack, const char* needle);
size_t strlen(const char* s);
size_t strnlen(const char* s, size_t maxlen);
char* strcat(char* dest, const char* src);
char* strncat(char* dest, const char* src, size_t n);
size_t strlcat(char* dest, const char* src, size_t size);
int strcasecmp(const char *s1, const char *s2);
int strncasecmp(const char *s1, const char *s2, size_t n);

void bcopy(const void *src, void *dest, size_t n);
void bzero(void *s, size_t n);
int  bcmp(const void *s1, const void *s2, size_t n);

int isdigit(int c);
int islower(int c);
int isupper(int c);
int isalpha(int c);
int isalnum(int c);
int isxdigit(int c);
int isspace(int c);
int isprint(int c);
int iscntrl(int c);
int ispunct(int c);

int tolower(int c);
int toupper(int c);
int is_digit(const char* str);
char* to_upper(char* s);

int printf(const char* format, ...);
int snprintf(char* str, size_t size, const char* format, ...);
int sprintf(char* str, const char* format, ...);

int atoi(const char *str);
long atol(const char *str);
long long atoll(const char *str);

unsigned long long strtoull(const char *nptr, char **endptr, int base);
long long strtoll(const char *nptr, char **endptr, int base);

int kstrtoull(const char *s, int base, unsigned long long *res);
int kstrtoll(const char *s, int base, long long *res);
int kstrtoint(const char *s, int base, int *res);
int kstrtobool(const char *s, bool *res);

char* itoa(int value, char* str, int base);
char* utoa(unsigned int value, char* str, int base);
char* ltoa(long value, char* str, int base);
char* ultoa(unsigned long value, char* str, int base);
char* lltoa(long long value, char* str, int base);
char* ulltoa(unsigned long long value, char* str, int base);
#endif

#ifdef __cplusplus
}
#endif
#endif // AOS_STRING_H