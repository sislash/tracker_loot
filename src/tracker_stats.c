/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   tracker_stats.c                                    :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: login <login@student.42.fr>                +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/01/31 00:00:00 by login             #+#    #+#             */
/*   Updated: 2026/01/31 00:00:00 by login            ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "tracker_stats.h"
#include "config_arme.h"
#include "core_paths.h"
#include "hunt_csv.h"
#include "csv_index.h"
#include "markup.h"
#include "weapon_selected.h"
#include "sweat_option.h"
#include "eu_economy.h"
#include "tm_money.h"
#include "utils.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

typedef struct s_kv
{
	char	*key;
	long	count;
}	t_kv;

typedef struct s_kv_loot
{
	char	*key;
	long	events;
	tm_money_t	tt_sum;
	tm_money_t	final_sum;
}	t_kv_loot;


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

static const char	*kv_key_safe(const char *key)
{
	if (!key || !key[0])
		return ("(unknown)");
	return (key);
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
static void	kv_inc(t_kv **arr, size_t *len, const char *key)
{
	t_kv	*tmp;
	size_t	i;
	
	if (!key || !key[0])
		key = "(unknown)";
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

static void	kv_free(t_kv *arr, size_t len)
{
	tm_free_str_key_array(arr, len, sizeof(*arr), offsetof(t_kv, key));
}

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
		snprintf(out->player_name, sizeof(out->player_name), "%s",
				 db->player_name);
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
	/* percent => multiplier TT (ex: 1.025 or 45.357) */
	mu_mul_1e4 = (int64_t)llround(r.value * 10000.0);
	if (mu_mul_1e4 <= 0)
		mu_mul_1e4 = 10000;
	return (tm_money_mul_mu(tt_value, mu_mul_1e4));
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

/* trim helpers are centralized in utils.c (tm_trim_eol) */

static int	looks_like_header(const char *line)
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
	v = (has_value ? value_uPED : ((tm_money_t)q * (tm_money_t)EU_SWEAT_uPED_PER_BOTTLE));
	out->loot_ped += v;
	out->loot_events++;
	/* Treat sweat as a loot item for MU estimation */
	final = apply_markup_value(mu, "Vibrant Sweat", v);
	maybe_add_markup_fields(out, v, final);
	kv_loot_add(loot, loot_len, "Vibrant Sweat", v, final);
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

static int	row_has_value(const t_hunt_csv_row_view *row)
{
	if (!row)
		return (0);
	return (row->has_value || ((row->flags & 1u) != 0u));
}

static void	process_row_view(t_hunt_stats *out, const t_markup_db *mu,
				   t_kv_loot **loot, size_t *loot_len,
				   t_kv **mobs, size_t *mobs_len,
				   const t_hunt_csv_row_view *row,
				   int sweat_enabled)
{
	int		expense;
	int		has_v;
	tm_money_t	v;

	if (!row || !row->type)
		return ;
	if (strcmp(row->type, "SWEAT") == 0 && !sweat_enabled)
		return ;
	if (strncmp(row->type, "KILL", 4) == 0)
	{
		stats_on_kill(out, mobs, mobs_len, row->name);
		return ;
	}
	if (strcmp(row->type, "SHOT") == 0)
	{
		stats_on_shot(out, row->qty);
		return ;
	}
	if (strcmp(row->type, "SWEAT") == 0)
	{
		has_v = row_has_value(row);
		stats_on_sweat(out, mu, loot, loot_len, row->qty, row->value_uPED, has_v);
		return ;
	}
	has_v = row_has_value(row);
	if (!has_v)
		return ;
	v = row->value_uPED;
	expense = is_expense_type(row->type);
	if (is_loot_type(row->type) || (!expense && v > 0))
		stats_add_loot(out, mu, loot, loot_len, row->type, row->name, v);
	else if (expense)
		stats_add_expense(out, v);
}


