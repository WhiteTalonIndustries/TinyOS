#ifndef TINYOS_STRING_H
#define TINYOS_STRING_H

#ifndef NULL
#define NULL ((void *)0)
#endif

static inline unsigned long strlen(const char *s) {
    const char *p = s;
    while (*p) p++;
    return (unsigned long)(p - s);
}

static inline int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

#endif
