/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   tm_money.c                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: tracker_loot                                   +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/02/12                                #+#    #+#             */
/*   Updated: 2026/02/12                                ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "tm_money.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>

static int	is_space(int c)
{
	return (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f');
}

static const char	*skip_spaces(const char *s)
{
	while (s && *s && is_space((unsigned char)*s))
		s++;
	return (s);
}

int	tm_money_parse_ped(const char *s, tm_money_t *out)
{
	int				neg;
	uint64_t		ip;
	uint64_t		fp;
	int				fd;
	int				round_up;
	uint64_t		scaled;
	const char		*p;

	if (!out)
		return (0);
	*out = 0;
	if (!s)
		return (0);
	p = skip_spaces(s);
	neg = 0;
	if (*p == '+' || *p == '-')
	{
		neg = (*p == '-');
		p++;
	}
	ip = 0;
	if (!isdigit((unsigned char)*p) && *p != '.' && *p != ',')
		return (0);
	while (isdigit((unsigned char)*p))
	{
		ip = ip * 10u + (uint64_t)(*p - '0');
		p++;
	}
	fp = 0;
	fd = 0;
	round_up = 0;
	if (*p == '.' || *p == ',')
	{
		p++;
		while (isdigit((unsigned char)*p))
		{
			if (fd < 4)
			{
				fp = fp * 10u + (uint64_t)(*p - '0');
				fd++;
			}
			else if (fd == 4)
			{
				/* 5th decimal digit decides rounding */
				round_up = ((*p - '0') >= 5);
				fd++;
			}
			p++;
		}
	}
	/* scale fraction to 4 decimals */
	while (fd > 0 && fd < 4)
	{
		fp *= 10u;
		fd++;
	}
	if (round_up)
	{
		fp++;
		if (fp >= (uint64_t)TM_MONEY_SCALE)
		{
			fp -= (uint64_t)TM_MONEY_SCALE;
			ip++;
		}
	}
	scaled = ip * (uint64_t)TM_MONEY_SCALE + fp;
	*out = (tm_money_t)scaled;
	if (neg)
		*out = -*out;
	return (1);
}

tm_money_t	tm_money_from_ped_double(double ped)
{
	/* Round to nearest 0.0001 PED */
	return ((tm_money_t)llround(ped * (double)TM_MONEY_SCALE));
}

double	tm_money_to_ped_double(tm_money_t v)
{
	return ((double)v / (double)TM_MONEY_SCALE);
}

tm_money_t	tm_money_add(tm_money_t a, tm_money_t b)
{
	return (a + b);
}

tm_money_t	tm_money_sub(tm_money_t a, tm_money_t b)
{
	return (a - b);
}

static uint64_t	uabs64_i64(int64_t x)
{
	/* INT64_MIN cannot be negated in signed space */
	if (x == INT64_MIN)
		return ((uint64_t)INT64_MAX + 1ULL);
	return (x < 0) ? (uint64_t)(-x) : (uint64_t)x;
}

tm_money_t	tm_money_mul_mu(tm_money_t base, int64_t mu_mul_1e4)
{
	/*
	 * MSVC portability: avoid __int128.
	 * Compute: round( base * mu_mul_1e4 / 10000 ) in magnitude space.
	 *
	 * We decompose base = q*10000 + r, then:
	 *   base*mu/10000 = q*mu + (r*mu)/10000
	 * and round the second term half-up. All operations are overflow-checked.
	 */
	int		sign;
	uint64_t	ab;
	uint64_t	am;
	uint64_t	q;
	uint64_t	r;
	uint64_t	part1;
	uint64_t	num;
	uint64_t	part2;
	uint64_t	rem;
	uint64_t	res;

	if (mu_mul_1e4 == 0)
		return (0);
	sign = 1;
	if (base < 0)
		sign = -sign;
	if (mu_mul_1e4 < 0)
		sign = -sign;
	ab = uabs64_i64((int64_t)base);
	am = uabs64_i64((int64_t)mu_mul_1e4);

	q = ab / 10000ULL;
	r = ab % 10000ULL;

	/* part1 = q * am */
	if (q != 0 && am > UINT64_MAX / q)
		return ((sign > 0) ? (tm_money_t)INT64_MAX : (tm_money_t)INT64_MIN);
	part1 = q * am;

	/* part2 = round( (r * am) / 10000 ) */
	if (r != 0 && am > UINT64_MAX / r)
		return ((sign > 0) ? (tm_money_t)INT64_MAX : (tm_money_t)INT64_MIN);
	num = r * am;
	part2 = num / 10000ULL;
	rem = num % 10000ULL;
	if (rem >= 5000ULL)
		part2++;

	if (part1 > UINT64_MAX - part2)
		return ((sign > 0) ? (tm_money_t)INT64_MAX : (tm_money_t)INT64_MIN);
	res = part1 + part2;
	if (res > (uint64_t)INT64_MAX)
		return ((sign > 0) ? (tm_money_t)INT64_MAX : (tm_money_t)INT64_MIN);
	return ((sign > 0) ? (tm_money_t)res : (tm_money_t)-(int64_t)res);
}

long long	tm_money_to_pec_round(tm_money_t v)
{
	/* 1 PEC = 0.01 PED = 100 units */
	if (v >= 0)
		return ((long long)((v + 50) / 100));
	return ((long long)-(((-v) + 50) / 100));
}

static void	format_ped(char *dst, size_t cap, tm_money_t v, int decimals)
{
	char		buf[64];
	tm_money_t	abs_v;
	tm_money_t	ip;
	tm_money_t	frac;
	int			neg;
	long long	f;
	long long	scale;
	long long	round;

	if (!dst || cap == 0)
		return ;
	neg = (v < 0);
	abs_v = neg ? -v : v;
	if (decimals == 2)
	{
		scale = 100;
		round = 50;
		abs_v = (abs_v + round) / scale;
		ip = abs_v / 100;
		frac = abs_v % 100;
		snprintf(buf, sizeof(buf), "%s%lld.%02lld", neg ? "-" : "",
				 (long long)ip, (long long)frac);
	}
	else
	{
		ip = abs_v / TM_MONEY_SCALE;
		frac = abs_v % TM_MONEY_SCALE;
		f = (long long)frac;
		snprintf(buf, sizeof(buf), "%s%lld.%04lld", neg ? "-" : "",
				 (long long)ip, f);
	}
	snprintf(dst, cap, "%s", buf);
}

void	tm_money_format_ped4(char *dst, size_t cap, tm_money_t v)
{
	format_ped(dst, cap, v, 4);
}

void	tm_money_format_ped2(char *dst, size_t cap, tm_money_t v)
{
	format_ped(dst, cap, v, 2);
}
