#include "overlay.h"

#include "ui_widgets.h"
#include "ui_theme.h"

#include "utils.h"
#include "session.h"
#include "core_paths.h"
#include "parser_thread.h"
#include "session_export.h"
#include "hunt_series_live.h"
#include "tracker_stats_live.h"

#include "mob_prompt.h"

#include "mob_selected.h"
#include "tm_money.h"
#include "tm_string.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

static int		g_overlay_on = 0;
static t_window	g_ow;

/* Overlay-specific theme: lower contrast / more discreet than main UI. */
static t_theme	g_overlay_theme;

/* Shared session clock + stats cache so overlay can update from any screen. */
static uint64_t		g_session_start_ms = 0;
static int			g_session_clock_dirty = 0; /* overlay changed the clock; needs to be pushed out */

static uint64_t		g_last_stats_ms = 0;
static t_hunt_stats	g_cached_stats;
static int			g_cached_ok = 0;

/* Lightweight feedback (toast) */
static char		g_toast[160];
static uint64_t		g_toast_until_ms = 0;

/* Mob (cible) requis avant START */
static char		g_mob_name[128] = "";

static void	overlay_toastf(const char *fmt, ...)
{
	va_list ap;
	uint64_t now;

	va_start(ap, fmt);
	vsnprintf(g_toast, sizeof(g_toast), fmt, ap);
	va_end(ap);
	now = ft_time_ms();
	g_toast_until_ms = now + 2200;
}

static void	fmt_time(char *out, int outsz, unsigned long long ms)
{
	unsigned long long s = ms / 1000ULL;
	unsigned long long h = s / 3600ULL;
	unsigned long long m = (s % 3600ULL) / 60ULL;
	unsigned long long sec = (s % 60ULL);
	snprintf(out, outsz, "%02llu:%02llu:%02llu", h, m, sec);
}

static double	ratio_pct_local(tm_money_t num, tm_money_t den)
{
	if (den <= 0)
		return (0.0);
	return (((double)num / (double)den) * 100.0);
}

int	overlay_is_enabled(void)
{
	return (g_overlay_on);
}

void	overlay_set_session_start_ms(uint64_t ms)
{
	g_session_start_ms = ms;
	/* Reset cached stats timing when session resets. */
	if (ms == 0)
	{
		g_last_stats_ms = 0;
		g_cached_ok = 0;
		memset(&g_cached_stats, 0, sizeof(g_cached_stats));
	}
}

uint64_t	overlay_get_session_start_ms(void)
{
	return (g_session_start_ms);
}

void	overlay_sync_session_clock(uint64_t *io_session_start_ms)
{
	if (!io_session_start_ms)
		return;
	/* If overlay changed the clock (buttons), push it back to the caller once. */
	if (g_session_clock_dirty)
	{
		*io_session_start_ms = g_session_start_ms;
		g_session_clock_dirty = 0;
		return;
	}
	/* Otherwise keep overlay aligned with the app clock. */
	overlay_set_session_start_ms(*io_session_start_ms);
}

void	overlay_toggle(void)
{
	if (!g_overlay_on)
	{
		/* Compact & discreet overlay by default.
		 * The overlay is meant to stay on top of the game with minimal distraction.
		 */
		if (window_init_overlay(&g_ow, "tracker_loot_overlay", 420, 180, 1, 125) != 0)
			return;
		g_overlay_on = 1;
		/* Derive a softer theme for overlay (reduced borders / softer surfaces). */
		g_overlay_theme = g_theme_dark;
		g_overlay_theme.surface = ui_color_lerp(g_overlay_theme.surface, g_overlay_theme.bg, 110);
		g_overlay_theme.surface2 = ui_color_lerp(g_overlay_theme.surface2, g_overlay_theme.bg, 120);
		g_overlay_theme.border = ui_color_lerp(g_overlay_theme.border, g_overlay_theme.bg, 200);
		g_mob_name[0] = '\0';
		(void)mob_selected_load(tm_path_mob_selected(), g_mob_name, sizeof(g_mob_name));
		mob_selected_sanitize(g_mob_name);
		if (g_session_start_ms == 0)
			g_session_start_ms = ft_time_ms();
	}
	else
	{
		g_overlay_on = 0;
		window_destroy(&g_ow);
	}
}

static void	overlay_action_start(void)
{
	int rc;
	char mob[128];

	if (parser_thread_is_running())
		return;
	snprintf(mob, sizeof(mob), "%s", g_mob_name);
	mob_selected_sanitize(mob);
	if (mob[0] == '\0')
	{
		/* Overlay is now discreet: no permanent mob row.
		 * If mob is missing, ask once (modal) then persist.
		 */
		char tmp[128];
		tmp[0] = '\0';
		if (!mob_prompt_ensure(&g_ow, tmp, sizeof(tmp)))
		{
			overlay_toastf("[MOB] annule");
			return;
		}
		safe_copy(g_mob_name, sizeof(g_mob_name), tmp);
		snprintf(mob, sizeof(mob), "%s", g_mob_name);
		mob_selected_sanitize(mob);
	}
	/* Persiste au cas ou (overlay input). */
	(void)mob_selected_save(tm_path_mob_selected(), mob);
	rc = parser_thread_start_live();
	if (rc == 0 && parser_thread_is_running())
	{
		if (g_session_start_ms == 0)
		{
			overlay_set_session_start_ms(ft_time_ms());
			g_session_clock_dirty = 1;
		}
		overlay_toastf("OK : START (LIVE)");
	}
	else
		overlay_toastf("[ERROR] START impossible");
}

