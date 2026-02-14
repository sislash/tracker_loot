/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   parser_engine.c                                    :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: login <login@student.42.fr>                +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/01/31 00:00:00 by login             #+#    #+#             */
/*   Updated: 2026/01/31 00:00:00 by login            ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "parser_engine.h"
#include "globals_parser.h"
#include "hunt_rules.h"
#include "hunt_csv.h"
#include "fs_utils.h"
#include "ui_utils.h"
#include "monitor_health.h"
#include "utils.h"
#include "sweat_option.h"
#include "core_paths.h"
#include "tm_string.h"
#include "tm_money.h"
#include "eu_economy.h"

#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char	g_my_name[128] = "";

/*
 * Step3: Reliable kill_id linkage.
 * We assign a monotonically increasing kill_id for each KILL event, and we attach
 * subsequent LOOT_ITEM rows to the most recent kill within a short window.
 *
 * This removes ambiguity when multiple kills or loot packets happen at the same
 * second (chat log timestamps are second-granularity).
 */
#define KILL_ATTACH_WINDOW_SEC 60
#define KILL_CTX_MAX 128

/*
 * RCE correctness: loot lines can arrive after a subsequent kill when grinding.
 * A single "current kill" slot is not enough.
 *
 * We keep a small ring of recent kills and attach each LOOT_ITEM to:
 *  - the kill that already received loot at the same second (stabilize multi-loot), else
 *  - the most recent kill in the past within KILL_ATTACH_WINDOW_SEC.
 */
typedef struct s_kill_slot
{
	int64_t	kill_id;
	int64_t	ts_unix;
	int64_t	last_loot_ts_unix;
	int		active;
}	t_kill_slot;

typedef struct s_kill_ctx
{
	t_kill_slot	slots[KILL_CTX_MAX];
	int			head;
	time_t		last_flush_t;
	int			flush_pending;
}	t_kill_ctx;

static void	kill_ctx_reset(t_kill_ctx *k)
{
	if (!k)
		return ;
	memset(k, 0, sizeof(*k));
	k->head = -1;
}

static int	csv_is_critical_event(const char *type)
{
	if (!type || !*type)
		return (0);
	/*
	 * UX LIVE graphs:
	 * - Loot/kill & Kills points must appear as soon as the mob is killed / looted.
	 * - We therefore force a flush on low-frequency "critical" events.
	 */
	if (strncmp(type, "KILL", 4) == 0)
		return (1);
	if (strcmp(type, "LOOT_ITEM") == 0)
		return (1);
	return (0);
}

/*
 * Performance/Durability trade-off:
 * - fflush() on each row is very expensive during intensive hunts (SHOT spam).
 * - We still want a responsive LIVE UI (kills/loot must appear immediately).
 *
 * Policy:
 * - Always flush on critical events (KILL / LOOT_ITEM) -> immediate graph point.
 * - Otherwise: flush at most once per second, or every 64 rows.
 */
static void	csv_maybe_flush(FILE *out, t_kill_ctx *kctx, const char *type)
{
	time_t	now;
	int	rc;
	int	fe;
	int	err;

	if (!out)
		return ;
	if (!kctx)
	{
		rc = fflush(out);
		fe = ferror(out);
		err = errno;
		monitor_health_on_flush(ft_time_ms(), (rc == 0 && fe == 0), err, fe);
		return ;
	}
	now = time(NULL);
	if (csv_is_critical_event(type))
	{
		rc = fflush(out);
		fe = ferror(out);
		err = errno;
		monitor_health_on_flush(ft_time_ms(), (rc == 0 && fe == 0), err, fe);
		kctx->flush_pending = 0;
		kctx->last_flush_t = now;
		return ;
	}
	kctx->flush_pending++;
	if (kctx->flush_pending >= 64 || kctx->last_flush_t != now)
	{
		rc = fflush(out);
		fe = ferror(out);
		err = errno;
		monitor_health_on_flush(ft_time_ms(), (rc == 0 && fe == 0), err, fe);
		kctx->flush_pending = 0;
		kctx->last_flush_t = now;
	}
}

