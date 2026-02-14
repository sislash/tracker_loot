/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   hunt_series.c                                      :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: tracker_loot                                   +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/02/09                                #+#    #+#             */
/*   Updated: 2026/02/12                                ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "hunt_series.h"

#include "hunt_csv.h"
#include "fs_utils.h"
#include "utils.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/*  Loot filters (Graph LIVE only)                                            */
/* -------------------------------------------------------------------------- */

static int	str_ieq(const char *a, const char *b)
{
	unsigned char	ca;
	unsigned char	cb;

	if (a == b)
		return (1);
	if (!a || !b)
		return (0);
	while (*a && *b)
	{
		ca = (unsigned char)*a;
		cb = (unsigned char)*b;
		if (tolower(ca) != tolower(cb))
			return (0);
		a++;
		b++;
	}
	return (*a == '\0' && *b == '\0');
}

static int	str_n_ieq(const char *a, const char *b, size_t n)
{
	size_t			 i;
	unsigned char	ca;
	unsigned char	cb;

	if (!a || !b)
		return (0);
	i = 0;
	while (i < n)
	{
		ca = (unsigned char)a[i];
		cb = (unsigned char)b[i];
		if (ca == '\0' || cb == '\0')
			return (0);
		if (tolower(ca) != tolower(cb))
			return (0);
		i++;
	}
	return (1);
}

static int	str_icontains(const char *hay, const char *needle)
{
	size_t	nlen;
	size_t	i;

	if (!hay || !needle || !*needle)
		return (0);
	nlen = strlen(needle);
	i = 0;
	while (hay[i])
	{
		if (tolower((unsigned char)hay[i]) == tolower((unsigned char)needle[0]))
		{
			if (str_n_ieq(hay + i, needle, nlen))
				return (1);
		}
		i++;
	}
	return (0);
}

static void	name_trim_copy(char *dst, size_t cap, const char *src)
{
	const char	*p;
	const char	*end;
	size_t		o;
	int			in_space;

	if (!dst || cap == 0)
		return ;
	dst[0] = '\0';
	if (!src)
		return ;
	while (*src && isspace((unsigned char)*src))
		src++;
	end = src + strlen(src);
	while (end > src && (isspace((unsigned char)end[-1]) || end[-1] == '.'))
		end--;
	o = 0;
	in_space = 0;
	p = src;
	while (p < end && o + 1 < cap)
	{
		if (isspace((unsigned char)*p))
		{
			in_space = 1;
			p++;
			continue ;
		}
		if (in_space && o > 0 && o + 1 < cap)
			dst[o++] = ' ';
		in_space = 0;
		dst[o++] = *p;
		p++;
	}
	dst[o] = '\0';
}

static int	loot_name_excluded_for_graph(const char *type, const char *name)
{
	/*
	 * User request: exclude these from Loot graphs (Graph LIVE):
	 * Sopur, Nessit, Trutun, Kaldon, Brukite, Papplon, Bombardo, Haimoros, Caroot
	 * Daily Token, Combat Token, Fen Token Gold, Mayhem Token, Entropia Unreal Token
	 * Vibrant Sweat
	 */
	static const char *const	items_exact[] = {
		"Sopur",
		"Nessit",
		"Trutun",
		"Kaldon",
		"Brukite",
		"Papplon",
		"Bombardo",
		"Haimoros",
		"Caroot",
		"Vibrant Sweat",
		"Daily Token",
		"Combat Token",
		"Fen Token Gold",
		"Mayhem Token",
		"Entropia Unreal Token",
		"Common Dung",
		NULL
	};
	static const char *const	received_contains[] = {
		"Daily Token",
		"Combat Token",
		"Fen Token Gold",
		"Mayhem Token",
		"Entropia Unreal Token",
		"Vibrant Sweat",
		NULL
	};
	char					trimmed[256];
	int					i;

	if (!name || !*name)
		return (0);
	name_trim_copy(trimmed, sizeof(trimmed), name);
	if (!*trimmed)
		return (0);
	i = 0;
	while (items_exact[i])
	{
		if (str_ieq(trimmed, items_exact[i]))
			return (1);
		i++;
	}
	if (type && strcmp(type, "RECEIVED_OTHER") == 0)
	{
		i = 0;
		while (received_contains[i])
		{
			if (str_icontains(trimmed, received_contains[i]))
				return (1);
			i++;
		}
	}
	return (0);
}

static int	is_loot_type(const char *type)
{
	if (!type)
		return (0);
	if (strncmp(type, "LOOT", 4) == 0)
		return (1);
	if (strncmp(type, "RECEIVED", 8) == 0)
		return (1);
	if (strcmp(type, "SWEAT") == 0)
		return (1);
	return (0);
}

static int	is_expense_type(const char *type)
{
	if (!type)
		return (0);
	if (strcmp(type, "SPEND") == 0)
		return (1);
	if (strcmp(type, "DECAY") == 0)
		return (1);
	if (strcmp(type, "AMMO") == 0)
		return (1);
	if (strcmp(type, "REPAIR") == 0)
		return (1);
	if (str_icontains(type, "SPEND"))
		return (1);
	if (str_icontains(type, "DECAY"))
		return (1);
	if (str_icontains(type, "AMMO"))
		return (1);
	if (str_icontains(type, "REPAIR"))
		return (1);
	return (0);
}

