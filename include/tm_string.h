#ifndef TM_STRING_H
# define TM_STRING_H

#include <stddef.h>

size_t  tm_strlcpy(char *dst, const char *src, size_t dstsz);
void    safe_copy(char *dst, size_t dstsz, const char *src);
void    tm_chomp_crlf(char *s);

#endif