static void	overlay_action_stop(int reset_clock)
{
	parser_thread_stop();
	if (reset_clock)
	{
		overlay_set_session_start_ms(0);
		g_session_clock_dirty = 1;
	}
	overlay_toastf("OK : STOP");
}

static void	overlay_action_stop_export(void)
{
	t_hunt_stats	export_stats;
	long		start_off;
	long		end_off;
	char		ts_start[64];
	char		ts_end[64];

	parser_thread_stop();
	start_off = session_load_offset(tm_path_session_offset());
	end_off = session_count_data_lines(tm_path_hunt_csv());
	memset(&export_stats, 0, sizeof(export_stats));
	memset(ts_start, 0, sizeof(ts_start));
	memset(ts_end, 0, sizeof(ts_end));
	if (tracker_stats_compute_range(tm_path_hunt_csv(), start_off, end_off, &export_stats) != 0)
	{
		overlay_toastf("[ERROR] export: compute stats");
		return;
	}
	session_extract_range_timestamps_ex(tm_path_hunt_csv(), start_off, end_off,
			ts_start, sizeof(ts_start), ts_end, sizeof(ts_end));
	if (session_export_stats_csv_ex(tm_path_sessions_stats_csv(),
			&export_stats, ts_start, ts_end, start_off, end_off))
		overlay_toastf("OK : STOP+EXPORT (offset=%ld)", end_off);
	else
		overlay_toastf("[WARN] export CSV impossible");
	session_save_offset(tm_path_session_offset(), end_off);
	/* Force la serie LIVE a re-demarrer sur le nouvel offset. */
	hunt_series_live_force_reset();
	/* Fin de session => obliger la saisie du mob au prochain Start */
	mob_selected_clear(tm_path_mob_selected());
	g_mob_name[0] = '\0';
	/* Reset session clock */
	overlay_set_session_start_ms(0);
	g_session_clock_dirty = 1;
}