static int	row_has_value(const t_hunt_csv_row_view *row)
{
	if (!row)
		return (0);
	return (row->has_value || ((row->flags & 1u) != 0u));
}

static int	raw_is_hit(const char *raw)
{
	if (!raw)
		return (0);
	if (strstr(raw, "You inflicted ") != NULL)
		return (1);
	if (strstr(raw, "Vous avez inflig") != NULL)
		return (1);
	return (0);
}

static int	looks_like_hunt_csv_header(const char *line)
{
	if (!line)
		return (0);
	if (strstr(line, "timestamp")
		&& (strstr(line, "event_type") || strstr(line, ",type,")))
		return (1);
	return (0);
}

/* -------------------------------------------------------------------------- */

static void	series_clear_all(t_hunt_series *s)
{
	int	i;

	if (!s)
		return ;
	s->file_pos = 0;
	s->t0 = 0;
	s->last_t = 0;
	s->shots_total = 0;
	s->hits_total = 0;
	s->kills_total = 0;
	s->loot_total_uPED = 0;
	s->expense_total_uPED = 0;
	s->first_bucket = 0;
	s->count = 0;
	s->kill_ev_count = 0;
	s->hits_ev_count = 0;
	s->hits_since_kill = 0;
	s->shots_ev_count = 0;
	s->shots_since_kill = 0;
	s->loot_ev_count = 0;
	s->last_loot_ev_t = 0;
	s->last_loot_ev_kill_id = 0;
	s->version = 0;
	i = 0;
	while (i < HS_MAX_POINTS)
	{
		s->buckets[i].shots = 0;
		s->buckets[i].hits = 0;
		s->buckets[i].kills = 0;
		s->buckets[i].loot_uPED = 0;
		s->buckets[i].expense_uPED = 0;
		i++;
	}
}

static void	series_clear_buckets(t_hunt_series *s)
{
	int	i;

	if (!s)
		return ;
	s->first_bucket = 0;
	s->count = 0;
	i = 0;
	while (i < HS_MAX_POINTS)
	{
		s->buckets[i].shots = 0;
		s->buckets[i].hits = 0;
		s->buckets[i].kills = 0;
		s->buckets[i].loot_uPED = 0;
	s->buckets[i].expense_uPED = 0;
		i++;
	}
}

void	hunt_series_reset(t_hunt_series *s, long start_offset, int bucket_sec)
{
	if (!s)
		return ;
	memset(s, 0, sizeof(*s));
	s->initialized = 0;
	s->start_offset = (start_offset < 0) ? 0 : start_offset;
	s->bucket_sec = (bucket_sec <= 0) ? 60 : bucket_sec;
	series_clear_all(s);
}

static void	shift_window_forward(t_hunt_series *s, long new_first)
{
	long	shift;
	int		i;

	shift = new_first - s->first_bucket;
	if (shift <= 0)
		return ;
	if (shift >= s->count)
	{
		series_clear_buckets(s);
		return ;
	}
	memmove(&s->buckets[0], &s->buckets[(int)shift],
		(size_t)(s->count - shift) * sizeof(s->buckets[0]));
	i = s->count - (int)shift;
	while (i < HS_MAX_POINTS)
	{
		s->buckets[i].shots = 0;
		s->buckets[i].hits = 0;
		s->buckets[i].kills = 0;
		s->buckets[i].loot_uPED = 0;
		s->buckets[i].expense_uPED = 0;
		i++;
	}
	s->count -= (int)shift;
	s->first_bucket = new_first;
}

static int	ensure_bucket(t_hunt_series *s, long abs_bucket)
{
	long	local;
	int		i;

	if (s->count == 0)
	{
		s->first_bucket = abs_bucket;
		s->count = 1;
		return (0);
	}
	if (abs_bucket < s->first_bucket)
		return (-1);
	if (abs_bucket >= s->first_bucket + HS_MAX_POINTS)
		shift_window_forward(s, abs_bucket - HS_MAX_POINTS + 1);
	local = abs_bucket - s->first_bucket;
	if (local >= HS_MAX_POINTS)
		return (-1);
	if ((int)local >= s->count)
	{
		i = s->count;
		while (i <= (int)local && i < HS_MAX_POINTS)
		{
			s->buckets[i].shots = 0;
			s->buckets[i].hits = 0;
			s->buckets[i].kills = 0;
			s->buckets[i].loot_uPED = 0;
			s->buckets[i].expense_uPED = 0;
			i++;
		}
		s->count = (int)local + 1;
	}
	return ((int)local);
}

static void	push_kill_event(t_hunt_series *s, time_t t)
{
	int	rel;

	if (!s || !s->t0 || !t)
		return ;
	rel = 0;
	if (t > s->t0)
		rel = (int)(t - s->t0);
	if (rel < 0)
		rel = 0;
	if (s->kill_ev_count < HS_MAX_EVENTS)
	{
		s->kill_ev_sec[s->kill_ev_count] = rel;
		s->kill_ev_count++;
		return ;
	}
	memmove(&s->kill_ev_sec[0], &s->kill_ev_sec[1],
		(size_t)(HS_MAX_EVENTS - 1) * sizeof(s->kill_ev_sec[0]));
	s->kill_ev_sec[HS_MAX_EVENTS - 1] = rel;
}