static void	kill_ctx_on_kill(t_kill_ctx *k, int64_t ts_unix, int64_t kill_id)
{
	int	i;

	if (!k)
		return ;
	k->head = (k->head + 1) % KILL_CTX_MAX;
	k->slots[k->head].kill_id = kill_id;
	k->slots[k->head].ts_unix = ts_unix;
	k->slots[k->head].last_loot_ts_unix = 0;
	k->slots[k->head].active = 1;
	/* expire very old slots */
	for (i = 0; i < KILL_CTX_MAX; i++)
	{
		if (k->slots[i].active
			&& ts_unix > k->slots[i].ts_unix
			&& (ts_unix - k->slots[i].ts_unix) > (KILL_ATTACH_WINDOW_SEC * 4))
			k->slots[i].active = 0;
	}
}

static int64_t	kill_ctx_attach_loot(t_kill_ctx *k, int64_t loot_ts_unix)
{
	int		i;
	int64_t	best_id;
	int64_t	best_dt;

	if (!k)
		return (0);
	best_id = 0;
	best_dt = INT64_MAX;
	/* 1) if a kill already received loot at this exact second, keep coherence */
	for (i = 0; i < KILL_CTX_MAX; i++)
	{
		if (k->slots[i].active && k->slots[i].last_loot_ts_unix == loot_ts_unix)
			return (k->slots[i].kill_id);
	}
	/* 2) otherwise choose nearest past kill within window */
	for (i = 0; i < KILL_CTX_MAX; i++)
	{
		int64_t	dt;

		if (!k->slots[i].active)
			continue ;
		if (k->slots[i].ts_unix > loot_ts_unix)
			continue ;
		dt = loot_ts_unix - k->slots[i].ts_unix;
		if (dt > KILL_ATTACH_WINDOW_SEC)
			continue ;
		if (dt < best_dt)
		{
			best_dt = dt;
			best_id = k->slots[i].kill_id;
		}
		else if (dt == best_dt && best_id != 0)
		{
			/* tie-breaker: prefer more recent */
			if (k->slots[i].ts_unix > 0)
				best_id = k->slots[i].kill_id;
		}
	}
	if (best_id)
	{
		for (i = 0; i < KILL_CTX_MAX; i++)
		{
			if (k->slots[i].active && k->slots[i].kill_id == best_id)
			{
				k->slots[i].last_loot_ts_unix = loot_ts_unix;
				break ;
			}
		}
	}
	return (best_id);
}

static void	log_engine_error(const char *ctx, const char *path)
{
	FILE	*f;
	
	f = fopen(tm_path_parser_debug_log(), "ab");
	if (!f)
		return ;
	fprintf(f, "[ENGINE] %s failed: errno=%d (%s) path=[%s]\n",
			ctx, errno, strerror(errno), path ? path : "(null)");
	fclose(f);
}

void	parser_engine_set_player_name(const char *name)
{
	safe_copy(g_my_name, sizeof(g_my_name), name);
	hunt_rules_set_player_name(name);
}

static void	map_globals_to_hunt(t_hunt_event *dst,
								const t_globals_event *src)
{
	memset(dst, 0, sizeof(*dst));
	safe_copy(dst->ts, sizeof(dst->ts), src->ts);
	safe_copy(dst->name, sizeof(dst->name), src->name);
	safe_copy(dst->value, sizeof(dst->value), src->value);
	safe_copy(dst->raw, sizeof(dst->raw), src->raw);
	if (strncmp(src->type, "GLOB_", 5) == 0)
		safe_copy(dst->type, sizeof(dst->type), "GLOBAL");
	else if (strncmp(src->type, "HOF_", 4) == 0)
		safe_copy(dst->type, sizeof(dst->type), "HOF");
	else if (strncmp(src->type, "ATH_", 4) == 0)
		safe_copy(dst->type, sizeof(dst->type), "ATH");
	else
		safe_copy(dst->type, sizeof(dst->type), src->type);
	dst->qty[0] = '\0';
}

