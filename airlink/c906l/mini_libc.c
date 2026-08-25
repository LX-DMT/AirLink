#include <stddef.h>
#include <stdint.h>

void *memset(void *dst, int value, size_t length)
{
    uint8_t *out = (uint8_t *)dst;
    while (length-- != 0U) *out++ = (uint8_t)value;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t length)
{
    uint8_t *out = (uint8_t *)dst;
    const uint8_t *in = (const uint8_t *)src;
    while (length-- != 0U) *out++ = *in++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t length)
{
    uint8_t *out = (uint8_t *)dst;
    const uint8_t *in = (const uint8_t *)src;
    if (out < in) while (length-- != 0U) *out++ = *in++;
    else { out += length; in += length; while (length-- != 0U) *--out = *--in; }
    return dst;
}

int memcmp(const void *a, const void *b, size_t length)
{
    const uint8_t *x = (const uint8_t *)a;
    const uint8_t *y = (const uint8_t *)b;
    while (length-- != 0U) { if (*x != *y) return (int)*x - (int)*y; ++x; ++y; }
    return 0;
}

size_t strlen(const char *s) { const char *p = s; while (*p) ++p; return (size_t)(p - s); }
size_t strnlen(const char *s, size_t n) { size_t i = 0U; while (i < n && s[i]) ++i; return i; }
int strcmp(const char *a, const char *b) { while (*a && *a == *b) { ++a; ++b; } return (unsigned char)*a - (unsigned char)*b; }
int strncmp(const char *a, const char *b, size_t n) { while (n && *a && *a == *b) { ++a; ++b; --n; } return n ? (unsigned char)*a - (unsigned char)*b : 0; }
char *strcpy(char *d, const char *s) { char *r=d; while ((*d++=*s++) != 0) {} return r; }
char *strncpy(char *d, const char *s, size_t n) { char *r=d; while (n && *s) { *d++=*s++; --n; } while (n--) *d++=0; return r; }
int ffs(int value)
{
    if (value == 0) return 0;
    int bit = 1;
    while ((value & 1) == 0) { value >>= 1; ++bit; }
    return bit;
}

const char *strchr(const char *s, int c)
{
    while (*s) {
        if ((unsigned char)*s == (unsigned char)c) return s;
        ++s;
    }
    return c == 0 ? s : (const char *)0;
}