static void	push_hits_event(t_hunt_series *s, time_t t, long hits)
{
	int	rel;

	if (!s || !s->t0 || !t)
		return ;
	if (hits <= 0)
		return ;
	rel = 0;
	if (t > s->t0)
		rel = (int)(t - s->t0);
	if (rel < 0)
		rel = 0;
	if (s->hits_ev_count < HS_MAX_EVENTS)
	{
		s->hits_ev_sec[s->hits_ev_count] = rel;
		s->hits_ev_hits[s->hits_ev_count] = (int)hits;
		s->hits_ev_count++;
		return ;
	}
	memmove(&s->hits_ev_sec[0], &s->hits_ev_sec[1],
		(size_t)(HS_MAX_EVENTS - 1) * sizeof(s->hits_ev_sec[0]));
	memmove(&s->hits_ev_hits[0], &s->hits_ev_hits[1],
		(size_t)(HS_MAX_EVENTS - 1) * sizeof(s->hits_ev_hits[0]));
	s->hits_ev_sec[HS_MAX_EVENTS - 1] = rel;
	s->hits_ev_hits[HS_MAX_EVENTS - 1] = (int)hits;
}

static void	push_shots_event(t_hunt_series *s, time_t t, long shots, long hits)
{
	int	rel;

	if (!s || !s->t0 || !t)
		return ;
	if (shots <= 0)
		return ;
	rel = 0;
	if (t > s->t0)
		rel = (int)(t - s->t0);
	if (rel < 0)
		rel = 0;
	if (s->shots_ev_count < HS_MAX_EVENTS)
	{
		s->shots_ev_sec[s->shots_ev_count] = rel;
		s->shots_ev_shots[s->shots_ev_count] = (int)shots;
		s->shots_ev_hits[s->shots_ev_count] = (int)((hits < 0) ? 0 : hits);
		s->shots_ev_count++;
		return ;
	}
	memmove(&s->shots_ev_sec[0], &s->shots_ev_sec[1],
		(size_t)(HS_MAX_EVENTS - 1) * sizeof(s->shots_ev_sec[0]));
	memmove(&s->shots_ev_shots[0], &s->shots_ev_shots[1],
		(size_t)(HS_MAX_EVENTS - 1) * sizeof(s->shots_ev_shots[0]));
	memmove(&s->shots_ev_hits[0], &s->shots_ev_hits[1],
		(size_t)(HS_MAX_EVENTS - 1) * sizeof(s->shots_ev_hits[0]));
	s->shots_ev_sec[HS_MAX_EVENTS - 1] = rel;
	s->shots_ev_shots[HS_MAX_EVENTS - 1] = (int)shots;
	s->shots_ev_hits[HS_MAX_EVENTS - 1] = (int)((hits < 0) ? 0 : hits);
}

static void	mark_loot_has_kill(t_hunt_series *s, time_t t)
{
	if (!s || !t || s->loot_ev_count <= 0)
		return ;
	if (s->last_loot_ev_t == t)
		s->loot_ev_has_kill[s->loot_ev_count - 1] = 1;
}

static void	push_loot_value(t_hunt_series *s, time_t t, int64_t kill_id,
						tm_money_t v_uPED)
{
	int	rel;

	if (!s || !s->t0 || !t)
		return ;
	rel = 0;
	if (t > s->t0)
		rel = (int)(t - s->t0);
	if (rel < 0)
		rel = 0;

	/* Loot packet grouping:
	 * - Preferred: group by kill_id (robust with multi-kills/same second)
	 * - Fallback: if kill_id is missing (0), group by same timestamp second.
	 */
	if (s->loot_ev_count > 0)
	{
		if (kill_id > 0 && s->last_loot_ev_kill_id == kill_id)
		{
				s->loot_ev_uPED[s->loot_ev_count - 1] += v_uPED;
			s->loot_ev_group_count[s->loot_ev_count - 1]++;
			return ;
		}
		if (kill_id <= 0 && s->last_loot_ev_kill_id <= 0 && s->last_loot_ev_t == t)
		{
				s->loot_ev_uPED[s->loot_ev_count - 1] += v_uPED;
			s->loot_ev_group_count[s->loot_ev_count - 1]++;
			return ;
		}
	}

	if (s->loot_ev_count < HS_MAX_EVENTS)
	{
		s->loot_ev_sec[s->loot_ev_count] = rel;
		s->loot_ev_uPED[s->loot_ev_count] = v_uPED;
		s->loot_ev_group_count[s->loot_ev_count] = 1;
		s->loot_ev_has_kill[s->loot_ev_count] =
			(kill_id > 0
				|| (s->kill_ev_count > 0
					&& s->kill_ev_sec[s->kill_ev_count - 1] == rel)) ? 1 : 0;
		s->loot_ev_count++;
		s->last_loot_ev_t = t;
		s->last_loot_ev_kill_id = kill_id;
		return ;
	}
	memmove(&s->loot_ev_sec[0], &s->loot_ev_sec[1],
		(size_t)(HS_MAX_EVENTS - 1) * sizeof(s->loot_ev_sec[0]));
	memmove(&s->loot_ev_uPED[0], &s->loot_ev_uPED[1],
		(size_t)(HS_MAX_EVENTS - 1) * sizeof(s->loot_ev_uPED[0]));
	memmove(&s->loot_ev_group_count[0], &s->loot_ev_group_count[1],
		(size_t)(HS_MAX_EVENTS - 1) * sizeof(s->loot_ev_group_count[0]));
	memmove(&s->loot_ev_has_kill[0], &s->loot_ev_has_kill[1],
		(size_t)(HS_MAX_EVENTS - 1) * sizeof(s->loot_ev_has_kill[0]));
	s->loot_ev_sec[HS_MAX_EVENTS - 1] = rel;
	s->loot_ev_uPED[HS_MAX_EVENTS - 1] = v_uPED;
	s->loot_ev_group_count[HS_MAX_EVENTS - 1] = 1;
	s->loot_ev_has_kill[HS_MAX_EVENTS - 1] =
		(kill_id > 0
			|| (s->kill_ev_count > 0
				&& s->kill_ev_sec[s->kill_ev_count - 1] == rel)) ? 1 : 0;
	s->last_loot_ev_t = t;
	s->last_loot_ev_kill_id = kill_id;
}