static long	parse_long_default0(const char *s)
{
	char	*end;
	long	v;

	if (!s || !*s)
		return (0);
	errno = 0;
	v = strtol(s, &end, 10);
	if (errno != 0 || end == s)
		return (0);
	return (v);
}

static int	extract_value_from_raw_uPED(const char *raw, tm_money_t *out)
{
	const char	*vp;

	if (!raw || !out)
		return (0);
	vp = strstr(raw, "Value:");
	if (!vp)
		vp = strstr(raw, "Valeur:");
	if (!vp)
		return (0);
	vp += 6;
	while (*vp && isspace((unsigned char)*vp))
		vp++;
	return (tm_money_parse_ped(vp, out));
}

static int	append_event(FILE *out, int64_t *kill_id_state, t_kill_ctx *kctx,
					const t_hunt_event *ev)
{
	int64_t		ts_unix;
	long		qty;
	tm_money_t	v;
	int			has_v;
	int64_t		kid;
	uint32_t	flags;

	if (!out || !ev)
		return (-1);
	/* V2 strict */
	ts_unix = 0;
	if (!hunt_csv_ts_text_to_unix(ev->ts, &ts_unix))
		ts_unix = (int64_t)time(NULL);
	qty = parse_long_default0(ev->qty);
	/* fixed-point value */
	v = 0;
	has_v = 0;
	if (strcmp(ev->type, "SWEAT") == 0)
	{
		if (qty > 0)
		{
			v = (tm_money_t)qty * (tm_money_t)EU_SWEAT_uPED_PER_BOTTLE;
			has_v = 1;
		}
	}
	else
	{
		has_v = tm_money_parse_ped(ev->value, &v);
		if (!has_v && ev->raw[0])
			has_v = extract_value_from_raw_uPED(ev->raw, &v);
	}
	/* kill_id: assign on KILL; attach LOOT_ITEM to best recent kill (ring-buffer) */
	kid = 0;
	if (strncmp(ev->type, "KILL", 4) == 0 && kill_id_state)
	{
		kid = ++(*kill_id_state);
		if (kctx)
			kill_ctx_on_kill(kctx, ts_unix, kid);
	}
	else if (strcmp(ev->type, "LOOT_ITEM") == 0 && kctx)
	{
		kid = kill_ctx_attach_loot(kctx, ts_unix);
	}
	flags = (has_v ? 1u : 0u);
	if (kid > 0)
		flags |= (1u << 1);
	hunt_csv_write_v2(out, ts_unix, ev->type, ev->name, qty,
					v, kid, flags, ev->raw);
	/* Health: parser is alive as soon as an event is validated. */
	monitor_health_on_event(ft_time_ms());
	if (ferror(out))
		monitor_health_on_io_error("CSV write", errno, ferror(out));
	/*
	 * IMPORTANT (LIVE graph UX):
	 * Force flush on KILL/LOOT_ITEM so points appear immediately after a kill,
	 * not only after the next SHOT (buffered IO).
	 */
	csv_maybe_flush(out, kctx, ev->type);
	return (0);
}

static int	backup_legacy_hunt_csv(const char *csv_path, const char *suffix)
{
	char	bak[1024];

	if (!csv_path || !suffix)
		return (0);
	snprintf(bak, sizeof(bak), "%s%s", csv_path, suffix);
	/* On Windows, rename() fails if destination exists. */
	(void)remove(bak);
	if (rename(csv_path, bak) != 0)
		return (0);
	return (1);
}

static int	try_process_globals(FILE *out, int64_t *kill_id_state,
						t_kill_ctx *kctx, const char *line)
{
	t_globals_event	gev;
	t_hunt_event		ev;
	
	if (globals_parse_line(line, &gev) != 1)
		return (0);
	if (g_my_name[0] && !strstr(gev.raw, g_my_name))
		return (1);
	map_globals_to_hunt(&ev, &gev);
	append_event(out, kill_id_state, kctx, &ev);
	return (1);
}

static void	flush_pending_hunt(FILE *out, int64_t *kill_id_state,
						t_kill_ctx *kctx, t_hunt_event *ev)
{
	while (hunt_pending_pop(ev))
		append_event(out, kill_id_state, kctx, ev);
}