static void	finalize_top_mobs(t_hunt_stats *out, t_kv *mobs, size_t mobs_len)
{
	size_t	i;
	size_t	max;
	
	out->mobs_unique = mobs_len;
	if (!mobs_len)
		return ;
	qsort(mobs, mobs_len, sizeof(mobs[0]), kv_cmp_desc);
	max = (mobs_len < (size_t)TM_TOP_MOBS) ? mobs_len : (size_t)TM_TOP_MOBS;
	out->top_mobs_count = max;
	i = 0;
	while (i < max)
	{
		out->top_mobs[i].name[0] = '\0';
		if (mobs[i].key)
			snprintf(out->top_mobs[i].name,
					 sizeof(out->top_mobs[i].name), "%s", mobs[i].key);
		out->top_mobs[i].kills = mobs[i].count;
		i++;
	}
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

static void	finalize_top_loot(t_hunt_stats *out, t_kv_loot *loot, size_t loot_len)
{
	size_t	i;
	size_t	max;

	out->top_loot_count = 0;
	if (!loot_len)
		return ;
	qsort(loot, loot_len, sizeof(loot[0]), kv_loot_cmp_desc_total);
	max = (loot_len < (size_t)TM_TOP_LOOT) ? loot_len : (size_t)TM_TOP_LOOT;
	out->top_loot_count = max;
	i = 0;
	while (i < max)
	{
		out->top_loot[i].name[0] = '\0';
		if (loot[i].key)
			snprintf(out->top_loot[i].name, sizeof(out->top_loot[i].name), "%s", loot[i].key);
		out->top_loot[i].tt_ped = loot[i].tt_sum;
		out->top_loot[i].total_mu_ped = loot[i].final_sum;
		out->top_loot[i].mu_ped = loot[i].final_sum - loot[i].tt_sum;
		out->top_loot[i].events = loot[i].events;
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

typedef struct s_stats_ctx
{
	FILE			*f;
	t_markup_db	mu;
	t_kv_loot		*loot;
	size_t		loot_len;
	t_kv			*mobs;
	size_t		mobs_len;
	long			data_idx;
	long			start_line;
	long			end_line;
	int			stop;
	int			is_first_line;
	int			sweat_enabled;
	t_hunt_stats	*out;
}	t_stats_ctx;

static void	ctx_init(t_stats_ctx *c, t_hunt_stats *out, long start_line, long end_line)
{
	memset(c, 0, sizeof(*c));
	c->out = out;
	c->start_line = start_line;
	c->end_line = end_line;
	c->stop = 0;
	c->is_first_line = 1;
	{
		int	enabled;
	
		enabled = 0;
		sweat_option_load(tm_path_options_cfg(), &enabled);
		c->sweat_enabled = enabled;
	}
	stats_zero(out);
	maybe_init_markup_fields(out);
	load_weapon_model(out);
}

static int	ctx_open(t_stats_ctx *c, const char *csv_path)
{
	(void)load_markup_db(&c->mu);
	c->f = fopen(csv_path, "rb");
	if (!c->f)
	{
		markup_db_free(&c->mu);
		return (-1);
	}
	return (0);
}

static int	ctx_consume_header(t_stats_ctx *c, const char *line)
{
	if (!c->is_first_line)
		return (0);
	c->is_first_line = 0;
	if (looks_like_header(line))
	{
		c->out->csv_has_header = 1;
		return (1);
	}
	return (0);
}

static int	ctx_skip_start(t_stats_ctx *c)
{
	if (c->data_idx < c->start_line)
	{
		c->data_idx++;
		return (1);
	}
	return (0);
}

static void	ctx_process_line(t_stats_ctx *c, char *buf)
{
	t_hunt_csv_row_view	row;
	
	tm_trim_eol(buf);
	if (ctx_consume_header(c, buf))
		return ;
	if (ctx_skip_start(c))
		return ;
	if (c->end_line >= 0 && c->data_idx >= c->end_line)
	{
		c->stop = 1;
		return ;
	}
	c->out->data_lines_read++;
	if (hunt_csv_parse_row_inplace(buf, &row))
		process_row_view(c->out, &c->mu, &c->loot, &c->loot_len,
					 &c->mobs, &c->mobs_len, &row, c->sweat_enabled);
	c->data_idx++;
}

static void	ctx_process_stream(t_stats_ctx *c)
{
	char	buf[8192];
	
	while (fgets(buf, (int)sizeof(buf), c->f))
	{
		ctx_process_line(c, buf);
		if (c->stop)
			break ;
	}
}

static void	ctx_finish(t_stats_ctx *c)
{
	if (c->f)
		fclose(c->f);
	finalize_top_loot(c->out, c->loot, c->loot_len);
	finalize_top_mobs(c->out, c->mobs, c->mobs_len);
	compute_costs(c->out);
	kv_loot_free(&c->loot, &c->loot_len);
	kv_free(c->mobs, c->mobs_len);
	markup_db_free(&c->mu);
}

int	tracker_stats_compute_range(const char *csv_path, long start_line,
							  long end_line, t_hunt_stats *out)
{
	t_stats_ctx	c;

	if (!csv_path || !out)
		return (-1);
	if (start_line < 0)
		start_line = 0;
	if (end_line >= 0 && end_line < start_line)
		end_line = start_line;
	ctx_init(&c, out, start_line, end_line);
	if (ctx_open(&c, csv_path) != 0)
		return (-1);
	ctx_process_stream(&c);
	/*
	 * Best-effort: if we reached EOF (end_line == -1), persist an O(1) row count
	 * for other modules (sessions picker, range resolution, etc.).
	 */
	if (end_line < 0 && c.f)
	{
		CsvIndexState	st;

		st.data_rows = (unsigned long long)c.data_idx;
		st.bytes = (unsigned long long)ftell(c.f);
		st.last_ts = 0;
		(void)csv_index_state_store_ex(csv_path, &st, NULL);
	}
	ctx_finish(&c);
	return (0);
}

int	tracker_stats_compute(const char *csv_path, long start_line,
					  t_hunt_stats *out)
{
	return (tracker_stats_compute_range(csv_path, start_line, -1, out));
}