static void	process_row_view(t_hunt_series *s, const t_hunt_csv_row_view *row)
{
	time_t	t;
	long	abs_bucket;
	int		idx;
	tm_money_t	v_uPED;

	if (!s || !row)
		return ;
	if (row->ts_unix <= 0)
		return ;
	t = (time_t)row->ts_unix;
	if (!t)
		return ;
	if (!s->t0)
		s->t0 = t;
	if (!s->last_t || t > s->last_t)
		s->last_t = t;
	abs_bucket = 0;
	if (t > s->t0)
		abs_bucket = (long)((t - s->t0) / s->bucket_sec);
	idx = ensure_bucket(s, abs_bucket);
	if (idx < 0)
		return ;

	if (row->type && strcmp(row->type, "SHOT") == 0)
	{
		s->buckets[idx].shots++;
		s->shots_total++;
		s->shots_since_kill++;
		if (raw_is_hit(row->raw))
		{
			s->buckets[idx].hits++;
			s->hits_total++;
			s->hits_since_kill++;
		}
		return ;
	}
	if (row->type && strncmp(row->type, "KILL", 4) == 0)
	{
		s->buckets[idx].kills++;
		s->kills_total++;
		push_kill_event(s, t);
		push_hits_event(s, t, s->hits_since_kill);
		push_shots_event(s, t, s->shots_since_kill, s->hits_since_kill);
		s->hits_since_kill = 0;
		s->shots_since_kill = 0;
		mark_loot_has_kill(s, t);
		return ;
	}
	if (row->type && is_expense_type(row->type))
	{
		if (row_has_value(row) && row->value_uPED != 0)
		{
			s->buckets[idx].expense_uPED = tm_money_add(s->buckets[idx].expense_uPED, row->value_uPED);
			s->expense_total_uPED = tm_money_add(s->expense_total_uPED, row->value_uPED);
		}
		return ;
	}
	if (row->type && is_loot_type(row->type))
	{
		if (loot_name_excluded_for_graph(row->type, row->name))
			return ;
		v_uPED = 0;
		if (row_has_value(row) && row->value_uPED > 0)
			v_uPED = row->value_uPED;
		if (v_uPED > 0)
		{
			s->buckets[idx].loot_uPED += v_uPED;
			s->loot_total_uPED += v_uPED;
		}
		/* Loot packets: 1 point per kill, group by same timestamp second */
		if (strcmp(row->type, "SWEAT") != 0 && strcmp(row->type, "RECEIVED_OTHER") != 0)
			push_loot_value(s, t, row->kill_id, v_uPED);
		return ;
	}
}

static long	skip_header_and_offset(FILE *f, long start_offset)
{
	char	buf[4096];
	long	data_lines;
	int		first;
	long	pos_before;

	data_lines = 0;
	first = 1;
	while (f && fgets(buf, (int)sizeof(buf), f))
	{
		pos_before = ftell(f) - (long)strlen(buf);
		if (first)
		{
			first = 0;
			if (looks_like_hunt_csv_header(buf))
				continue ;
		}
		if (data_lines == start_offset)
		{
			fseek(f, pos_before, SEEK_SET);
			break ;
		}
		data_lines++;
	}
	return (ftell(f));
}

int	hunt_series_update(t_hunt_series *s, const char *csv_path)
{
	FILE				*f;
	char				line[4096];
	long				sz;
	long				pos;
	t_hunt_csv_row_view	row;
	int				first;
	int				rows_ok;

	if (!s || !csv_path)
		return (0);
	sz = fs_file_size(csv_path);
	if (sz >= 0 && s->file_pos > sz)
		s->initialized = 0;
	f = fs_fopen_shared_read(csv_path);
	if (!f)
		return (0);
	if (!s->initialized)
	{
		series_clear_all(s);
		pos = skip_header_and_offset(f, s->start_offset);
		if (pos < 0)
			pos = 0;
		s->file_pos = pos;
		s->initialized = 1;
	}
	else
		fseek(f, s->file_pos, SEEK_SET);

	first = 1;
	rows_ok = 0;
	while (fgets(line, (int)sizeof(line), f))
	{
		/* Protect against gigantic lines */
		line[sizeof(line) - 1] = '\0';
		/* Skip header if file got truncated and rewritten */
		if (first && looks_like_hunt_csv_header(line))
		{
			first = 0;
			continue ;
		}
		first = 0;
		/* V2 parsing in-place (plus rapide: pas de copie) */
		if (!hunt_csv_parse_row_inplace(line, &row))
			continue ;	/* ignore malformed */
		process_row_view(s, &row);
		rows_ok++;
	}
	s->file_pos = ftell(f);
	fclose(f);
	if (rows_ok > 0)
		s->version++;
	return (1);
}