static void	process_hunt(FILE *out, int64_t *kill_id_state,
					t_kill_ctx *kctx, const char *line)
{
	t_hunt_event	ev;
	int			ret;
	int			sweat_enabled;

	if (hunt_should_ignore_line(line))
		return ;
	ret = hunt_parse_line(line, &ev);
	if (ret < 0)
	{
		monitor_health_on_parse_error("hunt_parse_line");
		return ;
	}
	if (strcmp(ev.type, "SWEAT") == 0)
	{
		sweat_enabled = 0;
		sweat_option_load(tm_path_options_cfg(), &sweat_enabled);
		if (!sweat_enabled)
			return ;
	}
	if (ret == 1)
	{
		t_hunt_event	pend;

		if (hunt_pending_pop(&pend))
			append_event(out, kill_id_state, kctx, &pend);
	}
	append_event(out, kill_id_state, kctx, &ev);
	flush_pending_hunt(out, kill_id_state, kctx, &ev);
}

static void	process_line(FILE *out, int64_t *kill_id_state,
					t_kill_ctx *kctx, const char *line)
{
	if (try_process_globals(out, kill_id_state, kctx, line))
		return ;
	process_hunt(out, kill_id_state, kctx, line);
}

static int	open_io_files(FILE **in, FILE **out,
					  const char *chatlog_path, const char *csv_path)
{
	char	line[256];

	*in = fs_fopen_shared_read(chatlog_path);
	if (!*in)
	{
		log_engine_error("open chatlog", chatlog_path);
		monitor_health_on_io_error("open chatlog", errno, 0);
		return (-1);
	}
	*out = fopen(csv_path, "ab+");
	if (!*out)
	{
		log_engine_error("open csv", csv_path);
		monitor_health_on_io_error("open csv", errno, 0);
		fclose(*in);
		*in = NULL;
		return (-1);
	}
	monitor_health_reset(chatlog_path, csv_path);
	/* Big buffered IO for intensive sessions (flushed by csv_maybe_flush). */
	(void)setvbuf(*out, NULL, _IOFBF, 1 << 20);
	/*
	 * V2 strict only.
	 * If the file header isn't V2, we backup it and recreate a clean V2 CSV.
	 */
	(void)fseek(*out, 0, SEEK_SET);
	if (!fgets(line, sizeof(line), *out))
	{
		/* empty -> create v2 */
		hunt_csv_ensure_header_v2(*out);
	}
	else if (strstr(line, "timestamp_unix") || strstr(line, "value_uPED"))
	{
		/* ok */
	}
	else
	{
		/* Legacy / unknown header => backup and restart in V2 */
		fclose(*out);
		*out = NULL;
		if (!backup_legacy_hunt_csv(csv_path, ".legacy.bak"))
		{
			log_engine_error("backup legacy hunt csv", csv_path);
			monitor_health_on_io_error("backup legacy hunt csv", errno, 0);
			fclose(*in);
			*in = NULL;
			return (-1);
		}
		*out = fopen(csv_path, "ab+");
		if (!*out)
		{
			log_engine_error("open csv after backup", csv_path);
			monitor_health_on_io_error("open csv after backup", errno, 0);
			fclose(*in);
			*in = NULL;
			return (-1);
		}
		(void)setvbuf(*out, NULL, _IOFBF, 1 << 20);
		hunt_csv_ensure_header_v2(*out);
	}
	/* Crash recovery: drop a partially written last line (no trailing \n). */
	(void)hunt_csv_repair_trailing_partial_line(*out);
	/* If repair truncated the file to 0, recreate a clean V2 header. */
	hunt_csv_ensure_header_v2(*out);
	(void)fseek(*out, 0, SEEK_END);
	csv_maybe_flush(*out, NULL, NULL);
	monitor_health_update_io(ft_time_ms(), fs_file_size(chatlog_path), ftell(*in), fs_file_size(csv_path), 0);
	return (0);
}


