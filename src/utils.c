/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   utils.c                                            :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: you <you@student.42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/02/04 00:00:00 by you               #+#    #+#             */
/*   Updated: 2026/02/04 00:00:00 by you              ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#define _POSIX_C_SOURCE 199309L

#include "utils.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

void	tm_zero(void *ptr, size_t n)
{
	if (!ptr || n == 0)
		return;
	memset(ptr, 0, n);
}

void	tm_trim_eol(char *s)
{
	size_t	n;

	if (!s)
		return;
	n = strlen(s);
	while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r'))
	{
		s[n - 1] = '\0';
		n--;
	}
}

/*
 * Parse a floating number.
 * Returns 1 on success, 0 on failure (to match legacy call sites).
 */
int	tm_parse_double(const char *s, double *out)
{
	char	buf[128];
	char	*end;
	double	v;
	size_t	i;
	size_t	j;

	if (!out)
		return (0);
	*out = 0.0;
	if (!s)
		return (0);
	while (*s && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r'))
		s++;
	if (!*s)
		return (0);
	i = 0;
	j = 0;
	while (s[i] && j + 1 < sizeof(buf))
	{
		if (s[i] == ',')
			buf[j++] = '.';
		else
			buf[j++] = s[i];
		i++;
	}
	buf[j] = '\0';
	v = strtod(buf, &end);
	if (end == buf)
		return (0);
	*out = v;
	return (1);
}

void	tm_fmt_linef(char *dst, size_t cap, const char *k,
			const char *fmt, ...)
{
	va_list	ap;
	char	buf[256];

	if (!dst || cap == 0)
		return;
	if (!k)
		k = "";
	buf[0] = '\0';
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	snprintf(dst, cap, "%s: %s", k, buf);
}

void	tm_free_str_key_array(void *arr, size_t len,
			size_t stride, size_t key_offset)
{
	size_t	i;
	char	**pkey;

	if (!arr)
		return;
	i = 0;
	while (i < len)
	{
		pkey = (char **)((char *)arr + i * stride + key_offset);
		free(*pkey);
		*pkey = NULL;
		i++;
	}
	free(arr);
}

#ifndef _WIN32

# include <time.h>

void	ft_sleep_ms(int ms)
{
    struct timespec	ts;
    
    if (ms <= 0)
        return ;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, 0);
}

uint64_t	ft_time_ms(void)
{
    struct timespec	ts;
    
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL);
}

#else

# include <windows.h>

void	ft_sleep_ms(int ms)
{
    if (ms <= 0)
        return ;
    Sleep((DWORD)ms);
}

uint64_t	ft_time_ms(void)
{
    return ((uint64_t)GetTickCount64());
}

#endif