int	hunt_series_rebuild_range(t_hunt_series *s,
					const char *csv_path,
					long start_line,
					long end_line,
					int bucket_sec)
{
	FILE					*f;
	char					line[4096];
	long					data_idx;
	int					first;
	t_hunt_csv_row_view	row;

	if (!s || !csv_path)
		return (0);
	if (start_line < 0)
		start_line = 0;
	if (end_line >= 0 && end_line < start_line)
		end_line = start_line;
	/* Start from a clean slate */
	hunt_series_reset(s, start_line, bucket_sec);
	/* We rebuild from scratch, so ignore incremental cursor. */
	s->initialized = 1;
	s->file_pos = 0;

	f = fs_fopen_shared_read(csv_path);
	if (!f)
		return (0);
	data_idx = 0;
	first = 1;
	while (fgets(line, (int)sizeof(line), f))
	{
		line[sizeof(line) - 1] = '\0';
		/* Skip CSV header once */
		if (first && looks_like_hunt_csv_header(line))
		{
			first = 0;
			continue ;
		}
		first = 0;
		/* Apply range bounds on DATA lines (header ignored) */
		if (data_idx < start_line)
		{
			data_idx++;
			continue ;
		}
		if (end_line >= 0 && data_idx >= end_line)
			break ;
		/* Parse + process if valid; malformed lines still count in data_idx */
		if (hunt_csv_parse_row_inplace(line, &row))
		{
			process_row_view(s, &row);
			s->version++;
		}
		data_idx++;
	}
	fclose(f);
	s->version++;
	return (1);
}

double	hunt_series_elapsed_seconds(const t_hunt_series *s)
{
	if (!s || !s->t0 || !s->last_t)
		return (0.0);
	return (difftime(s->last_t, s->t0));
}

int	hunt_series_sanity_check(const t_hunt_series *s)
{
	int	i;

	if (!s)
		return (0);
	if (s->bucket_sec <= 0)
		return (0);
	if (s->count < 0 || s->count > HS_MAX_POINTS)
		return (0);
	if (s->kill_ev_count < 0 || s->kill_ev_count > HS_MAX_EVENTS)
		return (0);
	if (s->hits_ev_count < 0 || s->hits_ev_count > HS_MAX_EVENTS)
		return (0);
	if (s->shots_ev_count < 0 || s->shots_ev_count > HS_MAX_EVENTS)
		return (0);
	if (s->loot_ev_count < 0 || s->loot_ev_count > HS_MAX_EVENTS)
		return (0);
	/* Ensure loot group counts are always positive for stored events. */
	i = 0;
	while (i < s->loot_ev_count)
	{
		if (s->loot_ev_group_count[i] <= 0)
			return (0);
		i++;
	}
	/* file_pos can be 0 when rebuilt from scratch */
	if (s->file_pos < 0)
		return (0);
	/* time ordering if both present */
	if (s->t0 && s->last_t && s->last_t < s->t0)
		return (0);
	return (1);
}

int	hunt_series_build_plot(const t_hunt_series *s, int last_n_buckets,
						t_hs_metric metric, int cumulative,
						double *out_values, int *out_x_seconds,
						int *out_n, double *out_vmax)
{
	int		start;
	int		i;
	int		n;
	double	acc;
	tm_money_t	acc_uPED;
	double	vmax;
	double	v;
	tm_money_t	v_uPED;
	long	abs_idx;
	int		valid;
	int		j;

	if (!s || !out_values || !out_x_seconds || !out_n || !out_vmax)
		return (0);
	*out_n = 0;
	*out_vmax = 0.0;
	if (s->count <= 0)
		return (1);
	start = 0;
	if (last_n_buckets > 0 && s->count > last_n_buckets)
		start = s->count - last_n_buckets;
	n = 0;
	acc = 0.0;
	acc_uPED = 0;
	if (cumulative && start > 0)
	{
		j = 0;
		while (j < start)
		{
			valid = 1;
			v = 0.0;
			v_uPED = 0;
			if (metric == HS_METRIC_SHOTS)
				v = (double)s->buckets[j].shots;
			else if (metric == HS_METRIC_HITS)
				v = (double)s->buckets[j].hits;
			else if (metric == HS_METRIC_KILLS)
				v = (double)s->buckets[j].kills;
			else if (metric == HS_METRIC_LOOT_PED)
			{
				v_uPED = s->buckets[j].loot_uPED;
				valid = (s->buckets[j].kills > 0 && v_uPED > 0);
			}
			if (valid)
			{
				if (metric == HS_METRIC_LOOT_PED)
					acc_uPED += v_uPED;
				else
					acc += v;
			}
			j++;
		}
	}
	vmax = 0.0;
	i = start;
	while (i < s->count && n < HS_MAX_POINTS)
	{
		valid = 1;
		v = 0.0;
		v_uPED = 0;
		if (metric == HS_METRIC_SHOTS)
			v = (double)s->buckets[i].shots;
		else if (metric == HS_METRIC_HITS)
			v = (double)s->buckets[i].hits;
		else if (metric == HS_METRIC_KILLS)
			v = (double)s->buckets[i].kills;
		else if (metric == HS_METRIC_LOOT_PED)
		{
			v_uPED = s->buckets[i].loot_uPED;
			valid = (s->buckets[i].kills > 0 && v_uPED > 0);
			v = tm_money_to_ped_double(v_uPED);
		}
		if (!valid)
		{
			i++;
			continue ;
		}
		if (cumulative)
		{
			if (metric == HS_METRIC_LOOT_PED)
			{
				acc_uPED += v_uPED;
				v = tm_money_to_ped_double(acc_uPED);
			}
			else
			{
				acc += v;
				v = acc;
			}
		}
		out_values[n] = v;
		if (v > vmax)
			vmax = v;
		abs_idx = s->first_bucket + i;
		out_x_seconds[n] = (int)(abs_idx * (long)s->bucket_sec);
		n++;
		i++;
	}
	*out_n = n;
	*out_vmax = vmax;
	return (1);
}

