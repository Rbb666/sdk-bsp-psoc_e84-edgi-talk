#ifndef PAL_LIBC_COMPAT_H
#define PAL_LIBC_COMPAT_H

#include <stddef.h>

char *pal_strdup(const char *text);
size_t pal_strnlen(const char *text, size_t maximum);
char *pal_strcasestr(const char *text, const char *needle);

#endif
