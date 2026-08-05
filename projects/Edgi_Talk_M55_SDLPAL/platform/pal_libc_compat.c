#include "pal_libc_compat.h"

#include <stdlib.h>
#include <string.h>

static unsigned char pal_ascii_lower(unsigned char character)
{
    if (character >= (unsigned char)'A' && character <= (unsigned char)'Z') {
        return (unsigned char)(character + ('a' - 'A'));
    }
    return character;
}

char *pal_strdup(const char *text)
{
    size_t length;
    char *copy;

    if (text == NULL) {
        return NULL;
    }
    length = strlen(text) + 1u;
    copy = (char *)malloc(length);
    if (copy != NULL) {
        memcpy(copy, text, length);
    }
    return copy;
}

size_t pal_strnlen(const char *text, size_t maximum)
{
    size_t length = 0u;

    if (text == NULL) {
        return 0u;
    }
    while (length < maximum && text[length] != '\0') {
        ++length;
    }
    return length;
}

char *pal_strcasestr(const char *text, const char *needle)
{
    const char *candidate;

    if (text == NULL || needle == NULL) {
        return NULL;
    }
    if (*needle == '\0') {
        return (char *)text;
    }

    for (candidate = text; *candidate != '\0'; ++candidate) {
        size_t offset = 0u;

        while (needle[offset] != '\0' && candidate[offset] != '\0' &&
               pal_ascii_lower((unsigned char)candidate[offset]) ==
                   pal_ascii_lower((unsigned char)needle[offset])) {
            ++offset;
        }
        if (needle[offset] == '\0') {
            return (char *)candidate;
        }
    }
    return NULL;
}