/* ------------------------------------------------------------------------- */
/* Cost / ROI (RCE-safe: fixed point on input, double only for rendering)     */
/* ------------------------------------------------------------------------- */

static tm_money_t	money_add_clamp(tm_money_t a, tm_money_t b)
{
	if (b > 0 && a > (tm_money_t)INT64_MAX - b)
		return ((tm_money_t)INT64_MAX);
	if (b < 0 && a < (tm_money_t)INT64_MIN - b)
		return ((tm_money_t)INT64_MIN);
	return (a + b);
}

static tm_money_t	money_mul_long_clamp(tm_money_t v, long n)
{
	int		sign;
	uint64_t	av;
	uint64_t	an;

	if (n <= 0 || v == 0)
		return (0);
	sign = 1;
	if (v < 0)
	{
		sign = -1;
		av = (uint64_t)(-v);
	}
	else
		av = (uint64_t)v;
	an = (uint64_t)n;
	if (an != 0 && av > (uint64_t)INT64_MAX / an)
		return (sign > 0) ? (tm_money_t)INT64_MAX : (tm_money_t)INT64_MIN;
	return (sign > 0)
		? (tm_money_t)(av * an)
		: (tm_money_t)-(int64_t)(av * an);
}

int	hunt_series_build_cost_cumulative(const t_hunt_series *s,
						int last_n_buckets,
						tm_money_t cost_shot_uPED,
						double *out_values,
						int *out_x_seconds,
						int *out_n,
						double *out_vmax)
{
	int	start;
	int	i;
	int	n;
	long	abs_idx;
	tm_money_t	acc_logged;
	tm_money_t	acc_model;
	tm_money_t	acc_used;
	int	has_logged;
	int	has_model;
	double	vmax;

	if (!s || !out_values || !out_x_seconds || !out_n || !out_vmax)
		return (0);
	*out_n = 0;
	*out_vmax = 0.0;
	if (s->count <= 0)
		return (1);
	start = 0;
	if (last_n_buckets > 0 && s->count > last_n_buckets)
		start = s->count - last_n_buckets;
	has_logged = (s->expense_total_uPED != 0);
	has_model = (cost_shot_uPED > 0);
	acc_logged = 0;
	acc_model = 0;
	acc_used = 0;
	/* Pre-accumulate when we plot only the tail */
	if (start > 0)
	{
		i = 0;
		while (i < start)
		{
			if (has_logged)
				acc_logged = money_add_clamp(acc_logged, s->buckets[i].expense_uPED);
			if (has_model && s->buckets[i].shots > 0)
				acc_model = money_add_clamp(acc_model,
					money_mul_long_clamp(cost_shot_uPED, (long)s->buckets[i].shots));
			if (has_logged && has_model)
				acc_used = (acc_logged > acc_model) ? acc_logged : acc_model;
			else if (has_logged)
				acc_used = acc_logged;
			else
				acc_used = acc_model;
			i++;
		}
	}
	n = 0;
	vmax = 0.0;
	i = start;
	while (i < s->count && n < HS_MAX_POINTS)
	{
		if (has_logged)
			acc_logged = money_add_clamp(acc_logged, s->buckets[i].expense_uPED);
		if (has_model && s->buckets[i].shots > 0)
			acc_model = money_add_clamp(acc_model,
				money_mul_long_clamp(cost_shot_uPED, (long)s->buckets[i].shots));
		if (has_logged && has_model)
			acc_used = (acc_logged > acc_model) ? acc_logged : acc_model;
		else if (has_logged)
			acc_used = acc_logged;
		else
			acc_used = acc_model;
		out_values[n] = tm_money_to_ped_double(acc_used);
		if (out_values[n] > vmax)
			vmax = out_values[n];
		abs_idx = s->first_bucket + i;
		out_x_seconds[n] = (int)(abs_idx * (long)s->bucket_sec);
		n++;
		i++;
	}
	*out_n = n;
	*out_vmax = vmax;
	return (1);
}

