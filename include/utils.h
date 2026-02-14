/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   utils.h                                            :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: you <you@student.42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/02/04                                 #+#    #+#           */
/*   Updated: 2026/02/04                                 #+#    #+#           */
/*                                                                            */
/* ************************************************************************** */

#ifndef UTILS_H
# define UTILS_H

# include <stdint.h>
# include <stddef.h>

/* Generic helpers */
void		tm_zero(void *ptr, size_t n);
void		tm_trim_eol(char *s);
int		tm_parse_double(const char *s, double *out);
void		tm_fmt_linef(char *dst, size_t cap, const char *k,
				const char *fmt, ...);

/* Generic: free an array of structs that each contain a heap string key */
void		tm_free_str_key_array(void *arr, size_t len,
				size_t stride, size_t key_offset);

void		ft_sleep_ms(int ms);
uint64_t	ft_time_ms(void);

#endif

