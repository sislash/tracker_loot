/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   hunt_series_live.c                                 :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: tracker_loot                                  +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/02/09                                #+#    #+#             */
/*   Updated: 2026/02/12                                ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "hunt_series_live.h"

#include "core_paths.h"
#include "fs_utils.h"
#include "session.h"

/*
 * IMPORTANT:
 * - Ce cache est volontairement statique et vit pendant toute l'execution.
 * - Il est mis a jour depuis le thread UI uniquement.
 */

static t_hunt_series	g_hs;
static int			g_ready = 0;
static long			g_last_offset = -1;

/* Range-view tracking (when a session is loaded from Sessions/Exports). */
static int			g_last_mode = -1; /* 0 = live offset, 1 = range view */
static long			g_last_range_start = -1;
/* Resolved end (>=0) actually used to rebuild the series. */
static long			g_last_range_end = -1;
/* Raw end as stored in logs/hunt_session.range (can be -1 meaning EOF). */
static long			g_last_range_end_raw = -2;

/* Cached EOF resolution for range end_raw == -1 (avoids rescanning CSV). */
static long			g_cached_eof_lines = -1;
static long			g_cached_eof_size = -1;

/* Simple regression guard / warning string (shown in UI status bar). */
static char			g_warn_text[96] = {0};

void	hunt_series_live_force_reset(void)
{
	g_ready = 0;
	g_last_offset = -1;
	g_last_mode = -1;
	g_last_range_start = -1;
	g_last_range_end = -1;
	g_last_range_end_raw = -2;
	g_warn_text[0] = '\0';
	/* g_hs sera re-initialise au prochain tick */
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

void	hunt_series_live_tick(void)
{
	long	offset;
	long	r_start;
	long	r_end_raw;
	long	r_end_resolved;
	int		range_on;
	int		need_rebuild;
	long	sz;
	int		ok;

	/* If the user loaded a session range, Graph LIVE must follow that range. */
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
			/* Resolve "until EOF" only when we actually rebuild. */
			if (r_end_resolved < 0)
			{
				sz = fs_file_size(tm_path_hunt_csv());
				if (sz >= 0 && g_cached_eof_size == sz && g_cached_eof_lines >= 0)
					r_end_resolved = g_cached_eof_lines;
				else
				{
					r_end_resolved = session_count_data_lines(tm_path_hunt_csv());
					g_cached_eof_size = sz;
					g_cached_eof_lines = r_end_resolved;
				}
			}
			if (r_end_resolved < r_start)
				r_end_resolved = r_start;
			ok = hunt_series_rebuild_range(&g_hs, tm_path_hunt_csv(),
				r_start, r_end_resolved, 60);
			g_ready = ok ? 1 : 0;
			g_last_mode = 1;
			g_last_range_start = r_start;
			g_last_range_end = r_end_resolved;
			g_last_range_end_raw = r_end_raw;
			g_last_offset = -1;
			g_warn_text[0] = '\0';
			if (!ok)
				snprintf(g_warn_text, sizeof(g_warn_text), "WARN: rebuild range failed");
			else if (r_end_resolved > r_start && g_hs.t0 == 0 && g_hs.count == 0
				&& g_hs.shots_total == 0 && g_hs.hits_total == 0
				&& g_hs.kills_total == 0 && g_hs.loot_total_uPED == 0)
				snprintf(g_warn_text, sizeof(g_warn_text), "WARN: range has no parsed data");
			else if (!hunt_series_sanity_check(&g_hs))
				snprintf(g_warn_text, sizeof(g_warn_text), "WARN: series sanity check failed");
		}
		return ;
	}

	offset = session_load_offset(tm_path_session_offset());
	if (!g_ready || g_last_mode != 0 || offset != g_last_offset)
	{
		hunt_series_reset(&g_hs, offset, 60);
		g_last_offset = offset;
		g_ready = 1;
		g_last_mode = 0;
		g_last_range_start = -1;
		g_last_range_end = -1;
		g_last_range_end_raw = -2;
		g_warn_text[0] = '\0';
	}
	/*
	 * hunt_series_update() gere deja le cas "CSV tronque" en re-initialisant
	 * l'etat interne (initialized=0) si file_pos > taille.
	 */
	hunt_series_update(&g_hs, tm_path_hunt_csv());
	if (!hunt_series_sanity_check(&g_hs))
		snprintf(g_warn_text, sizeof(g_warn_text), "WARN: series sanity check failed");
}

const t_hunt_series	*hunt_series_live_get(void)
{
	if (!g_ready)
		return (NULL);
	return (&g_hs);
}

int	hunt_series_live_is_range(void)
{
	return (g_ready && g_last_mode == 1);
}

void	hunt_series_live_get_range(long *out_start, long *out_end_raw,
					long *out_end_resolved)
{
	if (out_start)
		*out_start = g_last_range_start;
	if (out_end_raw)
		*out_end_raw = g_last_range_end_raw;
	if (out_end_resolved)
		*out_end_resolved = g_last_range_end;
}

long	hunt_series_live_get_offset(void)
{
	return (g_last_offset);
}

int	hunt_series_live_has_warning(void)
{
	return (g_warn_text[0] != '\0');
}

const char	*hunt_series_live_warning_text(void)
{
	if (g_warn_text[0] == '\0')
		return (NULL);
	return (g_warn_text);
}

void	hunt_series_live_bootstrap(void)
{
	/*
	 * Step3 (V2 reload):
	 * Au demarrage, on reconstruit le cache a partir du CSV V2 existant.
	 * Cela permet d'avoir les graphes disponibles meme si le parser n'est
	 * pas lance (mode consultation).
	 */
	hunt_series_live_force_reset();
	hunt_series_live_tick();
}