int	hunt_series_build_roi_cumulative(const t_hunt_series *s,
						int last_n_buckets,
						tm_money_t cost_shot_uPED,
						double *out_values,
						int *out_x_seconds,
						int *out_n,
						double *out_vmax)
{
	int	start;
	int	i;
	int	n;
	long	abs_idx;
	tm_money_t	acc_loot;
	tm_money_t	acc_logged;
	tm_money_t	acc_model;
	tm_money_t	acc_cost;
	int	has_logged;
	int	has_model;
	double	vmax;

	if (!s || !out_values || !out_x_seconds || !out_n || !out_vmax)
		return (0);
	*out_n = 0;
	*out_vmax = 0.0;
	if (s->count <= 0)
		return (1);
	start = 0;
	if (last_n_buckets > 0 && s->count > last_n_buckets)
		start = s->count - last_n_buckets;
	has_logged = (s->expense_total_uPED != 0);
	has_model = (cost_shot_uPED > 0);
	acc_loot = 0;
	acc_logged = 0;
	acc_model = 0;
	acc_cost = 0;
	if (start > 0)
	{
		i = 0;
		while (i < start)
		{
			acc_loot = money_add_clamp(acc_loot, s->buckets[i].loot_uPED);
			if (has_logged)
				acc_logged = money_add_clamp(acc_logged, s->buckets[i].expense_uPED);
			if (has_model && s->buckets[i].shots > 0)
				acc_model = money_add_clamp(acc_model,
					money_mul_long_clamp(cost_shot_uPED, (long)s->buckets[i].shots));
			if (has_logged && has_model)
				acc_cost = (acc_logged > acc_model) ? acc_logged : acc_model;
			else if (has_logged)
				acc_cost = acc_logged;
			else
				acc_cost = acc_model;
			i++;
		}
	}
	n = 0;
	vmax = 0.0;
	i = start;
	while (i < s->count && n < HS_MAX_POINTS)
	{
		acc_loot = money_add_clamp(acc_loot, s->buckets[i].loot_uPED);
		if (has_logged)
			acc_logged = money_add_clamp(acc_logged, s->buckets[i].expense_uPED);
		if (has_model && s->buckets[i].shots > 0)
			acc_model = money_add_clamp(acc_model,
				money_mul_long_clamp(cost_shot_uPED, (long)s->buckets[i].shots));
		if (has_logged && has_model)
			acc_cost = (acc_logged > acc_model) ? acc_logged : acc_model;
		else if (has_logged)
			acc_cost = acc_logged;
		else
			acc_cost = acc_model;
		if (acc_cost > 0)
			out_values[n] = ((double)acc_loot * 100.0) / (double)acc_cost;
		else
			out_values[n] = 0.0;
		if (out_values[n] > vmax)
			vmax = out_values[n];
		abs_idx = s->first_bucket + i;
		out_x_seconds[n] = (int)(abs_idx * (long)s->bucket_sec);
		n++;
		i++;
	}
	*out_n = n;
	*out_vmax = vmax;
	return (1);
}

int	hunt_series_build_kill_events(const t_hunt_series *s, int last_minutes,
						double *out_values, int *out_x_seconds,
						int *out_n, double *out_vmax)
{
	int		start;
	int		i;
	int		n;
	double	vmax;
	int		min_sec;
	int		end_sec;

	if (!s || !out_values || !out_x_seconds || !out_n || !out_vmax)
		return (0);
	*out_n = 0;
	*out_vmax = 0.0;
	if (s->kill_ev_count <= 0)
		return (1);
	start = 0;
	if (last_minutes > 0)
	{
		end_sec = s->kill_ev_sec[s->kill_ev_count - 1];
		min_sec = end_sec - (last_minutes * 60);
		if (min_sec < 0)
			min_sec = 0;
		while (start < s->kill_ev_count && s->kill_ev_sec[start] < min_sec)
			start++;
	}
	n = 0;
	vmax = 0.0;
	i = start;
	while (i < s->kill_ev_count && n < HS_MAX_POINTS)
	{
		out_x_seconds[n] = s->kill_ev_sec[i];
		out_values[n] = (double)(i + 1);
		if (out_values[n] > vmax)
			vmax = out_values[n];
		n++;
		i++;
	}
	*out_n = n;
	*out_vmax = vmax;
	return (1);
}

int	hunt_series_build_hits_events(const t_hunt_series *s, int last_minutes,
						double *out_values, int *out_x_seconds,
						int *out_n, double *out_vmax)
{
	int		start;
	int		i;
	int		n;
	int		min_sec;
	int		end_sec;
	double	vmax;

	if (!s || !out_values || !out_x_seconds || !out_n || !out_vmax)
		return (0);
	*out_n = 0;
	*out_vmax = 0.0;
	if (s->hits_ev_count <= 0)
		return (1);
	start = 0;
	if (last_minutes > 0)
	{
		end_sec = s->hits_ev_sec[s->hits_ev_count - 1];
		min_sec = end_sec - (last_minutes * 60);
		if (min_sec < 0)
			min_sec = 0;
		while (start < s->hits_ev_count && s->hits_ev_sec[start] < min_sec)
			start++;
	}
	n = 0;
	vmax = 0.0;
	i = start;
	while (i < s->hits_ev_count && n < HS_MAX_POINTS)
	{
		if (s->hits_ev_hits[i] <= 0)
		{
			i++;
			continue ;
		}
		out_x_seconds[n] = s->hits_ev_sec[i];
		out_values[n] = (double)s->hits_ev_hits[i];
		if (out_values[n] > vmax)
			vmax = out_values[n];
		n++;
		i++;
	}
	*out_n = n;
	*out_vmax = vmax;
	return (1);
}

