// Force-included into pcrio.c on non-MSVC compilers (see Genieutils.cmake).
// pcrio calls the MSVC "secure" CRT functions, which glibc doesn't provide.
#pragma once

#include <errno.h>
#include <stdio.h>
#include <string.h>

static inline int fopen_s(FILE **file, const char *filename, const char *mode)
{
    *file = fopen(filename, mode);
    return *file ? 0 : errno;
}

// Copies at most `count` chars and always null-terminates, truncating to fit
// `destsz` (MSVC would abort instead of truncating).
static inline int strncpy_s(char *dest, size_t destsz, const char *src, size_t count)
{
    if (destsz == 0)
        return EINVAL;
    size_t len = strnlen(src, count);
    if (len >= destsz)
        len = destsz - 1;
    memcpy(dest, src, len);
    dest[len] = '\0';
    return 0;
}

static inline int strcpy_s(char *dest, size_t destsz, const char *src)
{
    return strncpy_s(dest, destsz, src, destsz);
}