void	overlay_tick(const t_hunt_stats *s, unsigned long long elapsed_ms)
{
	t_ui_state	ui;
	t_rect		r;
	char		v_loot[32];
	char		v_exp[32];
	char		v_ret[32];
	char		v_mobs[32];
	char		v_time[32];
	char		v_parser[48];
	int			pad;
	int			gap;
	int			header_h;
	int			btn_h;
	int			col_w;
	int			row_h;
	int			y;
	int			y_cards;
	int			y_btn;
	int			running;

	if (!g_overlay_on)
		return;
	if (!g_ow.running)
	{
		g_overlay_on = 0;
		return;
	}
	window_poll_events(&g_ow);
	ui.theme = &g_overlay_theme;
	window_clear(&g_ow, ui.theme->bg);

	/* Compact layout: header + 2x2 stats + buttons.
	 * Mob name is not displayed anymore (overlay stays discreet).
	 * Toast is drawn floating above buttons (no reserved height).
	 */
	pad = 8;
	gap = 6;
	header_h = 18;
	btn_h = 26;
	col_w = (g_ow.width - pad * 2 - gap) / 2;
	/* compute card height from remaining space */
	y = pad;
	y += header_h + gap;
	y_cards = y;
	row_h = (g_ow.height - y_cards - btn_h - pad - gap) / 2;
	if (row_h < 36)
		row_h = 36;
	y_btn = g_ow.height - pad - btn_h;

	running = parser_thread_is_running();

	/* Pre-format values.
	 * IMPORTANT: do not reuse the same buffer for multiple ui_card() calls.
	 */
	if (s)
	{
			tm_money_format_ped4(v_loot, sizeof(v_loot), s->loot_ped);
			tm_money_format_ped4(v_exp, sizeof(v_exp), s->expense_used);
		snprintf(v_ret, sizeof(v_ret), "%.2f%%",
				ratio_pct_local(s->loot_ped, s->expense_used));
		snprintf(v_mobs, sizeof(v_mobs), "%ld", s->kills);
	}
	else
	{
		snprintf(v_loot, sizeof(v_loot), "0.0000");
		snprintf(v_exp, sizeof(v_exp), "0.0000");
		snprintf(v_ret, sizeof(v_ret), "0.00%%");
		snprintf(v_mobs, sizeof(v_mobs), "0");
	}
	fmt_time(v_time, sizeof(v_time), elapsed_ms);
	snprintf(v_parser, sizeof(v_parser), "%s", running ? "EN COURS" : "ARRETE");

	/* Outer panel: keep it subtle (border is blended into bg in overlay theme). */
	ui_draw_panel(&g_ow, (t_rect){0, 0, g_ow.width, g_ow.height}, ui.theme->bg, ui.theme->border);

	/* Header line (compact): time + parser state */
	{
		char hdr_right[96];
		unsigned int st = running ? ui.theme->success : ui.theme->warn;
		int rx;
		snprintf(hdr_right, sizeof(hdr_right), "%s | %s", v_time, v_parser);
		ui_draw_text(&g_ow, pad, pad + 4, "tracker_loot", ui.theme->text2);
		/* Right aligned-ish: simple monospace positioning */
		rx = g_ow.width - pad - (int)strlen(hdr_right) * 9;
		if (rx < pad)
			rx = pad;
		ui_draw_text(&g_ow, rx, pad + 4, hdr_right, st);
	}

	/* Mob name hidden: kept internally (saved in mob_selected.cfg) for START checks. */

	/* 2x2 stat cards (compact) */
	{
		int x1 = pad;
		int x2 = pad + col_w + gap;
		int y1 = y_cards;
		int y2 = y_cards + row_h + gap;
		/* Card 1: Loot */
		r = (t_rect){x1, y1, col_w, row_h};
		ui_draw_panel(&g_ow, r, ui.theme->surface, ui.theme->border);
		ui_draw_text(&g_ow, r.x + 10, r.y + 6, "TT Retour", ui.theme->text2);
		ui_draw_text(&g_ow, r.x + 10, r.y + (r.h - 18), v_loot, ui.theme->success);
		/* Card 2: Expense */
		r = (t_rect){x2, y1, col_w, row_h};
		ui_draw_panel(&g_ow, r, ui.theme->surface, ui.theme->border);
		ui_draw_text(&g_ow, r.x + 10, r.y + 6, "TT Depense", ui.theme->text2);
		ui_draw_text(&g_ow, r.x + 10, r.y + (r.h - 18), v_exp, ui.theme->warn);
		/* Card 3: Return % */
		r = (t_rect){x1, y2, col_w, row_h};
		ui_draw_panel(&g_ow, r, ui.theme->surface, ui.theme->border);
		ui_draw_text(&g_ow, r.x + 10, r.y + 6, "% Retour", ui.theme->text2);
		ui_draw_text(&g_ow, r.x + 10, r.y + (r.h - 18), v_ret, ui.theme->accent);
		/* Card 4: Kills */
		r = (t_rect){x2, y2, col_w, row_h};
		ui_draw_panel(&g_ow, r, ui.theme->surface, ui.theme->border);
		ui_draw_text(&g_ow, r.x + 10, r.y + 6, "Mobs", ui.theme->text2);
		ui_draw_text(&g_ow, r.x + 10, r.y + (r.h - 18), v_mobs, ui.theme->text);
	}

	/* Toast (floating above buttons; keeps overlay compact) */
	if (ft_time_ms() < g_toast_until_ms)
		ui_draw_text(&g_ow, pad, y_btn - 16, g_toast, ui.theme->text2);

	/* Buttons (two-button layout for clarity + larger hitboxes) */
	{
		int bx = pad;
		int by = y_btn;
		int bw = col_w;
		int bh = btn_h;
		if (!running)
		{
			if (ui_button(&g_ow, &ui, (t_rect){bx, by, bw, bh}, "Start", UI_BTN_PRIMARY, 1))
				overlay_action_start();
			bx += bw + gap;
			if (ui_button(&g_ow, &ui, (t_rect){bx, by, bw, bh}, "Export", UI_BTN_SECONDARY, 1))
				overlay_action_stop_export();
		}
		else
		{
			if (ui_button(&g_ow, &ui, (t_rect){bx, by, bw, bh}, "Stop", UI_BTN_SECONDARY, 1))
				overlay_action_stop(1);
			bx += bw + gap;
			if (ui_button(&g_ow, &ui, (t_rect){bx, by, bw, bh}, "Stop+Export", UI_BTN_PRIMARY, 1))
				overlay_action_stop_export();
		}
	}

	window_present(&g_ow);
}

void	overlay_tick_auto_hunt(void)
{
	uint64_t					now;
	unsigned long long		elapsed;
	const t_hunt_stats		*ps;

	if (!overlay_is_enabled())
		return;
	now = ft_time_ms();
	if (g_session_start_ms == 0)
		g_session_start_ms = now;
	elapsed = (unsigned long long)(now - g_session_start_ms);
	if (g_last_stats_ms == 0 || now - g_last_stats_ms >= 250)
	{
		tracker_stats_live_tick();
		ps = tracker_stats_live_get();
		memset(&g_cached_stats, 0, sizeof(g_cached_stats));
		if (ps)
		{
			g_cached_stats = *ps;
			g_cached_ok = 1;
		}
		else
			g_cached_ok = 0;
		g_last_stats_ms = now;
	}
	overlay_tick(g_cached_ok ? &g_cached_stats : NULL, elapsed);
}
