#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pal_libc_compat.h"

int main(void)
{
    const char text[] = "Font Flavor=GB2312";
    char *copy = pal_strdup(text);

    assert(copy != NULL);
    assert(copy != text);
    assert(strcmp(copy, text) == 0);
    assert(pal_strnlen("abcd", 3u) == 3u);
    assert(pal_strnlen("ab", 8u) == 2u);
    assert(pal_strcasestr(text, "flavor") == text + 5);
    assert(pal_strcasestr(text, "gb2312") == text + 12);
    assert(pal_strcasestr(text, "big5") == NULL);

    free(copy);
    puts("libc_compat: PASS");
    return 0;
}
