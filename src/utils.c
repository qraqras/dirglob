#include <rbc/rbc.h>
#include "internal.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

char *rbc_strdup(const char *str)
{
    if (!str)
        return NULL;

    size_t len = strlen(str);
    char *dup = malloc(len + 1);
    if (!dup)
        return NULL;

    memcpy(dup, str, len + 1);
    return dup;
}

uint32_t rbc_next_codepoint(const char **p)
{
    const unsigned char *s = (const unsigned char *)*p;
    uint32_t c = *s;

    if (c == 0)
        return 0;

    if (c < 0x80)
    {
        *p += 1;
        return c;
    }

    // 2 bytes: 110xxxxx 10xxxxxx
    if ((c & 0xE0) == 0xC0)
    {
        if ((s[1] & 0xC0) == 0x80)
        {
            *p += 2;
            return ((c & 0x1F) << 6) | (s[1] & 0x3F);
        }
    }
    // 3 bytes: 1110xxxx 10xxxxxx 10xxxxxx
    else if ((c & 0xF0) == 0xE0)
    {
        if ((s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80)
        {
            *p += 3;
            return ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        }
    }
    // 4 bytes: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
    else if ((c & 0xF8) == 0xF0)
    {
        if ((s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80)
        {
            *p += 4;
            return ((c & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        }
    }

    // Invalid UTF-8 sequence, treat as raw byte
    *p += 1;
    return c;
}
