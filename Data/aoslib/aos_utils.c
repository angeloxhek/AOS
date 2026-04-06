#include <stddef.h>
#include "../include/aoslib.h"

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

int is_digit(const char* str) {
    if (str == (void*)0 || *str == '\0') {
        return 0;
    }
    if (*str == '-') {
        str++;
        if (*str == '\0') return 0;
    }
    while (*str != '\0') {
        if (*str < '0' || *str > '9') {
            return 0;
        }
        str++;
    }
    return 1;
}

char* strnchr(const char* s, size_t count, int c) {
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

static bool is_clean_tail(const char *endptr) {
    return (*endptr == '\0' || (*endptr == '\n' && *(endptr + 1) == '\0'));
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
            if ((s[1] == 'r' || s[1] == 'R') && 
                (s[2] == 'u' || s[2] == 'U') && 
                (s[3] == 'e' || s[3] == 'E')) {
                *res = true; return SYS_RES_OK;
            }
            break;
        case 'f': case 'F':
            if ((s[1] == 'a' || s[1] == 'A') && 
                (s[2] == 'l' || s[2] == 'L') && 
                (s[3] == 's' || s[3] == 'S') && 
                (s[4] == 'e' || s[4] == 'E')) {
                *res = false; return SYS_RES_OK;
            }
            break;
        case 'o': case 'O':
            if (s[1] == 'n' || s[1] == 'N') { 
                *res = true; return SYS_RES_OK; 
            }
            if ((s[1] == 'f' || s[1] == 'F') && 
                (s[2] == 'f' || s[2] == 'F')) { 
                *res = false; return SYS_RES_OK; 
            }
            break;
    }

    return SYS_RES_INVALID;
}