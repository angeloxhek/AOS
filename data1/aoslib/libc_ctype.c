#include <stddef.h>
#include "../include/aoslib.h"

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