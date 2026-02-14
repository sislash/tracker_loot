/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   tracker_stats_live.c                               :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: tracker_loot                                  +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/02/12                                #+#    #+#             */
/*   Updated: 2026/02/12                                ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "tracker_stats_live.h"

#include "core_paths.h"
#include "fs_utils.h"
#include "session.h"
#include "hunt_csv.h"
#include "csv_index.h"
#include "markup.h"
#include "weapon_selected.h"
#include "config_arme.h"
#include "sweat_option.h"
#include "eu_economy.h"
#include "tm_money.h"
#include "utils.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

/*
 * This module implements a live incremental accumulator for t_hunt_stats.
 * It mirrors tracker_stats.c logic, but updates from appended CSV lines only.
 */

typedef struct s_kv
{
	char	*key;
	long	count;
} 	t_kv;

typedef struct s_kv_loot
{
	char		*key;
	long		events;
	tm_money_t	tt_sum;
	tm_money_t	final_sum;
} 	t_kv_loot;

typedef struct s_stats_live
{
	/* Incremental cursor */
	int		initialized;
	long		start_offset;
	long		file_pos;
	long		last_file_size;
	long		data_idx;

	/* Config cache */
	int		sweat_enabled;
	char		weapon_selected[128];

	/* Accumulators */
	t_markup_db	mu;
	t_kv_loot	*loot;
	size_t		loot_len;
	t_kv		*mobs;
	size_t		mobs_len;

	/* Output */
	t_hunt_stats	stats;

	/* Finalization throttling */
	int		dirty;
	uint64_t	last_finalize_ms;
} 	t_stats_live;

static t_stats_live	g_live;
static int			g_ready = 0;

/* Mode tracking (same semantics as hunt_series_live): */
static int			g_last_mode = -1; /* 0=live offset, 1=range */
static long			g_last_offset = -1;
static long			g_last_range_start = -1;
static long			g_last_range_end = -1;
static long			g_last_range_end_raw = -2;

static char			g_warn_text[96] = {0};

static void	stats_zero(t_hunt_stats *s)
{
	tm_zero(s, sizeof(*s));
}

static char	*xstrdup(const char *s)
{
	size_t	len;
	char	*p;

	if (!s)
		return (NULL);
	len = strlen(s) + 1;
	p = (char *)malloc(len);
	if (!p)
		return (NULL);
	memcpy(p, s, len);
	return (p);
}

static int	str_icontains(const char *hay, const char *needle)
{
	size_t	nlen;
	size_t	i;

	if (!hay || !needle || !needle[0])
		return (0);
	nlen = strlen(needle);
	while (*hay)
	{
		i = 0;
		while (i < nlen && hay[i]
			&& tolower((unsigned char)hay[i])
				== tolower((unsigned char)needle[i]))
			i++;
		if (i == nlen)
			return (1);
		hay++;
	}
	return (0);
}

static const char	*kv_key_safe(const char *key)
{
	if (!key || !key[0])
		return ("(unknown)");
	return (key);
}

static void	kv_inc(t_kv **arr, size_t *len, const char *key)
{
	t_kv	*tmp;
	size_t	i;

	key = kv_key_safe(key);
	i = 0;
	while (i < *len)
	{
		if ((*arr)[i].key && strcmp((*arr)[i].key, key) == 0)
		{
			(*arr)[i].count++;
			return ;
		}
		i++;
	}
	tmp = (t_kv *)realloc(*arr, (*len + 1) * sizeof(**arr));
	if (!tmp)
		return ;
	*arr = tmp;
	(*arr)[*len].key = xstrdup(key);
	(*arr)[*len].count = 1;
	(*len)++;
}

static void	kv_free(t_kv **arr, size_t *len)
{
	size_t	i;

	if (!arr || !*arr)
		return ;
	i = 0;
	while (i < *len)
	{
		free((*arr)[i].key);
		i++;
	}
	free(*arr);
	*arr = NULL;
	*len = 0;
}

