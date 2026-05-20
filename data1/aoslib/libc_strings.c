#include <strings.h>
#include <ctype.h>

int strcasecmp(const char *s1, const char *s2) {
    while (*s1 && (tolower((unsigned char)*s1) == tolower((unsigned char)*s2))) {
        s1++;
        s2++;
    }
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}

int strncasecmp(const char *s1, const char *s2, size_t n) {
    if (n == 0) return 0;
    while (n-- > 0 && *s1 && (tolower((unsigned char)*s1) == tolower((unsigned char)*s2))) {
        if (*s1 == '\0') return 0;
        s1++;
        s2++;
    }
    return (n == (size_t)-1) ? 0 : (tolower((unsigned char)*s1) - tolower((unsigned char)*s2));
}

void bzero(void *s, size_t n) {
    memset(s, 0, n);
}

void bcopy(const void *src, void *dest, size_t n) {
    memmove(dest, src, n);
}

int bcmp(const void *s1, const void *s2, size_t n) {
    return memcmp(s1, s2, n);
}