static int	replay_loop(FILE *in, FILE *out, int64_t *kill_id_state,
					t_kill_ctx *kctx, atomic_int *stop_flag)
{
	char	buf[2048];

	while (!stop_flag || (atomic_load(stop_flag) == 0))
	{
		if (!fgets(buf, sizeof(buf), in))
			break ;
		process_line(out, kill_id_state, kctx, buf);
	}
	return (0);
}

int	parser_run_replay(const char *chatlog_path, const char *csv_path,
					  atomic_int *stop_flag)
{
	FILE	*in;
	FILE	*out;
	int64_t	kill_id_state;
	t_kill_ctx	kctx;
	
	kill_id_state = 0;
	kill_ctx_reset(&kctx);
	if (open_io_files(&in, &out, chatlog_path, csv_path) < 0)
		return (-1);
	kill_id_state = hunt_csv_tail_max_kill_id(csv_path);
	replay_loop(in, out, &kill_id_state, &kctx, stop_flag);
	fclose(out);
	fclose(in);
	return (0);
}

static int	reopen_if_rotated(FILE **in, const char *path,
					long *last_pos, const char *csv_path)
{
	long	sz;
	int	rotated;
	uint64_t	now_ms;

	rotated = 0;
	now_ms = ft_time_ms();
	if (!in || !*in || !path || !last_pos)
		return (0);
	clearerr(*in);
	sz = fs_file_size(path);
	if (sz >= 0 && sz < *last_pos)
	{
		/* chat.log rotated or truncated */
		rotated = 1;
		fclose(*in);
		*in = fs_fopen_shared_read(path);
		if (!*in)
		{
			monitor_health_on_io_error("reopen chatlog", errno, 0);
			ui_sleep_ms(250);
			return (-1);
		}
		fseek(*in, 0, SEEK_END);
		*last_pos = ftell(*in);
	}
	else if (sz >= 0 && sz > *last_pos)
		fseek(*in, *last_pos, SEEK_SET);
	else
	{
		ui_sleep_ms(200);
		fseek(*in, *last_pos, SEEK_SET);
	}
	if (csv_path)
		monitor_health_update_io(now_ms, sz, *last_pos, fs_file_size(csv_path), rotated);
	else
		monitor_health_update_io(now_ms, sz, *last_pos, 0, rotated);
	return (rotated);
}

static void	live_loop(FILE *in, FILE *out, int64_t *kill_id_state,
					t_kill_ctx *kctx, const char *path, const char *csv_path, atomic_int *stop_flag)
{
	char		buf[2048];
	long		last_pos;
	uint64_t	last_io_ms;
	uint64_t	now_ms;

	last_io_ms = 0;
	fseek(in, 0, SEEK_END);
	last_pos = ftell(in);
	now_ms = ft_time_ms();
	monitor_health_update_io(now_ms, fs_file_size(path), last_pos, csv_path ? fs_file_size(csv_path) : 0, 0);
	last_io_ms = now_ms;
	while (!stop_flag || (atomic_load(stop_flag) == 0))
	{
		if (fgets(buf, sizeof(buf), in))
		{
			process_line(out, kill_id_state, kctx, buf);
			last_pos = ftell(in);
		}
		else
			(void)reopen_if_rotated(&in, path, &last_pos, csv_path);

		now_ms = ft_time_ms();
		if (now_ms - last_io_ms >= 1000)
		{
			monitor_health_update_io(now_ms, fs_file_size(path), last_pos, csv_path ? fs_file_size(csv_path) : 0, 0);
			last_io_ms = now_ms;
		}
	}
}

int	parser_run_live(const char *chatlog_path, const char *csv_path,
					atomic_int *stop_flag)
{
	FILE	*in;
	FILE	*out;
	int64_t	kill_id_state;
	t_kill_ctx	kctx;
	
	kill_id_state = 0;
	kill_ctx_reset(&kctx);
	if (open_io_files(&in, &out, chatlog_path, csv_path) < 0)
		return (-1);
	kill_id_state = hunt_csv_tail_max_kill_id(csv_path);
	live_loop(in, out, &kill_id_state, &kctx, chatlog_path, csv_path, stop_flag);
	fclose(out);
	fclose(in);
	return (0);
}