static void	kv_loot_push(t_kv_loot **arr, size_t *len,
						const char *key, tm_money_t tt, tm_money_t final)
{
	t_kv_loot	*tmp;

	tmp = (t_kv_loot *)realloc(*arr, (*len + 1) * sizeof(**arr));
	if (!tmp)
		return ;
	*arr = tmp;
	(*arr)[*len].key = xstrdup(key);
	(*arr)[*len].events = 1;
	(*arr)[*len].tt_sum = tt;
	(*arr)[*len].final_sum = final;
	(*len)++;
}

static void	kv_loot_add(t_kv_loot **arr, size_t *len,
					const char *key, tm_money_t tt, tm_money_t final)
{
	size_t	i;

	key = kv_key_safe(key);
	i = 0;
	while (i < *len)
	{
		if ((*arr)[i].key && strcmp((*arr)[i].key, key) == 0)
		{
			(*arr)[i].events++;
			(*arr)[i].tt_sum += tt;
			(*arr)[i].final_sum += final;
			return ;
		}
		i++;
	}
	kv_loot_push(arr, len, key, tt, final);
}

static void	kv_loot_free(t_kv_loot **arr, size_t *len)
{
	size_t	i;

	if (!arr || !*arr)
		return ;
	i = 0;
	while (i < *len)
	{
		free((*arr)[i].key);
		i++;
	}
	free(*arr);
	*arr = NULL;
	*len = 0;
}

/* ---------------- Weapon + Markup (mirrors tracker_stats.c) ------------- */

static void	weapon_defaults(t_hunt_stats *out)
{
	out->has_weapon = 0;
	out->weapon_name[0] = '\0';
	out->player_name[0] = '\0';
	out->ammo_shot_uPED = 0;
	out->decay_shot_uPED = 0;
	out->amp_decay_shot_uPED = 0;
	out->cost_shot_uPED = 0;
	out->markup = 1.0;
}

static int	weapon_load_selected(char *dst, size_t dstsz)
{
	dst[0] = '\0';
	if (weapon_selected_load(tm_path_weapon_selected(), dst, dstsz) != 0)
		return (0);
	if (!dst[0])
		return (0);
	return (1);
}

static void	weapon_fill_identity(t_hunt_stats *out,
							const arme_stats *w, const armes_db *db)
{
	out->has_weapon = 1;
	snprintf(out->weapon_name, sizeof(out->weapon_name), "%s", w->name);
	if (db->player_name[0])
		snprintf(out->player_name, sizeof(out->player_name), "%s", db->player_name);
}

static void	weapon_costs_split_mu(t_hunt_stats *out, const arme_stats *w)
{
	int64_t	ammo_mu;
	int64_t	weapon_mu;
	int64_t	amp_mu;
	tm_money_t	base_tt;

	ammo_mu = (w->ammo_mu_1e4 > 0) ? w->ammo_mu_1e4 : 10000;
	weapon_mu = (w->weapon_mu_1e4 > 0) ? w->weapon_mu_1e4 : 10000;
	amp_mu = (w->amp_mu_1e4 > 0) ? w->amp_mu_1e4 : 10000;
	out->ammo_shot_uPED = tm_money_mul_mu(w->ammo_shot, ammo_mu);
	out->decay_shot_uPED = tm_money_mul_mu(w->decay_shot, weapon_mu);
	out->amp_decay_shot_uPED = tm_money_mul_mu(w->amp_decay_shot, amp_mu);
	out->cost_shot_uPED = out->ammo_shot_uPED + out->decay_shot_uPED + out->amp_decay_shot_uPED;
	base_tt = w->ammo_shot + w->decay_shot + w->amp_decay_shot;
	out->markup = (base_tt > 0) ? ((double)out->cost_shot_uPED / (double)base_tt) : 1.0;
}

