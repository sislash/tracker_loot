/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   sessions_catalog.c                                  :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: tracker_loot <tracker_loot@student.42.fr>    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/02/12                                  #+#    #+#         */
/*   Updated: 2026/02/12                                  ###   ########.fr   */
/*                                                                            */
/* ************************************************************************** */

#include "sessions_catalog.h"
#include "csv.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void	zero_entry(t_session_entry *e)
{
	if (!e)
		return ;
	memset(e, 0, sizeof(*e));
	e->start_offset = 0;
	e->end_offset = -1;
	e->has_offsets = 0;
	e->mob[0] = '\0';
	e->has_mob = 0;
}

static void	str_copy(char *dst, size_t dstsz, const char *src)
{
	if (!dst || dstsz == 0)
		return ;
	if (!src)
	{
		dst[0] = '\0';
		return ;
	}
	snprintf(dst, dstsz, "%s", src);
}

static int	looks_like_header(const char *line)
{
	if (!line)
		return (0);
	return (strstr(line, "session_start") && strstr(line, "session_end"));
}

static int	parse_long_safe(const char *s, long *out)
{
	char	*end;
	long	v;

	if (out)
		*out = 0;
	if (!s || !*s || !out)
		return (0);
	end = NULL;
	v = strtol(s, &end, 10);
	if (end == s)
		return (0);
	*out = v;
	return (1);
}

static int	parse_double_safe(const char *s, double *out)
{
	char	*end;
	double	v;

	if (out)
		*out = 0.0;
	if (!s || !*s || !out)
		return (0);
	end = NULL;
	v = strtod(s, &end);
	if (end == s)
		return (0);
	*out = v;
	return (1);
}

static int	parse_session_line(const char *line, t_session_entry *out)
{
	char	buf[1024];
	char	*fields[16];
	int		n;

	if (!line || !out)
		return (0);
	if (looks_like_header(line))
		return (0);
	str_copy(buf, sizeof(buf), line);
	n = csv_split_n(buf, fields, 16);
	if (n < 2)
		return (0);
	zero_entry(out);
	str_copy(out->start_ts, sizeof(out->start_ts), fields[0]);
	str_copy(out->end_ts, sizeof(out->end_ts), (n > 1) ? fields[1] : "");
	str_copy(out->weapon, sizeof(out->weapon), (n > 2) ? fields[2] : "");
	if (n > 3)
		parse_long_safe(fields[3], &out->kills);
	if (n > 4)
		parse_long_safe(fields[4], &out->shots);
	if (n > 5)
		parse_double_safe(fields[5], &out->loot_ped);
	if (n > 6)
		parse_double_safe(fields[6], &out->expense_ped);
	if (n > 7)
		parse_double_safe(fields[7], &out->net_ped);
	if (n > 8)
		parse_double_safe(fields[8], &out->return_pct);
	/* Optional v2: start_offset,end_offset at the end */
	if (n > 10)
	{
		long	s;
		long	e;
		if (parse_long_safe(fields[9], &s) && parse_long_safe(fields[10], &e))
		{
			out->start_offset = s;
			out->end_offset = e;
			out->has_offsets = 1;
		}
	}
	/* Optional v3: mob (after offsets) */
	if (n > 11)
	{
		str_copy(out->mob, sizeof(out->mob), fields[11]);
		out->has_mob = (out->mob[0] != '\0');
	}
	return (1);
}

int	sessions_list_load(const char *csv_path, size_t max_items, t_sessions_list *out)
{
	FILE			*f;
	char			line[2048];
	t_session_entry	tmp;
	t_session_entry	*ring;
	size_t			cap;
	size_t			count;
	size_t			idx;

	if (out)
	{
		out->items = NULL;
		out->count = 0;
	}
	if (!csv_path || !out)
		return (-1);
	f = fopen(csv_path, "rb");
	if (!f)
		return (-1);
	cap = (max_items == 0) ? 256 : max_items;
	if (cap > 2048)
		cap = 2048;
	ring = (t_session_entry *)calloc(cap, sizeof(*ring));
	if (!ring)
	{
		fclose(f);
		return (-1);
	}
	count = 0;
	idx = 0;
	while (fgets(line, (int)sizeof(line), f))
	{
		if (!parse_session_line(line, &tmp))
			continue ;
		ring[idx] = tmp;
		idx = (idx + 1) % cap;
		if (count < cap)
			count++;
	}
	fclose(f);
	if (count == 0)
	{
		free(ring);
		return (0);
	}
	out->items = (t_session_entry *)calloc(count, sizeof(*out->items));
	if (!out->items)
	{
		free(ring);
		return (-1);
	}
	out->count = count;
	/* oldest->newest order in output */
	{
		size_t	start;
		size_t	i;

		start = (count == cap) ? idx : 0;
		i = 0;
		while (i < count)
		{
			size_t	src = (start + i) % cap;
			out->items[i] = ring[src];
			i++;
		}
	}
	free(ring);
	return (0);
}

void	sessions_list_free(t_sessions_list *l)
{
	if (!l)
		return ;
	free(l->items);
	l->items = NULL;
	l->count = 0;
}

int	sessions_format_label(const t_session_entry *e, char *out, size_t outsz)
{
	char	st[32];
	char	en[32];

	if (!out || outsz == 0)
		return (0);
	if (!e)
	{
		out[0] = '\0';
		return (0);
	}
	/* Compact timestamps for UI */
	str_copy(st, sizeof(st), e->start_ts);
	str_copy(en, sizeof(en), e->end_ts);
	if (strlen(st) > 19)
		st[19] = '\0';
	if (strlen(en) > 19)
		en[19] = '\0';
	if (e->has_offsets)
	{
		if (e->has_mob)
			snprintf(out, outsz, "%s -> %s  |  %s  |  Mob:%s  |  K:%ld  Loot:%.2f  (off %ld..%ld)",
				st, en, e->weapon, e->mob, e->kills, e->loot_ped, e->start_offset, e->end_offset);
		else
			snprintf(out, outsz, "%s -> %s  |  %s  |  K:%ld  Loot:%.2f  (off %ld..%ld)",
				st, en, e->weapon, e->kills, e->loot_ped, e->start_offset, e->end_offset);
	}
	else
	{
		if (e->has_mob)
			snprintf(out, outsz, "%s -> %s  |  %s  |  Mob:%s  |  K:%ld  Loot:%.2f  (no offsets)",
				st, en, e->weapon, e->mob, e->kills, e->loot_ped);
		else
			snprintf(out, outsz, "%s -> %s  |  %s  |  K:%ld  Loot:%.2f  (no offsets)",
				st, en, e->weapon, e->kills, e->loot_ped);
	}
	return (1);
}
