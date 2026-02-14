/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   tm_string.c                                       :+:      :+:    :+:    */
/*                                                    +:+ +:+         +:+     */
/*   By: login <login@student.42.fr>                +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/01/31 00:00:00 by login             #+#    #+#             */
/*   Updated: 2026/01/31 00:00:00 by login            ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "tm_string.h"

#include <stdio.h>
#include <string.h>

size_t  tm_strlcpy(char *dst, const char *src, size_t dstsz)
{
    size_t i;
    size_t slen;
    
    if (!src)
        src = "";
    slen = 0;
    while (src[slen])
        slen++;
    
    if (!dst || dstsz == 0)
        return (slen);
    
    i = 0;
    while (src[i] && i + 1 < dstsz)
    {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    return (slen);
}

void    safe_copy(char *dst, size_t dstsz, const char *src)
{
    (void)tm_strlcpy(dst, src, dstsz);
}

void    tm_chomp_crlf(char *s)
{
    size_t n;
    
    if (!s)
        return;
    
    n = 0;
    while (s[n])
        n++;
    
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r'))
    {
        s[n - 1] = '\0';
        n--;
    }
}