static void	weapon_costs_legacy(t_hunt_stats *out, const arme_stats *w)
{
	int64_t	mu;
	tm_money_t	base_tt;

	mu = (w->markup_mu_1e4 > 0) ? w->markup_mu_1e4 : 10000;
	out->ammo_shot_uPED = w->ammo_shot;
	out->decay_shot_uPED = tm_money_mul_mu(w->decay_shot, mu);
	out->amp_decay_shot_uPED = tm_money_mul_mu(w->amp_decay_shot, mu);
	out->cost_shot_uPED = out->ammo_shot_uPED + out->decay_shot_uPED + out->amp_decay_shot_uPED;
	base_tt = w->ammo_shot + w->decay_shot + w->amp_decay_shot;
	out->markup = (base_tt > 0) ? ((double)out->cost_shot_uPED / (double)base_tt) : 1.0;
}

static void	weapon_apply_model(t_hunt_stats *out, const arme_stats *w)
{
	if (w->weapon_mu_1e4 > 0 || w->amp_mu_1e4 > 0)
		weapon_costs_split_mu(out, w);
	else
		weapon_costs_legacy(out, w);
}

static void	load_weapon_model(t_hunt_stats *out)
{
	armes_db			db;
	char				selected[128];
	const arme_stats	*w;

	if (!out)
		return ;
	weapon_defaults(out);
	if (!weapon_load_selected(selected, sizeof(selected)))
		return ;
	memset(&db, 0, sizeof(db));
	if (!armes_db_load(&db, tm_path_armes_ini()))
	{
		armes_db_free(&db);
		return ;
	}
	w = armes_db_find(&db, selected);
	if (w)
	{
		weapon_fill_identity(out, w, &db);
		weapon_apply_model(out, w);
	}
	armes_db_free(&db);
}

static int	load_markup_db(t_markup_db *mu)
{
	markup_db_init(mu);
	if (markup_db_load(mu, tm_path_markup_ini()) != 0)
	{
		markup_db_free(mu);
		markup_db_init(mu);
		return (-1);
	}
	return (0);
}

static void	maybe_init_markup_fields(t_hunt_stats *out)
{
#ifdef TM_STATS_HAS_MARKUP
	out->loot_tt_ped = 0;
	out->loot_mu_ped = 0;
	out->loot_total_mu_ped = 0;
#else
	(void)out;
#endif
}

static void	maybe_add_markup_fields(t_hunt_stats *out, tm_money_t tt, tm_money_t final)
{
#ifdef TM_STATS_HAS_MARKUP
	out->loot_tt_ped += tt;
	out->loot_mu_ped += (final - tt);
	out->loot_total_mu_ped += final;
#else
	(void)out;
	(void)tt;
	(void)final;
#endif
}

static tm_money_t	apply_markup_value(const t_markup_db *mu,
										const char *item_name, tm_money_t tt_value)
{
	t_markup_rule	r;
	int64_t			mu_mul_1e4;

	if (!mu || !item_name || !item_name[0])
		return (tt_value);
	if (!markup_db_get(mu, item_name, &r))
		r = markup_default_rule();
	if (r.type == MARKUP_TT_PLUS)
		return (tt_value + tm_money_from_ped_double(r.value));
	mu_mul_1e4 = (int64_t)llround(r.value * 10000.0);
	if (mu_mul_1e4 <= 0)
		mu_mul_1e4 = 10000;
	return (tm_money_mul_mu(tt_value, mu_mul_1e4));
}

/* ---------------- Core processing (mirrors tracker_stats.c) -------------- */

static int	looks_like_hunt_csv_header(const char *line)
{
	if (!line)
		return (0);
	if (strstr(line, "timestamp")
		&& (strstr(line, "event_type") || strstr(line, ",type,")))
		return (1);
	return (0);
}

static void	stats_on_kill(t_hunt_stats *out, t_kv **mobs,
						size_t *mobs_len, const char *name)
{
	out->kills++;
	kv_inc(mobs, mobs_len, name);
}

static void	stats_on_shot(t_hunt_stats *out, long qty)
{
	long	q;

	q = qty;
	if (q <= 0)
		q = 1;
	out->shots += q;
}