int	hunt_series_build_shots_events(const t_hunt_series *s, int last_minutes,
					double *out_values, int *out_x_seconds,
					int *out_n, double *out_vmax)
{
	int		start;
	int		i;
	int		n;
	int		min_sec;
	int		end_sec;
	double	vmax;

	if (!s || !out_values || !out_x_seconds || !out_n || !out_vmax)
		return (0);
	*out_n = 0;
	*out_vmax = 0.0;
	if (s->shots_ev_count <= 0)
		return (1);
	start = 0;
	if (last_minutes > 0)
	{
		end_sec = s->shots_ev_sec[s->shots_ev_count - 1];
		min_sec = end_sec - (last_minutes * 60);
		if (min_sec < 0)
			min_sec = 0;
		while (start < s->shots_ev_count && s->shots_ev_sec[start] < min_sec)
			start++;
	}
	n = 0;
	vmax = 0.0;
	i = start;
	while (i < s->shots_ev_count && n < HS_MAX_POINTS)
	{
		if (s->shots_ev_shots[i] <= 0)
		{
			i++;
			continue ;
		}
		out_x_seconds[n] = s->shots_ev_sec[i];
		out_values[n] = (double)s->shots_ev_shots[i];
		if (out_values[n] > vmax)
			vmax = out_values[n];
		n++;
		i++;
	}
	*out_n = n;
	*out_vmax = vmax;
	return (1);
}

int	hunt_series_build_hit_rate_events(const t_hunt_series *s, int last_minutes,
					double *out_values, int *out_x_seconds,
					int *out_n, double *out_vmax)
{
	int		start;
	int		i;
	int		n;
	int		min_sec;
	int		end_sec;
	double	vmax;
	double	shots;
	double	hits;
	double	rate;

	if (!s || !out_values || !out_x_seconds || !out_n || !out_vmax)
		return (0);
	*out_n = 0;
	*out_vmax = 0.0;
	if (s->shots_ev_count <= 0)
		return (1);
	start = 0;
	if (last_minutes > 0)
	{
		end_sec = s->shots_ev_sec[s->shots_ev_count - 1];
		min_sec = end_sec - (last_minutes * 60);
		if (min_sec < 0)
			min_sec = 0;
		while (start < s->shots_ev_count && s->shots_ev_sec[start] < min_sec)
			start++;
	}
	n = 0;
	vmax = 0.0;
	i = start;
	while (i < s->shots_ev_count && n < HS_MAX_POINTS)
	{
		shots = (double)s->shots_ev_shots[i];
		hits = (double)s->shots_ev_hits[i];
		if (shots <= 0.0)
		{
			i++;
			continue ;
		}
		rate = (hits / shots) * 100.0;
		if (rate < 0.0)
			rate = 0.0;
		if (rate > 100.0)
			rate = 100.0;
		out_x_seconds[n] = s->shots_ev_sec[i];
		out_values[n] = rate;
		if (out_values[n] > vmax)
			vmax = out_values[n];
		n++;
		i++;
	}
	*out_n = n;
	*out_vmax = vmax;
	return (1);
}

int	hunt_series_build_loot_events_ex(const t_hunt_series *s, int last_minutes,
						int cumulative,
						double *out_values, int *out_x_seconds,
						int *out_group_counts,
						int *out_n, double *out_vmax)
{
	int		start;
	int		i;
	int		n;
	int		min_sec;
	int		end_sec;
	double	vmax;
	tm_money_t	acc_uPED;
	int		has_kills;

	if (!s || !out_values || !out_x_seconds || !out_n || !out_vmax)
		return (0);
	*out_n = 0;
	*out_vmax = 0.0;
	if (s->loot_ev_count <= 0)
		return (1);
	start = 0;
	if (last_minutes > 0)
	{
		end_sec = s->loot_ev_sec[s->loot_ev_count - 1];
		min_sec = end_sec - (last_minutes * 60);
		if (min_sec < 0)
			min_sec = 0;
		while (start < s->loot_ev_count && s->loot_ev_sec[start] < min_sec)
			start++;
	}
	has_kills = (s->kill_ev_count > 0);
	acc_uPED = 0;
	if (cumulative && start > 0)
	{
		i = 0;
		while (i < start)
		{
			if (s->loot_ev_uPED[i] > 0 && (!has_kills || s->loot_ev_has_kill[i]))
				acc_uPED += s->loot_ev_uPED[i];
			i++;
		}
	}
	n = 0;
	vmax = 0.0;
	i = start;
	while (i < s->loot_ev_count && n < HS_MAX_POINTS)
	{
		if (!(s->loot_ev_uPED[i] > 0 && (!has_kills || s->loot_ev_has_kill[i])))
		{
			i++;
			continue ;
		}
		out_x_seconds[n] = s->loot_ev_sec[i];
		if (out_group_counts)
			out_group_counts[n] = s->loot_ev_group_count[i] > 0 ? s->loot_ev_group_count[i] : 1;
		if (cumulative)
		{
			acc_uPED += s->loot_ev_uPED[i];
			out_values[n] = tm_money_to_ped_double(acc_uPED);
		}
		else
			out_values[n] = tm_money_to_ped_double(s->loot_ev_uPED[i]);
		if (out_values[n] > vmax)
			vmax = out_values[n];
		n++;
		i++;
	}
	*out_n = n;
	*out_vmax = vmax;
	return (1);
}

int	hunt_series_build_loot_events(const t_hunt_series *s, int last_minutes,
						int cumulative,
						double *out_values, int *out_x_seconds,
						int *out_n, double *out_vmax)
{
	return (hunt_series_build_loot_events_ex(s, last_minutes, cumulative,
			out_values, out_x_seconds, NULL, out_n, out_vmax));
}
