/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   tm_money.h                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: tracker_loot                                   +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/02/12                                #+#    #+#             */
/*   Updated: 2026/02/12                                ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef TM_MONEY_H
# define TM_MONEY_H

/*
** Fixed-point money helpers for Entropia Universe (RCE).
**
** - We store PED in 1/10,000 PED units (0.0001 PED = 0.01 PEC).
** - This keeps all sums deterministic and avoids cumulative floating errors.
*/

# include <stdint.h>
# include <stddef.h>

typedef int64_t				tm_money_t;

/* 1 PED = 10000 units */
# define TM_MONEY_SCALE		((tm_money_t)10000)

/* Parse/format */
int			tm_money_parse_ped(const char *s, tm_money_t *out);
tm_money_t	tm_money_from_ped_double(double ped);
double		tm_money_to_ped_double(tm_money_t v);

/* Formatting helpers (always NUL-terminated if cap>0) */
void		tm_money_format_ped4(char *dst, size_t cap, tm_money_t v);
void		tm_money_format_ped2(char *dst, size_t cap, tm_money_t v);

/* Conversions */
long long	tm_money_to_pec_round(tm_money_t v);

/* Arithmetic */
tm_money_t	tm_money_add(tm_money_t a, tm_money_t b);
tm_money_t	tm_money_sub(tm_money_t a, tm_money_t b);

/*
** Multiply by a MU multiplier stored as 1e4 fixed-point.
** Examples:
**   mu_mul_1e4 = 10000  => x1.0000 (TT)
**   mu_mul_1e4 = 10250  => x1.0250 (102.5%)
**   mu_mul_1e4 = 453570 => x45.3570
*/
tm_money_t	tm_money_mul_mu(tm_money_t base, int64_t mu_mul_1e4);

#endif