static int	is_loot_type(const char *type)
{
	if (strncmp(type, "LOOT", 4) == 0)
		return (1);
	if (strncmp(type, "RECEIVED", 8) == 0)
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

static void	stats_add_loot(t_hunt_stats *out, const t_markup_db *mu,
						t_kv_loot **loot, size_t *loot_len,
						const char *type, const char *name, tm_money_t v)
{
	tm_money_t	final;

	out->loot_ped += v;
	out->loot_events++;
	if (is_loot_type(type))
	{
		final = apply_markup_value(mu, name, v);
		maybe_add_markup_fields(out, v, final);
		kv_loot_add(loot, loot_len, name, v, final);
	}
}

static void	stats_add_expense(t_hunt_stats *out, tm_money_t v)
{
	out->expense_ped_logged += v;
	out->expense_events++;
}

static void	stats_on_sweat(t_hunt_stats *out, const t_markup_db *mu,
						t_kv_loot **loot, size_t *loot_len,
						long qty, tm_money_t value_uPED, int has_value)
{
	long		q;
	tm_money_t	v;
	tm_money_t	final;

	q = qty;
	if (q < 0)
		q = 0;
	out->sweat_total += q;
	out->sweat_events++;
	v = (has_value ? value_uPED
			: ((tm_money_t)q * (tm_money_t)EU_SWEAT_uPED_PER_BOTTLE));
	out->loot_ped += v;
	out->loot_events++;
	final = apply_markup_value(mu, "Vibrant Sweat", v);
	maybe_add_markup_fields(out, v, final);
	kv_loot_add(loot, loot_len, "Vibrant Sweat", v, final);
}

static void	process_row_view(t_stats_live *st, const t_hunt_csv_row_view *row)
{
	int			expense;
	int			has_v;
	tm_money_t	v;

	if (!st || !row || !row->type)
		return ;
	if (strcmp(row->type, "SWEAT") == 0 && !st->sweat_enabled)
		return ;
	if (strncmp(row->type, "KILL", 4) == 0)
	{
		stats_on_kill(&st->stats, &st->mobs, &st->mobs_len, row->name);
		return ;
	}
	if (strcmp(row->type, "SHOT") == 0)
	{
		stats_on_shot(&st->stats, row->qty);
		return ;
	}
	if (strcmp(row->type, "SWEAT") == 0)
	{
		has_v = row_has_value(row);
		stats_on_sweat(&st->stats, &st->mu, &st->loot, &st->loot_len,
			row->qty, row->value_uPED, has_v);
		return ;
	}
	has_v = row_has_value(row);
	if (!has_v)
		return ;
	v = row->value_uPED;
	expense = is_expense_type(row->type);
	if (is_loot_type(row->type) || (!expense && v > 0))
		stats_add_loot(&st->stats, &st->mu, &st->loot, &st->loot_len, row->type, row->name, v);
	else if (expense)
		stats_add_expense(&st->stats, v);
}

static int	kv_cmp_desc(const void *a, const void *b)
{
	const t_kv	*ka;
	const t_kv	*kb;

	ka = (const t_kv *)a;
	kb = (const t_kv *)b;
	if (ka->count < kb->count)
		return (1);
	if (ka->count > kb->count)
		return (-1);
	if (!ka->key)
		return (1);
	if (!kb->key)
		return (-1);
	return (strcmp(ka->key, kb->key));
}

static int	kv_loot_cmp_desc_total(const void *a, const void *b)
{
	const t_kv_loot	*ka;
	const t_kv_loot	*kb;

	ka = (const t_kv_loot *)a;
	kb = (const t_kv_loot *)b;
	if (ka->final_sum < kb->final_sum)
		return (1);
	if (ka->final_sum > kb->final_sum)
		return (-1);
	return (0);
}

static void	finalize_top_mobs(t_stats_live *st)
{
	size_t	i;
	size_t	max;

	st->stats.mobs_unique = st->mobs_len;
	st->stats.top_mobs_count = 0;
	if (!st->mobs_len)
		return ;
	qsort(st->mobs, st->mobs_len, sizeof(st->mobs[0]), kv_cmp_desc);
	max = (st->mobs_len < (size_t)TM_TOP_MOBS) ? st->mobs_len : (size_t)TM_TOP_MOBS;
	st->stats.top_mobs_count = max;
	i = 0;
	while (i < max)
	{
		st->stats.top_mobs[i].name[0] = '\0';
		if (st->mobs[i].key)
			snprintf(st->stats.top_mobs[i].name, sizeof(st->stats.top_mobs[i].name), "%s", st->mobs[i].key);
		st->stats.top_mobs[i].kills = st->mobs[i].count;
		i++;
	}
}

static void	finalize_top_loot(t_stats_live *st)
{
	size_t	i;
	size_t	max;

	st->stats.top_loot_count = 0;
	if (!st->loot_len)
		return ;
	qsort(st->loot, st->loot_len, sizeof(st->loot[0]), kv_loot_cmp_desc_total);
	max = (st->loot_len < (size_t)TM_TOP_LOOT) ? st->loot_len : (size_t)TM_TOP_LOOT;
	st->stats.top_loot_count = max;
	i = 0;
	while (i < max)
	{
		st->stats.top_loot[i].name[0] = '\0';
		if (st->loot[i].key)
			snprintf(st->stats.top_loot[i].name, sizeof(st->stats.top_loot[i].name), "%s", st->loot[i].key);
		st->stats.top_loot[i].tt_ped = st->loot[i].tt_sum;
		st->stats.top_loot[i].total_mu_ped = st->loot[i].final_sum;
		st->stats.top_loot[i].mu_ped = st->loot[i].final_sum - st->loot[i].tt_sum;
		st->stats.top_loot[i].events = st->loot[i].events;
		i++;
	}
}

static tm_money_t	money_mul_long_clamp(tm_money_t v, long n)
{
	int sign = 1;
	uint64_t av;
	uint64_t an;
	if (n <= 0 || v == 0)
		return (0);
	if (v < 0) { sign = -sign; av = (uint64_t)(-v); } else av = (uint64_t)v;
	an = (uint64_t)n;
	if (an != 0 && av > (uint64_t)INT64_MAX / an)
		return (sign > 0) ? (tm_money_t)INT64_MAX : (tm_money_t)INT64_MIN;
	return (sign > 0) ? (tm_money_t)(av * an) : (tm_money_t)-(int64_t)(av * an);
}

static void	compute_costs(t_hunt_stats *out)
{
	if (out->has_weapon && out->shots > 0 && out->cost_shot_uPED > 0)
		out->expense_ped_calc = money_mul_long_clamp(out->cost_shot_uPED, out->shots);
	out->expense_used_is_logged = (out->expense_events > 0);
	if (out->expense_used_is_logged && out->has_weapon && out->shots > 0)
	{
		if (out->expense_ped_logged > out->expense_ped_calc)
			out->expense_used = out->expense_ped_logged;
		else
			out->expense_used = out->expense_ped_calc;
	}
	else if (out->expense_used_is_logged)
		out->expense_used = out->expense_ped_logged;
	else
		out->expense_used = out->expense_ped_calc;
	out->net_ped = out->loot_ped - out->expense_used;
}

static long	skip_header_and_offset(FILE *f, long start_offset, t_hunt_stats *out)
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
			{
				if (out)
					out->csv_has_header = 1;
				continue ;
			}
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

static void	stats_live_clear(t_stats_live *st)
{
	if (!st)
		return ;
	markup_db_free(&st->mu);
	kv_loot_free(&st->loot, &st->loot_len);
	kv_free(&st->mobs, &st->mobs_len);
	st->initialized = 0;
	st->file_pos = 0;
	st->last_file_size = -1;
	st->data_idx = 0;
	st->dirty = 0;
	st->last_finalize_ms = 0;
}

static void	stats_live_reset(t_stats_live *st, long start_offset)
{
	int enabled;

	if (!st)
		return ;
	stats_live_clear(st);
	stats_zero(&st->stats);
	maybe_init_markup_fields(&st->stats);
	load_weapon_model(&st->stats);
	(void)load_markup_db(&st->mu);
	enabled = 0;
	sweat_option_load(tm_path_options_cfg(), &enabled);
	st->sweat_enabled = enabled;
	st->weapon_selected[0] = '\0';
	weapon_selected_load(tm_path_weapon_selected(), st->weapon_selected, sizeof(st->weapon_selected));
	st->start_offset = start_offset;
	st->data_idx = 0;
	st->stats.csv_has_header = 0;
}

static int	stats_live_update(t_stats_live *st, const char *csv_path)
{
	FILE				*f;
	char				line[8192];
	long				sz;
	long				pos;
	int				first;
	t_hunt_csv_row_view	row;
	int				lines_processed;
	int64_t				last_ts;
	const size_t		idx_stride = 1024;

	if (!st || !csv_path)
		return (0);
	last_ts = 0;
	sz = fs_file_size(csv_path);
	if (sz >= 0 && st->file_pos > sz)
		st->initialized = 0;
	f = fs_fopen_shared_read(csv_path);
	if (!f)
		return (0);
	if (!st->initialized)
	{
		pos = skip_header_and_offset(f, st->start_offset, &st->stats);
		if (pos < 0)
			pos = 0;
		st->file_pos = pos;
		st->initialized = 1;
		st->data_idx = st->start_offset;
	}
	else
		fseek(f, st->file_pos, SEEK_SET);

	lines_processed = 0;
	first = 1;
	while (fgets(line, (int)sizeof(line), f))
	{
		long		pos_after;
		long		pos_before;
		unsigned long long	row_index;

		line[sizeof(line) - 1] = '\0';
		pos_after = ftell(f);
		pos_before = pos_after - (long)strlen(line);
		/* Skip header if file got truncated and rewritten */
		if (first && looks_like_hunt_csv_header(line))
		{
			first = 0;
			st->stats.csv_has_header = 1;
			continue ;
		}
		first = 0;
		/* Global data row index (header excluded), 0-based. */
		row_index = (unsigned long long)st->data_idx;
		st->stats.data_lines_read++;
		st->data_idx++;
		if (!hunt_csv_parse_row_inplace(line, &row))
			continue ;
		process_row_view(st, &row);
		last_ts = row.ts_unix;
		lines_processed++;
		/* Best-effort sparse index maintenance for fast seeks & O(1) row counts. */
		(void)csv_index_maybe_append_checkpoint_ex(
			csv_path, idx_stride, row_index,
			(long long)row.ts_unix,
			(unsigned long long)pos_before,
			NULL);
	}
	st->file_pos = ftell(f);
	fclose(f);
	if (lines_processed > 0)
	{
		CsvIndexState	ist;

		st->dirty = 1;
		ist.data_rows = (unsigned long long)st->data_idx;
		ist.bytes = (unsigned long long)st->file_pos;
		ist.last_ts = (long long)last_ts;
		(void)csv_index_state_store_ex(csv_path, &ist, NULL);
	}
	st->last_file_size = sz;
	return (1);
}

static void	stats_live_finalize_if_needed(t_stats_live *st)
{
	uint64_t now;

	if (!st)
		return ;
	compute_costs(&st->stats);
	if (!st->dirty)
		return ;
	now = ft_time_ms();
	if (st->last_finalize_ms != 0 && (now - st->last_finalize_ms) < 650)
		return ;
	st->last_finalize_ms = now;
	finalize_top_mobs(st);
	finalize_top_loot(st);
	st->dirty = 0;
}

/* ---------------- Public API ------------------------------------------- */

void	tracker_stats_live_force_reset(void)
{
	stats_live_clear(&g_live);
	g_ready = 0;
	g_last_mode = -1;
	g_last_offset = -1;
	g_last_range_start = -1;
	g_last_range_end = -1;
	g_last_range_end_raw = -2;
	g_warn_text[0] = '\0';
}

static void	range_normalize(long *start, long *end_raw)
{
	if (!start || !end_raw)
		return ;
	if (*start < 0)
		*start = 0;
	if (*end_raw >= 0 && *end_raw < *start)
		*end_raw = *start;
}

void	tracker_stats_live_tick(void)
{
	long	offset;
	long	r_start;
	long	r_end_raw;
	long	r_end_resolved;
	int		range_on;
	int		need_rebuild;
	int		ok;
	int		enabled;
	char		selected[128];

	/* Range mode (Sessions picker) */
	r_start = 0;
	r_end_raw = -1;
	range_on = session_load_range(tm_path_session_range(), &r_start, &r_end_raw);
	if (range_on)
	{
		range_normalize(&r_start, &r_end_raw);
		need_rebuild = (!g_ready || g_last_mode != 1
				|| r_start != g_last_range_start
				|| r_end_raw != g_last_range_end_raw);
		if (need_rebuild)
		{
			r_end_resolved = r_end_raw;
			if (r_end_resolved < 0)
				r_end_resolved = session_count_data_lines(tm_path_hunt_csv());
			if (r_end_resolved < r_start)
				r_end_resolved = r_start;
			stats_live_clear(&g_live);
			stats_zero(&g_live.stats);
			ok = (tracker_stats_compute_range(tm_path_hunt_csv(), r_start, r_end_resolved, &g_live.stats) == 0);
			g_ready = ok ? 1 : 0;
			g_last_mode = 1;
			g_last_range_start = r_start;
			g_last_range_end = r_end_resolved;
			g_last_range_end_raw = r_end_raw;
			g_last_offset = -1;
			g_warn_text[0] = '\0';
			if (!ok)
				snprintf(g_warn_text, sizeof(g_warn_text), "WARN: stats range compute failed");
		}
		return ;
	}

	/* Live offset mode */
	offset = session_load_offset(tm_path_session_offset());
	if (!g_ready || g_last_mode != 0 || offset != g_last_offset)
	{
		stats_live_reset(&g_live, offset);
		g_last_offset = offset;
		g_last_mode = 0;
		g_last_range_start = -1;
		g_last_range_end = -1;
		g_last_range_end_raw = -2;
		g_ready = 1;
		g_warn_text[0] = '\0';
	}

	/* If SWEAT option changed, force rebuild (cannot un-count incrementally). */
	enabled = 0;
	sweat_option_load(tm_path_options_cfg(), &enabled);
	if (enabled != g_live.sweat_enabled)
	{
		stats_live_reset(&g_live, offset);
		g_warn_text[0] = '\0';
		g_live.sweat_enabled = enabled;
	}

	/* If weapon selection changed, reload weapon model (cost calc). */
	selected[0] = '\0';
	weapon_selected_load(tm_path_weapon_selected(), selected, sizeof(selected));
	if (strcmp(selected, g_live.weapon_selected) != 0)
	{
		/* Only affects expense calc; we can recompute costs without rescan. */
		strncpy(g_live.weapon_selected, selected, sizeof(g_live.weapon_selected) - 1);
		g_live.weapon_selected[sizeof(g_live.weapon_selected) - 1] = '\0';
		load_weapon_model(&g_live.stats);
	}

	ok = stats_live_update(&g_live, tm_path_hunt_csv());
	if (!ok)
	{
		g_ready = 0;
		snprintf(g_warn_text, sizeof(g_warn_text), "WARN: stats live update failed");
		return ;
	}
	stats_live_finalize_if_needed(&g_live);
}

const t_hunt_stats	*tracker_stats_live_get(void)
{
	if (!g_ready)
		return (NULL);
	return (&g_live.stats);
}

int	tracker_stats_live_is_range(void)
{
	return (g_ready && g_last_mode == 1);
}

void	tracker_stats_live_get_range(long *out_start, long *out_end_raw,
								long *out_end_resolved)
{
	if (out_start)
		*out_start = g_last_range_start;
	if (out_end_raw)
		*out_end_raw = g_last_range_end_raw;
	if (out_end_resolved)
		*out_end_resolved = g_last_range_end;
}

long	tracker_stats_live_get_offset(void)
{
	return (g_last_offset);
}

void	tracker_stats_live_bootstrap(void)
{
	tracker_stats_live_force_reset();
	tracker_stats_live_tick();
}
