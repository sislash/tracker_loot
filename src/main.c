/* ************************************************************************** */
/*                                                                            */
/*                                                                            */
/*   main.c                                                                   */
/*                                                                            */
/*   By: you <you@student.42.fr>                                              */
/*                                                                            */
/*   Created: 2026/02/04                                                      */
/*   Updated: 2026/02/04                                                      */
/*                                                                            */
/* ************************************************************************** */

#include "menu_config.h" /* menus INI existants (armes/markup) */
#include "menu_principale.h" /* stop_all_parsers() */

#include "utils.h"
#include "frame_limiter.h"

#include "ui_layout.h"
#include "ui_chrome.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "ui_utils.h"

/* For clipped rendering inside scrollable panels */
#include "window.h"

/* Health (operational trust) */
#include "monitor_health.h"

#include "screen_graph_live.h"
#include "hunt_series_live.h"
#include "tracker_stats_live.h"
#include "hunt_csv.h"

#include "parser_thread.h"
#include "globals_thread.h"
#include "tracker_stats.h"
#include "globals_stats.h"
#include "session.h"
#include "session_export.h"
#include "sessions_catalog.h"
#include "weapon_selected.h"
#include "mob_selected.h"
#include "mob_prompt.h"
#include "config_arme.h"
#include "overlay.h"
#include "sweat_option.h"
#include "csv.h"
#include "chatlog_path.h"
#include "core_paths.h"
#include "fs_utils.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* -------------------------------------------------------------------------- */
/* Nouvelle structure UI (Sidebar = navigation, Topbar = actions rapides,      */
/* Content = pages "riches")                                                 */
/* -------------------------------------------------------------------------- */

typedef enum e_page
{
	PAGE_DASHBOARD = 0,
	PAGE_CHASSE,
	PAGE_GLOBALS,
	PAGE_SESSIONS,
	PAGE_CONFIG,
	PAGE_MAINTENANCE,
	PAGE_AIDE,
	PAGE_HEALTH
}	t_page;

typedef struct s_app
{
	t_page		page;
	t_page		prev_page; /* for H toggle */
	int			nav_cursor; /* 0..(top+bottom-1) */
	int			nav_scroll; /* px scroll for main sidebar (top section) */
	int			hunt_mode_live;
	int			globals_mode_live;
	uint64_t	session_start_ms;

	/* caches */
	uint64_t	last_refresh_ms;
	long		last_offset;
	t_hunt_stats	hunt_stats;
	int			hunt_stats_ok;
	t_globals_stats	globals_stats;
	int			globals_stats_ok;

	char		weapon_name[128];
	int			sweat_enabled;

	/* feed */
	char		hunt_feed_buf[32][256];
	const char	*hunt_feed_lines[32];
	int			hunt_feed_n;
	int			hunt_feed_scroll;
	int			hunt_info_scroll;

	char		globals_feed_buf[24][256];
	const char	*globals_feed_lines[24];
	int			globals_feed_n;
	int			globals_feed_scroll;
	int			globals_info_scroll;

	char		sessions_feed_buf[20][256];
	const char	*sessions_feed_lines[20];
	int			sessions_feed_n;
	int			sessions_feed_scroll;

	/* help page */
	int			help_scroll;

	/* weapon picker overlay */
	int			weapon_picker_open;
	armes_db	weapon_db;
	int			weapon_db_loaded;
	int			weapon_picker_selected;
	int			weapon_picker_scroll;

	/* session picker overlay */
	int			session_picker_open;
	t_sessions_list	session_list;
	int			session_list_loaded;
	int			session_picker_selected;
	int			session_picker_scroll;

	/* session range view (loaded session) */
	int			session_range_active;
	long		session_range_start;
	long		session_range_end;
}	t_app;

/* Sidebar index mapping (keep in sync with app_sidebar_nav()). */
static int	page_to_nav_cursor(t_page p)
{
	/* top: 0..4, bottom: 5.. */
	if (p == PAGE_CHASSE)
		return (0);
	if (p == PAGE_GLOBALS)
		return (1);
	if (p == PAGE_SESSIONS)
		return (3);
	if (p == PAGE_CONFIG)
		return (4);
	if (p == PAGE_HEALTH)
		return (5);
	if (p == PAGE_MAINTENANCE)
		return (6);
	if (p == PAGE_AIDE)
		return (7);
	return (0);
}

static void	fmt_hms(char *out, size_t outsz, uint64_t ms)
{
	uint64_t	s;
	uint64_t	h;
	uint64_t	m;
	uint64_t	sec;

	if (!out || outsz == 0)
		return ;
	s = ms / 1000ULL;
	h = s / 3600ULL;
	m = (s % 3600ULL) / 60ULL;
	sec = (s % 60ULL);
	snprintf(out, outsz, "%02llu:%02llu:%02llu",
			(unsigned long long)h, (unsigned long long)m,
			(unsigned long long)sec);
}

/* Sidebar index mapping (keep explicit, do not rely on enum ordering). */
/* -------------------------------------------------------------------------- */
/* Scrollable panel helpers (clipped rendering)                               */
/* -------------------------------------------------------------------------- */

static int	in_rect_local(int x, int y, t_rect r)
{
	return (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h);
}

static int	rect_intersect(t_rect a, t_rect b, t_rect *out)
{
	int x1 = (a.x > b.x) ? a.x : b.x;
	int y1 = (a.y > b.y) ? a.y : b.y;
	int x2 = ((a.x + a.w) < (b.x + b.w)) ? (a.x + a.w) : (b.x + b.w);
	int y2 = ((a.y + a.h) < (b.y + b.h)) ? (a.y + a.h) : (b.y + b.h);
	if (x2 <= x1 || y2 <= y1)
		return (0);
	if (out)
		*out = (t_rect){x1, y1, x2 - x1, y2 - y1};
	return (1);
}

static void	fill_rect_clipped(t_window *w, t_rect r, t_rect clip, unsigned int color)
{
	t_rect inter;
	if (!w)
		return;
	if (!rect_intersect(r, clip, &inter))
		return;
	window_fill_rect(w, inter.x, inter.y, inter.w, inter.h, color);
}

static void	ui_draw_panel_clipped(t_window *w, t_rect r,
					unsigned int bg, unsigned int border, t_rect clip)
{
	fill_rect_clipped(w, r, clip, bg);
	/* Border (each edge clipped) */
	fill_rect_clipped(w, (t_rect){r.x, r.y, r.w, 1}, clip, border);
	fill_rect_clipped(w, (t_rect){r.x, r.y + r.h - 1, r.w, 1}, clip, border);
	fill_rect_clipped(w, (t_rect){r.x, r.y, 1, r.h}, clip, border);
	fill_rect_clipped(w, (t_rect){r.x + r.w - 1, r.y, 1, r.h}, clip, border);
}

static void	ui_draw_text_clipped(t_window *w, int x, int y, const char *txt,
					unsigned int color, t_rect clip)
{
	/* Approx font height ~= 12px in this UI */
	if (!w || !txt)
		return;
	if (y < clip.y || y > clip.y + clip.h - 12)
		return;
	window_draw_text(w, x, y, txt, color);
}

/* -------------------------------------------------------------------------- */
/* Local helper: draw a single line truncated with "..."                      */
/* (We keep it local to avoid changing the public widget API.)                */
/* -------------------------------------------------------------------------- */

static void	ui_draw_text_ellipsis_local(t_window *w, int x, int y,
						const char *text, unsigned int color,
						int max_w_px, int font_px)
{
	char	buf[512];
	int		len;
	int		max_chars;
	int		keep;
	int		i;

	if (!w || !text || max_w_px <= 0)
		return;
	if (font_px <= 0)
		font_px = 14;
	if (ui_measure_text_w(text, font_px) <= max_w_px)
	{
		window_draw_text(w, x, y, text, color);
		return;
	}
	/* Approx. number of glyphs that can fit (same heuristic as ui_widgets.c). */
	max_chars = (int)((double)max_w_px / ((double)font_px * 0.60) + 0.5);
	if (max_chars < 4)
		return;
	if (max_chars > (int)sizeof(buf) - 1)
		max_chars = (int)sizeof(buf) - 1;
	keep = max_chars - 3;
	len = (int)strlen(text);
	if (keep > len)
		keep = len;
	i = 0;
	while (i < keep)
	{
		buf[i] = text[i];
		i++;
	}
	buf[i++] = '.';
	buf[i++] = '.';
	buf[i++] = '.';
	buf[i] = '\0';
	window_draw_text(w, x, y, buf, color);
}

static void	ui_card_clipped(t_window *w, t_ui_state *ui, t_rect r,
					const char *title, const char *value,
					unsigned int value_color, t_rect clip)
{
	if (!w || !ui || !ui->theme)
		return;
	ui_draw_panel_clipped(w, r, ui->theme->surface, ui->theme->border, clip);
	ui_draw_text_clipped(w, r.x + 12, r.y + 10, title, ui->theme->text2, clip);
	ui_draw_text_clipped(w, r.x + 12, r.y + 28, value, value_color, clip);
}

static void	ui_section_header_clipped(t_window *w, t_ui_state *ui, t_rect r,
					const char *title, t_rect clip)
{
	unsigned int bg;
	unsigned int bd;
	unsigned int fg;

	if (!w || !ui || !ui->theme)
		return;
	bg = ui->theme->surface2;
	bd = ui->theme->border;
	fg = ui->theme->text;
	ui_draw_panel_clipped(w, r, bg, bd, clip);
	if (title)
		ui_draw_text_clipped(w, r.x + 10, r.y + (r.h / 2 - 6), title, fg, clip);
}

static int	looks_like_any_csv_header(const char *line)
{
	if (!line)
		return (0);
	if ((strstr(line, "timestamp") || strstr(line, "timestamp_unix"))
		&& (strstr(line, "event_type") || strstr(line, ",type,")))
		return (1);
	if (strstr(line, "session_start") && strstr(line, "session_end"))
		return (1);
	if (strstr(line, "session_start,") && strstr(line, "kills"))
		return (1);
	return (0);
}

static int	csv_format_line(char *dst, size_t dstsz, const char *line)
{
	char			work[1024];
	t_hunt_csv_row_view	row;
	char			ts[64];
	char			val[64];
	char			qty[32];

	if (!dst || dstsz == 0)
		return (0);
	if (!line)
	{
		dst[0] = '\0';
		return (0);
	}
	/* First try: Hunt CSV (V2 strict) pretty formatting */
	snprintf(work, sizeof(work), "%s", line);
	if (hunt_csv_parse_row_inplace(work, &row) && row.type && row.type[0])
	{
		hunt_csv_format_ts_local(ts, sizeof(ts), row.ts_unix);
		if (ts[0] == '\0')
			snprintf(ts, sizeof(ts), "%lld", (long long)row.ts_unix);
		if (row.has_value)
			tm_money_format_ped4(val, sizeof(val), row.value_uPED);
		else
			snprintf(val, sizeof(val), "-");
		if (row.qty != 0)
			snprintf(qty, sizeof(qty), "x%ld", row.qty);
		else
			qty[0] = '\0';
		if (row.kill_id > 0)
			snprintf(dst, dstsz, "%s  %-12s  %s  %-6s  %s  #%lld",
				ts, row.type, row.name, qty, val, (long long)row.kill_id);
		else
			snprintf(dst, dstsz, "%s  %-12s  %s  %-6s  %s",
				ts, row.type, row.name, qty, val);
		return (1);
	}

	/* Fallback: generic CSV compact */
	{
		char	buf[1024];
		char	*fields[6];
		int	n;
		snprintf(buf, sizeof(buf), "%s", line);
		n = csv_split_n(buf, fields, 6);
		if (n < 2)
		{
			snprintf(dst, dstsz, "%s", line);
			return (1);
		}
		snprintf(ts, sizeof(ts), "%s", fields[0] ? fields[0] : "");
		if (strlen(ts) > 19)
			ts[19] = '\0';
		if (n >= 5)
			snprintf(dst, dstsz, "%s  %-10s  %s  %s  %s",
				ts,
				fields[1] ? fields[1] : "",
				fields[2] ? fields[2] : "",
				fields[3] ? fields[3] : "",
				fields[4] ? fields[4] : "");
		else
			snprintf(dst, dstsz, "%s  %-10s  %s",
				ts,
				fields[1] ? fields[1] : "",
				fields[2] ? fields[2] : "");
		return (1);
	}
}

static int	file_tail_csv_formatted(const char *path, int max_lines,
							  char out[][256], int out_cap, int *out_n)
{
	FILE	*f;
	char	line[1024];
	char	ring[64][1024];
	int		count;
	int		idx;
	int		i;
	int		start;
	int		n;
	int		seen_header;

	if (out_n)
		*out_n = 0;
	if (!path || max_lines <= 0 || !out || out_cap <= 0 || !out_n)
		return (0);
	f = fs_fopen_shared_read(path);
	if (!f)
		return (0);
	if (max_lines > 64)
		max_lines = 64;
	count = 0;
	idx = 0;
	seen_header = 0;
	while (fgets(line, (int)sizeof(line), f))
	{
		if (!seen_header)
		{
			seen_header = 1;
			if (looks_like_any_csv_header(line))
				continue ;
		}
		snprintf(ring[idx], sizeof(ring[idx]), "%s", line);
		idx = (idx + 1) % max_lines;
		if (count < max_lines)
			count++;
	}
	fclose(f);
	start = (count == max_lines) ? idx : 0;
	n = (count < out_cap) ? count : out_cap;
	i = 0;
	while (i < n)
	{
		int src = (start + i) % max_lines;
		csv_format_line(out[i], 256, ring[src]);
		i++;
	}
	*out_n = n;
	return (n > 0);
}

static void	app_refresh_cached(t_app *app)
{
	uint64_t	now;
	long		offset;
	int		need;

	if (!app)
		return ;
	now = ft_time_ms();
	need = (app->last_refresh_ms == 0 || (now - app->last_refresh_ms) > 250);
	if (!need)
		return ;
	app->last_refresh_ms = now;

	/*
	 * Graphe LIVE: maintient un cache serie "chaud" en arriere-plan.
	 * (Le screen Graph LIVE l'utilise ensuite sans re-initialisation.)
	 */
	if (parser_thread_is_running())
		hunt_series_live_tick();

	/* weapon */
	app->weapon_name[0] = '\0';
	weapon_selected_load(tm_path_weapon_selected(), app->weapon_name, sizeof(app->weapon_name));

	/* sweat */
	app->sweat_enabled = 0;
	sweat_option_load(tm_path_options_cfg(), &app->sweat_enabled);

	/* offset + stats (range view if a session is loaded) */
	offset = session_load_offset(tm_path_session_offset());
	app->last_offset = offset;

	/* Live stats cache (no rescans): follows offset or an optional loaded range. */
	tracker_stats_live_tick();
	{
		const t_hunt_stats	*ps;
		long				start_off;
		long				end_raw;
		long				end_res;

		ps = tracker_stats_live_get();
		memset(&app->hunt_stats, 0, sizeof(app->hunt_stats));
		if (ps)
		{
			app->hunt_stats = *ps;
			app->hunt_stats_ok = 1;
		}
		else
			app->hunt_stats_ok = 0;
		app->session_range_active = tracker_stats_live_is_range();
		app->session_range_start = 0;
		app->session_range_end = -1;
		if (app->session_range_active)
		{
			start_off = 0;
			end_raw = -1;
			end_res = -1;
			tracker_stats_live_get_range(&start_off, &end_raw, &end_res);
			app->session_range_start = start_off;
			app->session_range_end = end_res;
		}
	}
	memset(&app->globals_stats, 0, sizeof(app->globals_stats));
	app->globals_stats_ok = (globals_stats_compute(tm_path_globals_csv(), 0, &app->globals_stats) == 0);

	/* feeds */
	file_tail_csv_formatted(tm_path_hunt_csv(), 28, app->hunt_feed_buf, 32, &app->hunt_feed_n);
	for (int i = 0; i < app->hunt_feed_n; i++)
		app->hunt_feed_lines[i] = app->hunt_feed_buf[i];
	file_tail_csv_formatted(tm_path_globals_csv(), 18, app->globals_feed_buf, 24, &app->globals_feed_n);
	for (int i = 0; i < app->globals_feed_n; i++)
		app->globals_feed_lines[i] = app->globals_feed_buf[i];
	file_tail_csv_formatted(tm_path_sessions_stats_csv(), 16, app->sessions_feed_buf, 20, &app->sessions_feed_n);
	for (int i = 0; i < app->sessions_feed_n; i++)
		app->sessions_feed_lines[i] = app->sessions_feed_buf[i];
}

static int	app_chatlog_ok(char *out_path, size_t outsz)
{
	if (out_path && outsz)
		out_path[0] = '\0';
	if (!out_path || outsz == 0)
		return (0);
	if (chatlog_build_path(out_path, outsz) != 0)
		snprintf(out_path, outsz, "%s", "chat.log");
	return (fs_file_exists(out_path));
}

static void	app_export_snapshot(t_window *w, t_app *app)
{
	char	start_ts[64];
	char	end_ts[64];
	char	line[256];
	const char	*msg[3];
	long	offset;
	long	end_off;

	if (!w || !app || !app->hunt_stats_ok)
		return ;
		/* If a session range is active, export that range; else export current offset->EOF */
	if (app->session_range_active)
	{
		offset = app->session_range_start;
		end_off = app->session_range_end;
		if (end_off < 0)
			end_off = session_count_data_lines(tm_path_hunt_csv());
	}
	else
	{
		offset = session_load_offset(tm_path_session_offset());
		end_off = session_count_data_lines(tm_path_hunt_csv());
	}
	start_ts[0] = '\0';
	end_ts[0] = '\0';
	session_extract_range_timestamps_ex(tm_path_hunt_csv(), offset, end_off,
		start_ts, sizeof(start_ts), end_ts, sizeof(end_ts));
	if (!session_export_stats_csv_ex(tm_path_sessions_stats_csv(), &app->hunt_stats,
		start_ts, end_ts, offset, end_off))
	{
		msg[0] = "[ERREUR] Export impossible.";
		msg[1] = tm_path_sessions_stats_csv();
		msg[2] = "";
		ui_screen_message(w, "EXPORT", msg, 3);
		return ;
	}
	snprintf(line, sizeof(line), "OK : session exportee -> %s", tm_path_sessions_stats_csv());
	msg[0] = line;
	msg[1] = start_ts;
	msg[2] = end_ts;
	ui_screen_message(w, "EXPORT", msg, 3);
}

static void	app_action_set_offset_end(t_window *w)
{
	long		end;
	char		line[256];
	const char	*msg[2];

	if (!w)
		return ;
	end = session_count_data_lines(tm_path_hunt_csv());
	/* Leaving any loaded session view */
	(session_clear_range(tm_path_session_range()));
	session_save_offset(tm_path_session_offset(), end);
	/* Force la serie LIVE a se recaler immediatement sur ce nouvel offset. */
	hunt_series_live_force_reset();
	snprintf(line, sizeof(line), "OK : Offset session = %ld ligne(s) (fin CSV)", end);
	msg[0] = line;
	msg[1] = "(Echap pour revenir)";
	ui_screen_message(w, "OFFSET", msg, 2);
}

static void	app_action_stop_export_offset(t_window *w, t_app *app)
{
	t_hunt_stats	s;
	long		start_off;
	long		end_off;
	char		start_ts[64];
	char		end_ts[64];
	char		line[256];
	const char	*msg[4];

	if (!w || !app)
		return ;
	parser_thread_stop();
	/* Stop+Export always targets the current session (offset->EOF), not a loaded range. */
	(session_clear_range(tm_path_session_range()));
	app->last_refresh_ms = 0;
	start_off = session_load_offset(tm_path_session_offset());
	end_off = session_count_data_lines(tm_path_hunt_csv());
	memset(&s, 0, sizeof(s));
	if (tracker_stats_compute_range(tm_path_hunt_csv(), start_off, end_off, &s) != 0)
	{
		msg[0] = "[ERREUR] Pas de stats (CSV manquant ?).";
		msg[1] = tm_path_hunt_csv();
		msg[2] = "";
		ui_screen_message(w, "STOP + EXPORT", msg, 3);
		return ;
	}
	start_ts[0] = '\0';
	end_ts[0] = '\0';
	session_extract_range_timestamps_ex(tm_path_hunt_csv(), start_off, end_off,
		start_ts, sizeof(start_ts), end_ts, sizeof(end_ts));
	if (!session_export_stats_csv_ex(tm_path_sessions_stats_csv(), &s,
		start_ts, end_ts, start_off, end_off))
	{
		msg[0] = "[ERREUR] Export impossible.";
		msg[1] = tm_path_sessions_stats_csv();
		msg[2] = "";
		ui_screen_message(w, "STOP + EXPORT", msg, 3);
		return ;
	}
	session_save_offset(tm_path_session_offset(), end_off);
	/* Force la serie LIVE a re-demarrer sur le nouvel offset. */
	hunt_series_live_force_reset();
	/* Fin de session => obliger la saisie du mob au prochain Start */
	mob_selected_clear(tm_path_mob_selected());
	app->session_start_ms = 0;
	snprintf(line, sizeof(line), "OK : export + offset=%ld (fin CSV)", end_off);
	msg[0] = line;
	msg[1] = tm_path_sessions_stats_csv();
	msg[2] = start_ts;
	msg[3] = end_ts;
	ui_screen_message(w, "STOP + EXPORT", msg, 4);
}

static void	app_weapon_picker_close(t_app *app)
{
	if (!app)
		return ;
	if (app->weapon_db_loaded)
	{
		armes_db_free(&app->weapon_db);
		memset(&app->weapon_db, 0, sizeof(app->weapon_db));
		app->weapon_db_loaded = 0;
	}
	app->weapon_picker_open = 0;
}

static void	app_weapon_picker_open(t_app *app)
{
	if (!app)
		return ;
	if (app->weapon_picker_open)
		return ;
	memset(&app->weapon_db, 0, sizeof(app->weapon_db));
	if (!armes_db_load(&app->weapon_db, tm_path_armes_ini()))
	{
		app->weapon_db_loaded = 0;
		app->weapon_picker_open = 0;
		return ;
	}
	app->weapon_db_loaded = 1;
	app->weapon_picker_selected = 0;
	app->weapon_picker_scroll = 0;
	app->weapon_picker_open = 1;
}

static void	app_draw_weapon_picker(t_window *w, t_ui_state *ui, t_app *app, t_rect content)
{
	int		count;
	const char	*items[256];
	int		clicked;
	t_rect		over;
	t_rect		panel;
	t_rect		list_r;
	t_rect		btn_r;
	int		item_h;
	int		i;

	if (!w || !ui || !app || !app->weapon_picker_open || !app->weapon_db_loaded)
		return ;
	count = (int)app->weapon_db.count;
	if (count <= 0)
		return ;
	if (count > 256)
		count = 256;
	i = 0;
	while (i < count)
	{
		items[i] = app->weapon_db.items[i].name;
		i++;
	}
	item_h = 38;
	/* overlay dims */
	over = (t_rect){content.x + content.w / 8, content.y + content.h / 8,
				content.w * 3 / 4, content.h * 3 / 4};
	window_fill_rect(w, content.x, content.y, content.w, content.h,
		ui_color_lerp(ui->theme->bg, 0x000000, 60));
	panel = over;
	ui_draw_panel(w, panel, ui->theme->surface, ui->theme->border);
	ui_draw_text(w, panel.x + 12, panel.y + 12, "Choisir une arme", ui->theme->text);
	ui_draw_text(w, panel.x + 12, panel.y + 32, "Clique pour selectionner (Esc pour fermer)", ui->theme->text2);
	btn_r = (t_rect){panel.x + panel.w - 110, panel.y + 10, 96, 28};
	if (ui_button(w, ui, btn_r, "Fermer", UI_BTN_SECONDARY, 1))
		app_weapon_picker_close(app);
	list_r = (t_rect){panel.x + 8, panel.y + 60, panel.w - 16, panel.h - 70};
	clicked = ui_list_scroll(w, ui, list_r, items, count,
		&app->weapon_picker_selected, item_h, 0, &app->weapon_picker_scroll);
	if (w->key_escape)
		app_weapon_picker_close(app);
	if (clicked >= 0)
	{
		weapon_selected_save(tm_path_weapon_selected(), items[clicked]);
		snprintf(app->weapon_name, sizeof(app->weapon_name), "%s", items[clicked]);
		app_weapon_picker_close(app);
	}
}


/* -------------------------------------------------------------------------- */
/* Session picker (load an exported session range)                             */
/* -------------------------------------------------------------------------- */

static void	app_session_picker_close(t_app *app)
{
	if (!app)
		return ;
	if (app->session_list_loaded)
	{
		sessions_list_free(&app->session_list);
		memset(&app->session_list, 0, sizeof(app->session_list));
		app->session_list_loaded = 0;
	}
	app->session_picker_open = 0;
	app->session_picker_selected = 0;
	app->session_picker_scroll = 0;
}

static void	app_session_picker_open(t_window *w, t_app *app)
{
	t_sessions_list	list;

	if (!app)
		return ;
	if (app->session_picker_open)
		return ;
	memset(&list, 0, sizeof(list));
	if (sessions_list_load(tm_path_sessions_stats_csv(), 256, &list) != 0 || list.count == 0)
	{
		const char *msg[3] = {
			"[ERREUR] Aucune session a charger.",
			tm_path_sessions_stats_csv(),
			"Fais un Stop+Export ou Export snapshot."
		};
		if (w)
			ui_screen_message(w, "CHARGER SESSION", msg, 3);
		sessions_list_free(&list);
		return ;
	}
	app->session_list = list;
	app->session_list_loaded = 1;
	app->session_picker_selected = 0; /* newest first in UI */
	app->session_picker_scroll = 0;
	app->session_picker_open = 1;
}

static const t_session_entry	*app_session_picker_entry(t_app *app, int ui_index)
{
	size_t	count;
	size_t	idx;

	if (!app || !app->session_list_loaded || !app->session_list.items)
		return (NULL);
	count = app->session_list.count;
	if (count == 0)
		return (NULL);
	if (ui_index < 0)
		ui_index = 0;
	if ((size_t)ui_index >= count)
		ui_index = (int)(count - 1);
	/* UI order: newest first -> data is oldest->newest */
	idx = (count - 1) - (size_t)ui_index;
	return (&app->session_list.items[idx]);
}

static void	app_apply_session_range(t_window *w, t_app *app, const t_session_entry *e)
{
	char		line1[256];
	char		line2[256];
	const char	*msg[3];

	if (!w || !app || !e)
		return ;
	if (!e->has_offsets)
	{
		msg[0] = "[WARNING] Cette session n'a pas d'offsets (export ancien).";
		msg[1] = "Refais un Export snapshot / Stop+Export pour generer start_offset/end_offset.";
		msg[2] = "";
		ui_screen_message(w, "CHARGER SESSION", msg, 3);
		return ;
	}
	if (session_save_range(tm_path_session_range(), e->start_offset, e->end_offset) != 0)
	{
		msg[0] = "[ERREUR] Impossible d'ecrire hunt_session.range";
		msg[1] = tm_path_session_range();
		msg[2] = "";
		ui_screen_message(w, "CHARGER SESSION", msg, 3);
		return ;
	}
	snprintf(line1, sizeof(line1), "OK : Session chargee (range %ld..%ld)", e->start_offset, e->end_offset);
	snprintf(line2, sizeof(line2), "%-.19s -> %-.19s  |  %s", e->start_ts, e->end_ts, e->weapon);
	msg[0] = line1;
	msg[1] = line2;
	msg[2] = "(Bouton 'Retour courant' pour revenir a l'offset)";
	ui_screen_message(w, "CHARGER SESSION", msg, 3);

	/* Force refresh cache immediately */
	app->last_refresh_ms = 0;
}

static void	app_draw_session_picker(t_window *w, t_ui_state *ui, t_app *app, t_rect content)
{
	size_t		count;
	char		labels[256][256];
	const char	*items[256];
	int			clicked;
	t_rect		over;
	t_rect		panel;
	t_rect		left;
	t_rect		right;
	t_rect		btn_close;
	t_rect		btn_load;
	t_rect		btn_cancel;
	int			item_h;
	size_t		i;
	const t_session_entry *e;

	if (!w || !ui || !app || !app->session_picker_open || !app->session_list_loaded)
		return ;
	if (!app->session_list.items || app->session_list.count == 0)
	{
		app_session_picker_close(app);
		return ;
	}
	count = app->session_list.count;
	if (count > 256)
		count = 256;

	/* Build UI labels newest->oldest */
	i = 0;
	while (i < count)
	{
		const t_session_entry *se = &app->session_list.items[(app->session_list.count - 1) - i];
		sessions_format_label(se, labels[i], sizeof(labels[i]));
		items[i] = labels[i];
		i++;
	}

	/* Keyboard navigation */
	if (w->key_up)
		app->session_picker_selected--;
	if (w->key_down)
		app->session_picker_selected++;
	if (app->session_picker_selected < 0)
		app->session_picker_selected = 0;
	if ((size_t)app->session_picker_selected >= count)
		app->session_picker_selected = (int)(count - 1);

	item_h = 36;
	over = (t_rect){content.x + content.w / 10, content.y + content.h / 10,
		content.w * 8 / 10, content.h * 8 / 10};
	window_fill_rect(w, content.x, content.y, content.w, content.h,
		ui_color_lerp(ui->theme->bg, 0x000000, 60));
	panel = over;
	ui_draw_panel(w, panel, ui->theme->surface, ui->theme->border);
	ui_draw_text(w, panel.x + 12, panel.y + 12, "Charger une session", ui->theme->text);
	ui_draw_text(w, panel.x + 12, panel.y + 32,
		"Selectionne une session exportee (Esc pour fermer)", ui->theme->text2);

	btn_close = (t_rect){panel.x + panel.w - 110, panel.y + 10, 96, 28};
	if (ui_button(w, ui, btn_close, "Fermer", UI_BTN_SECONDARY, 1))
		app_session_picker_close(app);

	/* Split layout */
	left = (t_rect){panel.x + 8, panel.y + 60, (panel.w * 60 / 100) - 12, panel.h - 110};
	right = (t_rect){left.x + left.w + 8, left.y, panel.x + panel.w - (left.x + left.w + 16), left.h};

	/* List */
	clicked = ui_list_scroll(w, ui, left, items, (int)count,
		&app->session_picker_selected, item_h, 0, &app->session_picker_scroll);
	if (clicked >= 0)
		app->session_picker_selected = clicked;

	/* Details */
	e = app_session_picker_entry(app, app->session_picker_selected);
	ui_draw_panel(w, right, ui->theme->bg, ui->theme->border);
	if (e)
	{
		char line[256];
		int  yy = right.y + 10;
		ui_draw_text(w, right.x + 10, yy, "Details", ui->theme->text);
		yy += 22;
		snprintf(line, sizeof(line), "Start: %-.24s", e->start_ts);
		ui_draw_text(w, right.x + 10, yy, line, ui->theme->text2); yy += 18;
		snprintf(line, sizeof(line), "End:   %-.24s", e->end_ts);
		ui_draw_text(w, right.x + 10, yy, line, ui->theme->text2); yy += 18;
		snprintf(line, sizeof(line), "Weapon: %s", e->weapon);
		ui_draw_text(w, right.x + 10, yy, line, ui->theme->text2); yy += 18;
		snprintf(line, sizeof(line), "Kills/Shots: %ld / %ld", e->kills, e->shots);
		ui_draw_text(w, right.x + 10, yy, line, ui->theme->text2); yy += 18;
		snprintf(line, sizeof(line), "Loot: %.4f  Expense: %.4f", e->loot_ped, e->expense_ped);
		ui_draw_text(w, right.x + 10, yy, line, ui->theme->text2); yy += 18;
		snprintf(line, sizeof(line), "Net: %.4f  Return: %.2f%%", e->net_ped, e->return_pct);
		ui_draw_text(w, right.x + 10, yy, line, ui->theme->text2); yy += 18;
		if (e->has_offsets)
			snprintf(line, sizeof(line), "Offsets: %ld .. %ld", e->start_offset, e->end_offset);
		else
			snprintf(line, sizeof(line), "Offsets: (absents - export ancien)");
		ui_draw_text(w, right.x + 10, yy, line, e->has_offsets ? ui->theme->success : ui->theme->warn);
	}

	/* Bottom buttons */
	btn_cancel = (t_rect){panel.x + panel.w - 220, panel.y + panel.h - 40, 96, 30};
	btn_load = (t_rect){panel.x + panel.w - 112, panel.y + panel.h - 40, 96, 30};
	if (ui_button(w, ui, btn_cancel, "Annuler", UI_BTN_SECONDARY, 1))
		app_session_picker_close(app);

	{
		int enabled = (e && e->has_offsets);
		if (ui_button(w, ui, btn_load, "Charger", UI_BTN_PRIMARY, enabled))
		{
			app_apply_session_range(w, app, e);
			app_session_picker_close(app);
		}
	}

	if (w->key_escape)
		app_session_picker_close(app);
	if (w->key_enter && e && e->has_offsets)
	{
		app_apply_session_range(w, app, e);
		app_session_picker_close(app);
	}
}

static void	app_sidebar_nav(t_window *w, t_ui_state *ui, t_ui_layout *ly, t_app *app)
{
	/*
	 * Main navigation order (requested):
	 *  - Chasse
	 *  - Globals
	 *  - Graph LIVE (opens the dedicated full-screen graph UI)
	 *  - Sessions / Exports
	 *  - Configuration
	 */
	const char	*top_items[] = {"Chasse", "Globals", "Graph LIVE", "Sessions / Exports", "Configuration"};
	const char	*bot_items[] = {"Health", "Maintenance", "Aide", "Quitter"};
	const int	top_count = 5;
	const int	bot_count = 4;
	int			total;
	int			sel_top;
	int			sel_bot;
	t_rect		r;
	t_rect		r_top;
	t_rect		r_bot;
	int			clicked;
	int			activated;
	int			act_idx;
	int			item_h;
	int			header_h;
	int			bot_area_h;

	if (!w || !ui || !ly || !app)
		return ;
	total = top_count + bot_count;
	activated = 0;
	act_idx = -1;
	item_h = 40;
	header_h = 28;
	bot_area_h = header_h + bot_count * item_h + UI_PAD;

	/* Keyboard navigation */
	if (w->key_up)
		app->nav_cursor--;
	if (w->key_down)
		app->nav_cursor++;
	if (app->nav_cursor < 0)
		app->nav_cursor = 0;
	if (app->nav_cursor >= total)
		app->nav_cursor = total - 1;

	/* Top section */
	r = (t_rect){ly->sidebar.x + UI_PAD, ly->sidebar.y + UI_PAD, ly->sidebar.w - UI_PAD * 2, header_h};
	ui_section_header(w, ui, r, "Navigation");
	r_top = (t_rect){ly->sidebar.x, r.y + r.h, ly->sidebar.w,
		ly->sidebar.h - (r.h + UI_PAD) - bot_area_h};
	if (r_top.h < 0)
		r_top.h = 0;
	sel_top = (app->nav_cursor < top_count) ? app->nav_cursor : -1;
	/*
	 * Scroll in the main sidebar like in Graph LIVE.
	 * Only enable the scroll-list (with scrollbar area) when needed to avoid
	 * wasting horizontal space when everything fits.
	 */
	if (top_count * item_h > r_top.h)
		clicked = ui_list_scroll(w, ui, r_top, top_items, top_count, &sel_top,
			item_h, 0, &app->nav_scroll);
	else
	{
		app->nav_scroll = 0;
		clicked = ui_list(w, ui, r_top, top_items, top_count, &sel_top, item_h, 0);
	}
	if (in_rect_local(w->mouse_x, w->mouse_y, r_top))
		w->mouse_wheel = 0;
	if (clicked >= 0)
	{
		app->nav_cursor = clicked;
		activated = 1;
		act_idx = clicked;
	}

	/* Bottom section */
	r_bot = (t_rect){ly->sidebar.x, ly->sidebar.y + ly->sidebar.h - bot_area_h,
		ly->sidebar.w, bot_area_h - UI_PAD};
	r = (t_rect){ly->sidebar.x + UI_PAD, r_bot.y, ly->sidebar.w - UI_PAD * 2, header_h};
	ui_section_header(w, ui, r, "System");
	r_bot.y += header_h;
	r_bot.h -= header_h;
	sel_bot = (app->nav_cursor >= top_count) ? (app->nav_cursor - top_count) : -1;
	clicked = ui_list(w, ui, r_bot, bot_items, bot_count, &sel_bot, item_h, 0);
	if (clicked >= 0)
	{
		app->nav_cursor = top_count + clicked;
		activated = 1;
		act_idx = app->nav_cursor;
	}

	/* Activate navigation on click or Enter */
	if (w->key_enter)
	{
		activated = 1;
		act_idx = app->nav_cursor;
	}
	if (activated)
	{
		int idx = act_idx;
		if (idx >= 0 && idx < top_count)
		{
			/* Explicit mapping (do not rely on enum order). */
			if (idx == 0)
				app->page = PAGE_CHASSE;
			else if (idx == 1)
				app->page = PAGE_GLOBALS;
			else if (idx == 2)
			{
				/*
				 * Graph LIVE is a dedicated full-screen screen.
				 * UX request: leaving it (Retour/Echap) must always return to Chasse.
				 */
				app->page = PAGE_CHASSE;
				app->nav_cursor = 0; /* keep sidebar selection on Chasse */
				screen_graph_live(w);
			}
			else if (idx == 3)
				app->page = PAGE_SESSIONS;
			else if (idx == 4)
				app->page = PAGE_CONFIG;
		}
		else if (idx == top_count + 0)
			app->page = PAGE_HEALTH;
		else if (idx == top_count + 1)
			app->page = PAGE_MAINTENANCE;
		else if (idx == top_count + 2)
			app->page = PAGE_AIDE;
		else if (idx == top_count + 3)
			w->running = 0;
	}
}

static void	app_topbar(t_window *w, t_ui_state *ui, t_ui_layout *ly, t_app *app)
{
	char	crumb[128];
	char	chatpath[1024];
	int		chat_ok;
	char	center[256];
	char	timebuf[32];
	uint64_t	elapsed;
	long	offset;
	int		x;
	int		btn_y;
	int		text_y;
	int		row1_h;
	int		row2_h;
	int		btn_h;
	int		pad;
	int		gap;
	char	weapon_btn[96];
	int		w_running;
	int		h_running;

	if (!w || !ui || !ly || !app)
		return ;
	pad = UI_PAD;
	gap = 8;
	btn_h = 30;
	row1_h = btn_h + 12;
	if (row1_h < 40)
		row1_h = 40;
	if (row1_h > ly->topbar.h - 28)
		row1_h = ly->topbar.h - 28;
	row2_h = ly->topbar.h - row1_h;
	btn_y = ly->topbar.y + (row1_h - btn_h) / 2;
	text_y = ly->topbar.y + row1_h + (row2_h / 2 - 6);
	/* Breadcrumb */
	if (app->page == PAGE_CHASSE)
		snprintf(crumb, sizeof(crumb), "Chasse / %s", app->hunt_mode_live ? "Live" : "Replay");
	else if (app->page == PAGE_GLOBALS)
		snprintf(crumb, sizeof(crumb), "Globals / %s", app->globals_mode_live ? "Live" : "Replay");
	else if (app->page == PAGE_SESSIONS)
		snprintf(crumb, sizeof(crumb), "Sessions / Exports");
	else if (app->page == PAGE_CONFIG)
		snprintf(crumb, sizeof(crumb), "Configuration");
	else if (app->page == PAGE_MAINTENANCE)
		snprintf(crumb, sizeof(crumb), "Maintenance");
	else if (app->page == PAGE_AIDE)
		snprintf(crumb, sizeof(crumb), "Aide");
	else if (app->page == PAGE_HEALTH)
		snprintf(crumb, sizeof(crumb), "Health");
	else
		snprintf(crumb, sizeof(crumb), "Graph LIVE");
	ui_draw_text(w, ly->topbar.x + pad, text_y, crumb, ui->theme->text);

	/* chat.log status */
	chat_ok = app_chatlog_ok(chatpath, sizeof(chatpath));
	ui_draw_text(w, ly->topbar.x + pad + ui_measure_text_w(crumb, 14) + 16,
		text_y, chat_ok ? "chat.log OK" : "chat.log introuvable", chat_ok ? ui->theme->success : ui->theme->warn);

	/* Center status */
	h_running = parser_thread_is_running();
	w_running = globals_thread_is_running();
	offset = session_load_offset(tm_path_session_offset());
	elapsed = 0;
	if (app->session_start_ms && h_running)
		elapsed = ft_time_ms() - app->session_start_ms;
	fmt_hms(timebuf, sizeof(timebuf), elapsed);
	if (app->session_range_active)
		snprintf(center, sizeof(center), "Chasse:%s  Globals:%s  VIEW Range:%ld..%ld",
			h_running ? "RUN" : "STOP", w_running ? "RUN" : "STOP",
			app->session_range_start, app->session_range_end);
	else
		snprintf(center, sizeof(center), "Chasse:%s  Globals:%s  Session:%s  Offset:%ld",
			h_running ? "RUN" : "STOP", w_running ? "RUN" : "STOP", timebuf, offset);	ui_draw_text(w, ly->topbar.x + (ly->topbar.w / 2) - (ui_measure_text_w(center, 14) / 2),
		text_y, center, ui->theme->text2);

	/* Right quick actions (contextuel) */
	x = ly->topbar.x + ly->topbar.w - pad;
	/* Weapon */
	if (app->weapon_name[0])
		snprintf(weapon_btn, sizeof(weapon_btn), "Arme: %.24s", app->weapon_name);
	else
		snprintf(weapon_btn, sizeof(weapon_btn), "Arme: (aucune)");
	{
		int bw = ui_measure_text_w(weapon_btn, 14) + 24;
		if (bw > 220)
			bw = 220;
		x -= bw;
		if (ui_button(w, ui, (t_rect){x, btn_y, bw, btn_h}, weapon_btn, UI_BTN_SECONDARY, 1))
			app_weapon_picker_open(app);
		x -= gap;
	}
	/* Overlay */
	{
		const char *lbl = overlay_is_enabled() ? "Overlay ON" : "Overlay OFF";
		int bw = ui_measure_text_w(lbl, 14) + 24;
		x -= bw;
		if (ui_button(w, ui, (t_rect){x, btn_y, bw, btn_h}, lbl,
            overlay_is_enabled() ? UI_BTN_PRIMARY : UI_BTN_SECONDARY, 1))
        {
            overlay_toggle();
            if (overlay_is_enabled() && app->session_start_ms == 0)
                app->session_start_ms = ft_time_ms();
        }
		x -= gap;
	}
	/* Export */
	{
		const char *lbl = "Export";
		int bw = ui_measure_text_w(lbl, 14) + 24;
		x -= bw;
		if (ui_button(w, ui, (t_rect){x, btn_y, bw, btn_h}, lbl, UI_BTN_SECONDARY, app->hunt_stats_ok))
			app_export_snapshot(w, app);
		x -= gap;
	}
	/* Start/Stop (contextuel) */
	{
		int enabled = (app->page == PAGE_CHASSE || app->page == PAGE_GLOBALS);
		const char *lbl;
		int running;
		running = (app->page == PAGE_GLOBALS) ? w_running : h_running;
		lbl = running ? "Stop" : "Start";
		int bw = ui_measure_text_w(lbl, 14) + 24;
		x -= bw;
		if (ui_button(w, ui, (t_rect){x, btn_y, bw, btn_h}, lbl,
			running ? UI_BTN_SECONDARY : UI_BTN_PRIMARY, enabled))
		{
			if (app->page == PAGE_CHASSE)
			{
				if (running)
				{
					parser_thread_stop();
					app->session_start_ms = 0;
				}
				else
				{
					if (mob_prompt_ensure(w, NULL, 0))
					{
						int rc = parser_thread_start_live();
						app->hunt_mode_live = 1;
						if (rc != 0 || !parser_thread_is_running())
						{
							const char *msg[] = {"[ERREUR] Le parser LIVE n'a pas pu demarrer.",
								"Regarde: logs/parser_debug.log (paths + errno)",
								""};
							ui_screen_message(w, "PARSER CHASSE", msg, 3);
						}
						else if (app->session_start_ms == 0)
							app->session_start_ms = ft_time_ms();
					}
				}
			}
			else if (app->page == PAGE_GLOBALS)
			{
				if (running)
					globals_thread_stop();
				else
				{
					globals_thread_start_live();
					app->globals_mode_live = 1;
				}
			}
		}
	}
}

static void	app_page_header(t_window *w, t_ui_state *ui, t_rect content,
						const char *title, const char *subtitle,
						t_rect *out_body)
{
	t_rect	h;

	if (!w || !ui)
		return ;
	h = (t_rect){content.x, content.y, content.w, 84};
	ui_draw_panel(w, h, ui->theme->bg, ui->theme->border);
	ui_draw_text(w, h.x + UI_PAD, h.y + 18, title, ui->theme->text);
	if (subtitle)
		ui_draw_text(w, h.x + UI_PAD, h.y + 40, subtitle, ui->theme->text2);
	if (out_body)
		*out_body = (t_rect){content.x, content.y + h.h, content.w, content.h - h.h};
}

static void	app_page_chasse(t_window *w, t_ui_state *ui, t_app *app, t_rect content)
{
	t_rect	body;
	t_rect	left;
	t_rect	right;
	int		gap;
	int		right_w;
	int		card_h;
	int		cols;
	int		cw;
	int		x;
	int		y;
	char	buf[64];

	app_page_header(w, ui, content, "Chasse", "Pilotage session + stats + activity feed", &body);
	gap = UI_PAD;
	right_w = (int)(body.w * 0.30);
	if (right_w < 260)
		right_w = 260;
	if (right_w > body.w - 260)
		right_w = body.w - 260;
	left = (t_rect){body.x + UI_PAD, body.y + UI_PAD,
		body.w - right_w - (UI_PAD * 3), body.h - UI_PAD * 2};
	right = (t_rect){left.x + left.w + gap, left.y, right_w, left.h};

	/* Toolbar contextuelle */
	{
		t_rect	tr = (t_rect){body.x + UI_PAD, content.y + 56, body.w - UI_PAD * 2, 28};
		int bx = tr.x;
		int by = tr.y;
		int bh = 28;
		int bw;
		const char *lbl;
		/* Start LIVE */
		lbl = "Start LIVE";
		bw = ui_measure_text_w(lbl, 14) + 24;
		if (ui_button(w, ui, (t_rect){bx, by, bw, bh}, lbl, UI_BTN_PRIMARY, !parser_thread_is_running()))
		{
			if (mob_prompt_ensure(w, NULL, 0))
			{
				int rc = parser_thread_start_live();
				app->hunt_mode_live = 1;
				if (rc != 0 || !parser_thread_is_running())
				{
					const char *msg[] = {"[ERREUR] Le parser LIVE n'a pas pu demarrer.",
						"Regarde: logs/parser_debug.log (paths + errno)", ""};
					ui_screen_message(w, "PARSER CHASSE", msg, 3);
				}
				else if (app->session_start_ms == 0)
					app->session_start_ms = ft_time_ms();
			}
		}
		bx += bw + 8;
		/* Start REPLAY */
		lbl = "Start REPLAY";
		bw = ui_measure_text_w(lbl, 14) + 24;
		if (ui_button(w, ui, (t_rect){bx, by, bw, bh}, lbl, UI_BTN_SECONDARY, !parser_thread_is_running()))
		{
			if (mob_prompt_ensure(w, NULL, 0))
			{
				int rc = parser_thread_start_replay();
				app->hunt_mode_live = 0;
				if (rc != 0 || !parser_thread_is_running())
				{
					const char *msg[] = {"[ERREUR] Le parser REPLAY n'a pas pu demarrer.",
						"Regarde: logs/parser_debug.log (paths + errno)", ""};
					ui_screen_message(w, "PARSER CHASSE", msg, 3);
				}
				else if (app->session_start_ms == 0)
					app->session_start_ms = ft_time_ms();
			}
		}
		bx += bw + 8;
		/* Stop + Export */
		lbl = "Stop+Export";
		bw = ui_measure_text_w(lbl, 14) + 24;
		if (ui_button(w, ui, (t_rect){bx, by, bw, bh}, lbl, UI_BTN_SECONDARY, 1))
			app_action_stop_export_offset(w, app);
		bx += bw + 8;
		/* Sweat */
		lbl = app->sweat_enabled ? "Sweat ON" : "Sweat OFF";
		bw = ui_measure_text_w(lbl, 14) + 24;
		if (ui_button(w, ui, (t_rect){bx, by, bw, bh}, lbl,
			app->sweat_enabled ? UI_BTN_PRIMARY : UI_BTN_SECONDARY, 1))
		{
			app->sweat_enabled = !app->sweat_enabled;
			sweat_option_save(tm_path_options_cfg(), app->sweat_enabled);
		}
		bx += bw + 8;
		/* Offset -> fin CSV */
		lbl = "Offset fin CSV";
		bw = ui_measure_text_w(lbl, 14) + 24;
		if (ui_button(w, ui, (t_rect){bx, by, bw, bh}, lbl, UI_BTN_GHOST, 1))
			app_action_set_offset_end(w);
	}

	/* Left column: infos (scrollable + clipped) */
	ui_draw_panel(w, left, ui->theme->bg, ui->theme->border);
	{
		t_rect	clip;
		int		scroll;
		int		y_logic;
		int		y_draw;
		int		content_h;
		char	line[256];
		size_t	i;

		clip = (t_rect){left.x + 1, left.y + 1, left.w - 2, left.h - 2};
		scroll = app->hunt_info_scroll;
		cols = 3;
		card_h = 70;
		cw = (left.w - (UI_PAD * (cols - 1))) / cols;
		/* Cards: Loot/Depense/Net + Return/Kills/Shots */
		y_logic = left.y;
		y_draw = y_logic - scroll;
		x = left.x;
		y = y_draw;
		if (app->hunt_stats_ok)
		{
				tm_money_format_ped4(buf, sizeof(buf), app->hunt_stats.loot_ped);
			ui_card_clipped(w, ui, (t_rect){x, y, cw, card_h}, "Loot", buf, ui->theme->success, clip);
			x += cw + UI_PAD;
				tm_money_format_ped4(buf, sizeof(buf), app->hunt_stats.expense_used);
			ui_card_clipped(w, ui, (t_rect){x, y, cw, card_h}, "Depense", buf, ui->theme->warn, clip);
			x += cw + UI_PAD;
				tm_money_format_ped4(buf, sizeof(buf), app->hunt_stats.net_ped);
			ui_card_clipped(w, ui, (t_rect){x, y, cw, card_h}, "Net", buf,
					app->hunt_stats.net_ped >= 0 ? ui->theme->success : ui->theme->warn, clip);

			y_logic += card_h + UI_PAD;
			y_draw = y_logic - scroll;
			x = left.x;
			y = y_draw;
				snprintf(buf, sizeof(buf), "%.2f%%", (app->hunt_stats.expense_used > 0)
					? ((double)app->hunt_stats.loot_ped / (double)app->hunt_stats.expense_used) * 100.0 : 0.0);
			ui_card_clipped(w, ui, (t_rect){x, y, cw, card_h}, "Return", buf, ui->theme->accent, clip);
			x += cw + UI_PAD;
			snprintf(buf, sizeof(buf), "%ld", (long)app->hunt_stats.kills);
			ui_card_clipped(w, ui, (t_rect){x, y, cw, card_h}, "Kills", buf, ui->theme->text, clip);
			x += cw + UI_PAD;
			snprintf(buf, sizeof(buf), "%ld", (long)app->hunt_stats.shots);
			ui_card_clipped(w, ui, (t_rect){x, y, cw, card_h}, "Shots", buf, ui->theme->text, clip);

			y_logic += card_h + 10;

			/* Details (TT/MU) as cards (same style as infos) */
			ui_section_header_clipped(w, ui,
				(t_rect){left.x, (y_logic - 10) - scroll, left.w, 28},
				"Details (TT / MU)", clip);

			#ifdef TM_STATS_HAS_MARKUP
			{
				int		dh;
				int		dcw;
				int		dx;
				int		dy;

				dh = 70;
				dcw = (left.w - (UI_PAD * (cols - 1))) / cols;
				dx = left.x;
				dy = y_logic - scroll;
					tm_money_format_ped4(buf, sizeof(buf), app->hunt_stats.loot_tt_ped);
				ui_card_clipped(w, ui, (t_rect){dx, dy, dcw, dh}, "Loot TT", buf, ui->theme->success, clip);
				dx += dcw + UI_PAD;
					tm_money_format_ped4(buf, sizeof(buf), app->hunt_stats.loot_mu_ped);
				ui_card_clipped(w, ui, (t_rect){dx, dy, dcw, dh}, "Loot MU", buf, ui->theme->success, clip);
				dx += dcw + UI_PAD;
					tm_money_format_ped4(buf, sizeof(buf), app->hunt_stats.loot_total_mu_ped);
				ui_card_clipped(w, ui, (t_rect){dx, dy, dcw, dh}, "Loot TT+MU", buf, ui->theme->success, clip);

				dy += dh + UI_PAD;
				dx = left.x;
					snprintf(buf, sizeof(buf), "%.2f%%", (app->hunt_stats.expense_used > 0)
						? ((double)app->hunt_stats.loot_total_mu_ped / (double)app->hunt_stats.expense_used) * 100.0 : 0.0);
				ui_card_clipped(w, ui, (t_rect){dx, dy, dcw, dh}, "Return TT+MU", buf, ui->theme->accent, clip);
				dx += dcw + UI_PAD;
					tm_money_format_ped4(buf, sizeof(buf), app->hunt_stats.loot_total_mu_ped - app->hunt_stats.expense_used);
				ui_card_clipped(w, ui, (t_rect){dx, dy, dcw, dh}, "Net TT+MU", buf,
					(app->hunt_stats.loot_total_mu_ped - app->hunt_stats.expense_used) >= 0 ? ui->theme->success : ui->theme->warn, clip);

				y_logic += (dh * 2) + UI_PAD + 10;
			}
			#endif

			/* Other infos */
			snprintf(line, sizeof(line), "Loot events: %ld   Sweat: %ld (x%ld)",
				(long)app->hunt_stats.loot_events, (long)app->hunt_stats.sweat_events, (long)app->hunt_stats.sweat_total);
			ui_draw_text_clipped(w, left.x + UI_PAD, y_logic - scroll, line, ui->theme->text2, clip);
			y_logic += 18;
				{
					char exp_csv[32];
					char exp_model[32];
					tm_money_format_ped4(exp_csv, sizeof(exp_csv), app->hunt_stats.expense_ped_logged);
					tm_money_format_ped4(exp_model, sizeof(exp_model), app->hunt_stats.expense_ped_calc);
					snprintf(line, sizeof(line), "Depenses CSV: %s | modele: %s | source: %s",
						exp_csv, exp_model,
						app->hunt_stats.expense_used_is_logged ? "CSV" : "modele arme");
				}
			ui_draw_text_clipped(w, left.x + UI_PAD, y_logic - scroll, line, ui->theme->text2, clip);
			y_logic += 18;
			{
				char ped[32];
				tm_money_format_ped4(ped, sizeof(ped), app->hunt_stats.cost_shot_uPED);
				snprintf(line, sizeof(line), "Cout/shot: %s PED", ped);
			}
			ui_draw_text_clipped(w, left.x + UI_PAD, y_logic - scroll, line, ui->theme->text2, clip);
			y_logic += 22;

			if (app->hunt_stats.top_loot_count > 0)
			{
				ui_section_header_clipped(w, ui,
					(t_rect){left.x, y_logic - scroll, left.w, 28},
					"Top loot (TT+MU)", clip);
				y_logic += 38;
				ui_draw_text_clipped(w, left.x + UI_PAD, y_logic - scroll,
					"Item                TT      MU     Total   Evts", ui->theme->text2, clip);
				y_logic += 18;
				i = 0;
				while (i < app->hunt_stats.top_loot_count && i < 6)
				{
					const t_top_loot *tl = &app->hunt_stats.top_loot[i];
						{
							char tt[32];
							char mu[32];
							char mu_fmt[32];
							char tot[32];

							tm_money_format_ped2(tt, sizeof(tt), tl->tt_ped);
							tm_money_format_ped2(mu, sizeof(mu), tl->mu_ped);
							tm_money_format_ped2(tot, sizeof(tot), tl->total_mu_ped);
							snprintf(mu_fmt, sizeof(mu_fmt), "%s%s", (tl->mu_ped >= 0) ? "+" : "", mu);
							snprintf(line, sizeof(line), "%-18.18s %7s %7s %7s %5ld",
								tl->name, tt, mu_fmt, tot, (long)tl->events);
						}
					ui_draw_text_clipped(w, left.x + UI_PAD, y_logic - scroll, line, ui->theme->text2, clip);
					y_logic += 18;
					i++;
				}
			}
		}
		else
		{
			ui_draw_text_clipped(w, left.x + UI_PAD, left.y + UI_PAD - scroll,
				"Aucune stats (CSV manquant ?).", ui->theme->text2, clip);
			ui_draw_text_clipped(w, left.x + UI_PAD, left.y + UI_PAD + 18 - scroll,
				tm_path_hunt_csv(), ui->theme->text2, clip);
			y_logic = left.y + 120;
		}

		content_h = (y_logic - left.y) + UI_PAD;
		if (content_h < left.h)
			content_h = left.h;
		if (in_rect_local(w->mouse_x, w->mouse_y, left))
			ui_scroll_update(w, &app->hunt_info_scroll, content_h, left.h, 24);
	}

	/* Right column: activity feed */
	ui_draw_panel(w, right, ui->theme->surface, ui->theme->border);
	ui_draw_text(w, right.x + 12, right.y + 10, "Activity feed", ui->theme->text);
	ui_draw_text(w, right.x + 12, right.y + 28, "Dernieres lignes CSV", ui->theme->text2);
	if (app->hunt_feed_n > 0)
	{
		t_rect lr = (t_rect){right.x + 8, right.y + 52, right.w - 16, right.h - 60};
		ui_text_lines_scroll(w, ui, lr, app->hunt_feed_lines, app->hunt_feed_n, 14,
			ui->theme->text2, &app->hunt_feed_scroll);
	}
	else
		ui_draw_text(w, right.x + 12, right.y + 56, "(vide)", ui->theme->text2);
}

static void	app_page_globals(t_window *w, t_ui_state *ui, t_app *app, t_rect content)
{
	t_rect	body;
	t_rect	left;
	t_rect	right;
	int		gap;
	int		right_w;

	app_page_header(w, ui, content, "Globals", "Pilotage + feed globals", &body);
	gap = UI_PAD;
	right_w = (int)(body.w * 0.34);
	if (right_w < 260)
		right_w = 260;
	if (right_w > body.w - 260)
		right_w = body.w - 260;
	left = (t_rect){body.x + UI_PAD, body.y + UI_PAD,
		body.w - right_w - (UI_PAD * 3), body.h - UI_PAD * 2};
	right = (t_rect){left.x + left.w + gap, left.y, right_w, left.h};

	/* Toolbar contextuelle */
	{
		t_rect	tr = (t_rect){body.x + UI_PAD, content.y + 56, body.w - UI_PAD * 2, 28};
		int bx = tr.x;
		int by = tr.y;
		int bh = 28;
		int bw;
		const char *lbl;
		lbl = "Start LIVE";
		bw = ui_measure_text_w(lbl, 14) + 24;
		if (ui_button(w, ui, (t_rect){bx, by, bw, bh}, lbl, UI_BTN_PRIMARY, !globals_thread_is_running()))
		{
			globals_thread_start_live();
			app->globals_mode_live = 1;
		}
		bx += bw + 8;
		lbl = "Start REPLAY";
		bw = ui_measure_text_w(lbl, 14) + 24;
		if (ui_button(w, ui, (t_rect){bx, by, bw, bh}, lbl, UI_BTN_SECONDARY, !globals_thread_is_running()))
		{
			globals_thread_start_replay();
			app->globals_mode_live = 0;
		}
		bx += bw + 8;
		lbl = "Stop";
		bw = ui_measure_text_w(lbl, 14) + 24;
		if (ui_button(w, ui, (t_rect){bx, by, bw, bh}, lbl, UI_BTN_SECONDARY, 1))
			globals_thread_stop();
		bx += bw + 8;
		lbl = "Clear CSV";
		bw = ui_measure_text_w(lbl, 14) + 24;
		if (ui_button(w, ui, (t_rect){bx, by, bw, bh}, lbl, UI_BTN_GHOST, 1))
			ui_action_clear_globals_csv(w);
	}

	/* Middle/left: infos (scrollable + clipped) */
	ui_draw_panel(w, left, ui->theme->bg, ui->theme->border);
	{
		t_rect	clip;
		int		scroll;
		int		y_logic;
		int		content_h;
		char	line[256];
		size_t	i;

		clip = (t_rect){left.x + 1, left.y + 1, left.w - 2, left.h - 2};
		scroll = app->globals_info_scroll;

		ui_section_header_clipped(w, ui,
			(t_rect){left.x, left.y - scroll, left.w, 28},
			"Resume globals", clip);
		y_logic = left.y + 44;
		if (app->globals_stats_ok)
		{
			snprintf(line, sizeof(line), "CSV: %s", tm_path_globals_csv());
			ui_draw_text_clipped(w, left.x + UI_PAD, y_logic - scroll, line, ui->theme->text2, clip);
			y_logic += 18;
			snprintf(line, sizeof(line), "Lignes lues: %ld", (long)app->globals_stats.data_lines_read);
			ui_draw_text_clipped(w, left.x + UI_PAD, y_logic - scroll, line, ui->theme->text2, clip);
			y_logic += 18;
			snprintf(line, sizeof(line), "Mob events: %ld (%.4f PED)", (long)app->globals_stats.mob_events, app->globals_stats.mob_sum_ped);
			ui_draw_text_clipped(w, left.x + UI_PAD, y_logic - scroll, line, ui->theme->text2, clip);
			y_logic += 18;
			snprintf(line, sizeof(line), "Craft events: %ld (%.4f PED)", (long)app->globals_stats.craft_events, app->globals_stats.craft_sum_ped);
			ui_draw_text_clipped(w, left.x + UI_PAD, y_logic - scroll, line, ui->theme->text2, clip);
			y_logic += 18;
			if (app->globals_stats.rare_events > 0)
			{
				snprintf(line, sizeof(line), "Rare events: %ld (%.4f PED)", (long)app->globals_stats.rare_events, app->globals_stats.rare_sum_ped);
				ui_draw_text_clipped(w, left.x + UI_PAD, y_logic - scroll, line, ui->theme->text2, clip);
				y_logic += 18;
			}
			y_logic += 10;

			ui_section_header_clipped(w, ui,
				(t_rect){left.x, y_logic - scroll, left.w, 28},
				"Top mobs (somme PED)", clip);
			y_logic += 38;
			if (app->globals_stats.top_mobs_count == 0)
			{
				ui_draw_text_clipped(w, left.x + UI_PAD, y_logic - scroll, "(aucun)", ui->theme->text2, clip);
				y_logic += 18;
			}
			else
			{
				i = 0;
				while (i < app->globals_stats.top_mobs_count && i < 6)
				{
					const t_globals_top *t = &app->globals_stats.top_mobs[i];
					snprintf(line, sizeof(line), "%2zu) %-18.18s %8.2f PED  (%ld)", i + 1, t->name, t->sum_ped, (long)t->count);
					ui_draw_text_clipped(w, left.x + UI_PAD, y_logic - scroll, line, ui->theme->text2, clip);
					y_logic += 18;
					i++;
				}
			}
			y_logic += 10;

			ui_section_header_clipped(w, ui,
				(t_rect){left.x, y_logic - scroll, left.w, 28},
				"Top crafts (somme PED)", clip);
			y_logic += 38;
			if (app->globals_stats.top_crafts_count == 0)
			{
				ui_draw_text_clipped(w, left.x + UI_PAD, y_logic - scroll, "(aucun)", ui->theme->text2, clip);
				y_logic += 18;
			}
			else
			{
				i = 0;
				while (i < app->globals_stats.top_crafts_count && i < 6)
				{
					const t_globals_top *t = &app->globals_stats.top_crafts[i];
					snprintf(line, sizeof(line), "%2zu) %-18.18s %8.2f PED  (%ld)", i + 1, t->name, t->sum_ped, (long)t->count);
					ui_draw_text_clipped(w, left.x + UI_PAD, y_logic - scroll, line, ui->theme->text2, clip);
					y_logic += 18;
					i++;
				}
			}

			if (app->globals_stats.rare_events > 0)
			{
				y_logic += 10;
				ui_section_header_clipped(w, ui,
					(t_rect){left.x, y_logic - scroll, left.w, 28},
					"Top rares (somme PED)", clip);
				y_logic += 38;
				if (app->globals_stats.top_rares_count == 0)
				{
					ui_draw_text_clipped(w, left.x + UI_PAD, y_logic - scroll, "(aucun)", ui->theme->text2, clip);
					y_logic += 18;
				}
				else
				{
					i = 0;
					while (i < app->globals_stats.top_rares_count && i < 6)
					{
						const t_globals_top *t = &app->globals_stats.top_rares[i];
						snprintf(line, sizeof(line), "%2zu) %-18.18s %8.2f PED  (%ld)", i + 1, t->name, t->sum_ped, (long)t->count);
						ui_draw_text_clipped(w, left.x + UI_PAD, y_logic - scroll, line, ui->theme->text2, clip);
						y_logic += 18;
						i++;
					}
				}
			}
		}
		else
		{
			ui_draw_text_clipped(w, left.x + UI_PAD, y_logic - scroll, "(pas de stats)", ui->theme->text2, clip);
			y_logic += 18;
			ui_draw_text_clipped(w, left.x + UI_PAD, y_logic - scroll, tm_path_globals_csv(), ui->theme->text2, clip);
			y_logic += 18;
		}

		content_h = (y_logic - left.y) + UI_PAD;
		if (content_h < left.h)
			content_h = left.h;
		if (in_rect_local(w->mouse_x, w->mouse_y, left))
			ui_scroll_update(w, &app->globals_info_scroll, content_h, left.h, 24);
	}

	ui_draw_panel(w, right, ui->theme->surface, ui->theme->border);
	ui_draw_text(w, right.x + 12, right.y + 10, "Activity feed", ui->theme->text);
	ui_draw_text(w, right.x + 12, right.y + 28, "Dernieres lignes CSV", ui->theme->text2);
	if (app->globals_feed_n > 0)
	{
		t_rect lr = (t_rect){right.x + 8, right.y + 52, right.w - 16, right.h - 60};
		ui_text_lines_scroll(w, ui, lr, app->globals_feed_lines, app->globals_feed_n, 14,
			ui->theme->text2, &app->globals_feed_scroll);
	}
	else
		ui_draw_text(w, right.x + 12, right.y + 56, "(vide)", ui->theme->text2);
}

static void	app_page_sessions(t_window *w, t_ui_state *ui, t_app *app, t_rect content)
{
	t_rect	body;
	t_rect	panel;
	t_rect	header;
	int		header_h;
	int		bh;
	int		gap;
	int		btn_export_w;
	int		btn_load_w;
	int		btn_back_w;
	int		actions_w;
	int		info_w;

	app_page_header(w, ui, content, "Sessions / Exports", "Historique des exports (sessions_stats.csv)", &body);
	panel = (t_rect){body.x + UI_PAD, body.y + UI_PAD, body.w - UI_PAD * 2, body.h - UI_PAD * 2};
	ui_draw_panel(w, panel, ui->theme->surface, ui->theme->border);

	/*
	 * Header (info left + actions right)
	 * NOTE: prevents buttons from drawing over the file path / mode line.
	 */
	header_h = 64;
	header = (t_rect){panel.x + 8, panel.y + 8, panel.w - 16, header_h};
	bh = 28;
	gap = 8;

	/* Pre-compute actions width so we can reserve space for the info area. */
	btn_export_w = ui_measure_text_w("Export snapshot", 14) + 24;
	if (btn_export_w < 128) btn_export_w = 128;
	if (btn_export_w > 160) btn_export_w = 160;
	btn_load_w = ui_measure_text_w("Charger session", 14) + 24;
	if (btn_load_w < 128) btn_load_w = 128;
	if (btn_load_w > 170) btn_load_w = 170;
	btn_back_w = ui_measure_text_w("Retour courant", 14) + 24;
	if (btn_back_w < 128) btn_back_w = 128;
	if (btn_back_w > 170) btn_back_w = 170;
	actions_w = btn_back_w + gap + btn_load_w + gap + btn_export_w;
	if (actions_w > header.w - 80)
		actions_w = header.w - 80;
	info_w = header.w - actions_w - gap;
	if (info_w < 120)
		info_w = 120;

	/* Info (left) */
	ui_draw_text(w, header.x + 4, header.y + 6, "Fichier:", ui->theme->text2);
	{
		int lbl_w = ui_measure_text_w("Fichier:", 14) + 10;
		int maxw = info_w - lbl_w;
		if (maxw < 60)
			maxw = 60;
		ui_draw_text_ellipsis_local(w, header.x + 4 + lbl_w, header.y + 6,
			tm_path_sessions_stats_csv(), ui->theme->text2, maxw, 14);
	}

	/* Mode info */
	{
		char	mode_line[256];
		if (app->session_range_active)
			snprintf(mode_line, sizeof(mode_line), "Mode: Session chargee (range %ld..%ld)",
				app->session_range_start, app->session_range_end);
		else
			snprintf(mode_line, sizeof(mode_line), "Mode: Session courante (offset %ld)", app->last_offset);
		ui_draw_text_ellipsis_local(w, header.x + 4, header.y + 30, mode_line,
			app->session_range_active ? ui->theme->accent : ui->theme->text2,
			info_w, 14);
	}

	/* Actions */
	{
		int bx = header.x + header.w;
		int by = header.y + 2;
		/* Export */
		bx -= btn_export_w;
		if (ui_button(w, ui, (t_rect){bx, by, btn_export_w, bh},
			"Export snapshot", UI_BTN_SECONDARY, app->hunt_stats_ok))
			app_export_snapshot(w, app);
		bx -= gap;
		/* Load */
		bx -= btn_load_w;
		if (ui_button(w, ui, (t_rect){bx, by, btn_load_w, bh},
			"Charger session", UI_BTN_PRIMARY, 1))
			app_session_picker_open(w, app);
		bx -= gap;
		/* Back */
		bx -= btn_back_w;
		if (ui_button(w, ui, (t_rect){bx, by, btn_back_w, bh},
			"Retour courant", UI_BTN_GHOST, app->session_range_active))
		{
			(session_clear_range(tm_path_session_range()));
			app->last_refresh_ms = 0;
		}
	}
	if (app->sessions_feed_n > 0)
	{
		t_rect lr = (t_rect){panel.x + 8, panel.y + header_h + 12,
			panel.w - 16, panel.h - (header_h + 20)};
		ui_text_lines_scroll(w, ui, lr, app->sessions_feed_lines, app->sessions_feed_n, 14,
			ui->theme->text2, &app->sessions_feed_scroll);
	}
	else
		ui_draw_text(w, panel.x + 12, panel.y + header_h + 24, "(aucun export)", ui->theme->text2);
}

static void	app_page_config(t_window *w, t_ui_state *ui, t_app *app, t_rect content)
{
	t_rect	body;
	t_rect	panel;

	(void)app;
	app_page_header(w, ui, content, "Configuration", "Armes / Markup / options", &body);
	panel = (t_rect){body.x + UI_PAD, body.y + UI_PAD, body.w - UI_PAD * 2, body.h - UI_PAD * 2};
	ui_draw_panel(w, panel, ui->theme->surface, ui->theme->border);
	ui_draw_text(w, panel.x + 12, panel.y + 12, "Outils", ui->theme->text);
	ui_draw_text(w, panel.x + 12, panel.y + 34, "Ces ecrans utilisent les menus existants.", ui->theme->text2);
	if (ui_button(w, ui, (t_rect){panel.x + 12, panel.y + 70, 260, 34},
		"Gerer Armes (INI)", UI_BTN_PRIMARY, 1))
		menu_config_armes(w);
	if (ui_button(w, ui, (t_rect){panel.x + 12, panel.y + 112, 260, 34},
		"Gerer Markup (INI)", UI_BTN_SECONDARY, 1))
		menu_config_markup(w);
	ui_draw_text(w, panel.x + 12, panel.y + 170,
		"Astuce: le bouton 'Arme:' en topbar ouvre un selecteur rapide.", ui->theme->text2);
}

static void	app_page_maintenance(t_window *w, t_ui_state *ui, t_app *app, t_rect content)
{
	t_rect	body;
	t_rect	panel;

	(void)app;
	app_page_header(w, ui, content, "Maintenance", "Actions systeme (attention)", &body);
	panel = (t_rect){body.x + UI_PAD, body.y + UI_PAD, body.w - UI_PAD * 2, body.h - UI_PAD * 2};
	ui_draw_panel(w, panel, ui->theme->surface, ui->theme->border);
	ui_draw_text(w, panel.x + 12, panel.y + 12, "Actions", ui->theme->text);
	if (ui_button(w, ui, (t_rect){panel.x + 12, panel.y + 60, 260, 34},
		"Stop ALL parsers", UI_BTN_PRIMARY, 1))
		stop_all_parsers(w, 0, 0);
	if (ui_button(w, ui, (t_rect){panel.x + 12, panel.y + 102, 260, 34},
		"Vider CSV chasse", UI_BTN_GHOST, 1))
		ui_action_clear_hunt_csv(w);
	if (ui_button(w, ui, (t_rect){panel.x + 12, panel.y + 144, 260, 34},
		"Vider CSV globals", UI_BTN_GHOST, 1))
		ui_action_clear_globals_csv(w);
	ui_draw_text(w, panel.x + 12, panel.y + 196,
		"Astuce: 'Offset -> fin CSV' permet de demarrer une nouvelle session sans vider le fichier.",
		ui->theme->text2);
}

/* -------------------------------------------------------------------------- */
/* HEALTH page (high-stakes operational trust)                                */
/* -------------------------------------------------------------------------- */

static unsigned int	health_color_level(HealthLevel l)
{
	if (l == HEALTH_OK)
		return (0x55FF7A);
	if (l == HEALTH_WARN)
		return (0xFFB020);
	return (0xFF3B3B);
}

static const char	*health_label_level(HealthLevel l)
{
	if (l == HEALTH_OK)
		return ("OK");
	if (l == HEALTH_WARN)
		return ("WARN");
	return ("FAIL");
}

static void	fmt_bytes(char *out, size_t outsz, long v)
{
	const char	*unit[] = {"B", "KiB", "MiB", "GiB"};
	double			sz;
	int			i;

	if (!out || outsz == 0)
		return ;
	if (v < 0)
		v = 0;
	sz = (double)v;
	i = 0;
	while (sz >= 1024.0 && i < 3)
	{
		sz /= 1024.0;
		i++;
	}
	if (i == 0)
		snprintf(out, outsz, "%ld %s", v, unit[i]);
	else
		snprintf(out, outsz, "%.2f %s", sz, unit[i]);
}

static void	health_pill(t_window *w, int x, int y, HealthLevel l)
{
	unsigned int	c;

	if (!w)
		return ;
	c = health_color_level(l);
	window_fill_rect(w, x, y, 10, 10, c);
}

static void	app_page_health(t_window *w, t_ui_state *ui, t_app *app, t_rect content)
{
	MonitorHealth	h;
	uint64_t		now_ms;
	t_rect			body;
	t_rect			grid;
	t_rect			io;
	t_rect			lat;
	t_rect			err;
	char			buf[256];
	char			sz_chat[64];
	char			sz_csv[64];
	unsigned int	c_ok;
	unsigned int	c_border;
	unsigned int	c_muted;

	(void)app;
	if (!w || !ui)
		return ;
	c_ok = 0x55FF7A;
	c_border = ui->theme->border;
	c_muted = ui->theme->text2;

	now_ms = ft_time_ms();
	monitor_health_snapshot(&h, now_ms);

	app_page_header(w, ui, content, "Health", "I/O + Latence + erreurs (RCE-safe)", &body);

	/* Layout: 2 columns top (I/O, Parser) + full width bottom (Errors) */
	grid = (t_rect){body.x + UI_PAD, body.y + UI_PAD, body.w - UI_PAD * 2, body.h - UI_PAD * 2};
	io = (t_rect){grid.x, grid.y, grid.w / 2 - 6, 170};
	lat = (t_rect){grid.x + grid.w / 2 + 6, grid.y, grid.w / 2 - 6, 170};
	err = (t_rect){grid.x, grid.y + 170 + 12, grid.w, grid.h - (170 + 12)};

	ui_draw_panel(w, io, ui->theme->surface, c_border);
	ui_draw_panel(w, lat, ui->theme->surface, c_border);
	ui_draw_panel(w, err, ui->theme->surface, c_border);

	/* --- I/O block --- */
	ui_draw_text(w, io.x + 12, io.y + 10, "I/O", ui->theme->text);
	health_pill(w, io.x + 12, io.y + 34, h.io_level);
	snprintf(buf, sizeof(buf), "Status: %s", health_label_level(h.io_level));
	ui_draw_text(w, io.x + 28, io.y + 32, buf, health_color_level(h.io_level));

	fmt_bytes(sz_chat, sizeof(sz_chat), h.chat_size);
	fmt_bytes(sz_csv, sizeof(sz_csv), h.csv_size);
	snprintf(buf, sizeof(buf), "chat.log  size: %s", sz_chat);
	ui_draw_text(w, io.x + 12, io.y + 58, buf, ui->theme->text);
	snprintf(buf, sizeof(buf), "chat.log  pos : %ld", h.chat_pos);
	ui_draw_text(w, io.x + 12, io.y + 76, buf, ui->theme->text);
	snprintf(buf, sizeof(buf), "hunt.csv  size: %s", sz_csv);
	ui_draw_text(w, io.x + 12, io.y + 94, buf, ui->theme->text);

	/* last flush + heartbeat */
	if (h.last_flush_ms == 0)
	{
		snprintf(buf, sizeof(buf), "last flush: n/a");
		ui_draw_text(w, io.x + 12, io.y + 112, buf, c_muted);
	}
	else
	{
		uint64_t ago = (now_ms >= h.last_flush_ms) ? (now_ms - h.last_flush_ms) : 0;
		char t[32];
		fmt_hms(t, sizeof(t), ago);
		snprintf(buf, sizeof(buf), "last flush: %s ago", t);
		ui_draw_text(w, io.x + 12, io.y + 112, buf, ui->theme->text2);
		/* heartbeat blink when recent */
		if (ago < 1500)
		{
			unsigned int blink = ((now_ms / 250ULL) % 2ULL) ? c_ok : ui->theme->border;
			window_fill_rect(w, io.x + io.w - 22, io.y + 14, 10, 10, blink);
			ui_draw_text(w, io.x + io.w - 88, io.y + 12, "FLUSH", c_muted);
		}
	}
	if (h.rotation_detected)
		ui_draw_text(w, io.x + 12, io.y + 134, "Rotation detected (chat.log)", 0xFFB020);
	if (h.io_errors > 0 || h.last_ferror != 0)
	{
		snprintf(buf, sizeof(buf), "I/O errors: %d  errno:%d  ferror:%d", h.io_errors, h.last_errno, h.last_ferror);
		ui_draw_text(w, io.x + 12, io.y + 152, buf, 0xFF3B3B);
	}

	/* --- Parser / Latence block --- */
	ui_draw_text(w, lat.x + 12, lat.y + 10, "Parser / Latence", ui->theme->text);
	health_pill(w, lat.x + 12, lat.y + 34, h.lag_level);
	snprintf(buf, sizeof(buf), "Behind: %lld ms (%s)", (long long)h.lag_ms, health_label_level(h.lag_level));
	ui_draw_text(w, lat.x + 28, lat.y + 32, buf, health_color_level(h.lag_level));
	if (h.last_event_ms == 0)
		ui_draw_text(w, lat.x + 12, lat.y + 58, "last event: n/a", c_muted);
	else
	{
		uint64_t ago = (now_ms >= h.last_event_ms) ? (now_ms - h.last_event_ms) : 0;
		char t[32];
		fmt_hms(t, sizeof(t), ago);
		snprintf(buf, sizeof(buf), "last event: %s ago", t);
		ui_draw_text(w, lat.x + 12, lat.y + 58, buf, ui->theme->text2);
	}
	{
		double eps = (double)h.events_per_sec_x100 / 100.0;
		snprintf(buf, sizeof(buf), "events/sec (10s avg): %.2f", eps);
		ui_draw_text(w, lat.x + 12, lat.y + 78, buf, ui->theme->text);
	}
	snprintf(buf, sizeof(buf), "parse errors: %d", h.parse_errors);
	ui_draw_text(w, lat.x + 12, lat.y + 98, buf, h.parse_errors ? 0xFFB020 : c_muted);

	/* --- Errors ring buffer --- */
	ui_draw_text(w, err.x + 12, err.y + 10, "Errors (last 10)", ui->theme->text);
	if (h.errors_count == 0)
		ui_draw_text(w, err.x + 12, err.y + 36, "No recent errors.", c_muted);
	else
	{
		int y = err.y + 34;
		for (int i = 0; i < h.errors_count && i < HEALTH_ERR_RING; i++)
		{
			t_health_error *e = &h.errors[i];
			uint64_t ago = (now_ms >= e->ts_ms) ? (now_ms - e->ts_ms) : 0;
			char t[32];
			fmt_hms(t, sizeof(t), ago);
			health_pill(w, err.x + 12, y + 2, e->level);
			snprintf(buf, sizeof(buf), "%s ago  |  %s  |  errno:%d ferror:%d", t, e->msg, e->err_no, e->ferror_code);
			ui_draw_text(w, err.x + 28, y, buf, health_color_level(e->level));
			y += 18;
			if (y > err.y + err.h - 18)
				break ;
		}
	}
}

static void	app_page_aide(t_window *w, t_ui_state *ui, t_app *app, t_rect content)
{
	t_rect	body;
	t_rect	panel;
	t_rect	list;
	char	chatpath[1024];
	int		chat_ok;

	const char	*lines[] = {
		"======================================================================",
		"tracker_loot - AIDE COMPLETE (RCE / Entropia Universe)",
		"======================================================================",
		"",
		"0) Cheat-sheet (focus 'hunt in progress')",
		"- Fleches Haut/Bas : naviguer (sidebar, listes)",
		"- Enter            : ouvrir / valider",
		"- Echap            : retour / fermer un selecteur",
		"- Molette          : scroll (feeds, Aide, listes, Graph LIVE)",
		"- O                : Overlay ON/OFF (reste discret, ne vole pas le focus jeu)",
		"- H                : Health (I/O, lag, erreurs)",
		"- Dans Graph LIVE  : onglet 'Retour' ou Echap => revient a Chasse",
		"- TAB (debug)      : dashboard legacy depuis Chasse (ancien layout / profiling)",
		"",
		"1) Demarrage rapide (RCE-safe)",
		"- Dans Entropia: Options -> activer l'ecriture du chat.log (Write chat log).",
		"- Lancer tracker_loot et verifier la ligne de statut: chat.log + root.",
		"- Chasse: choisir l'arme (bouton 'Arme:' en topbar) puis Start LIVE.",
		"- Fin de session: Stop+Export (snapshot + offset pret pour la prochaine hunt).",
		"",
		"2) Interface / Navigation",
		"- Sidebar 'Navigation': Chasse | Globals | Graph LIVE | Sessions/Exports | Configuration",
		"- Sidebar 'System'    : Health | Maintenance | Aide | Quitter",
		"- Tout est cliquable (souris). Les listes ont un scroll molette.",
		"- Champs texte (Config): clic pour focus, taper, Backspace efface 1 caractere.",
		"",
		"3) Coeur de verite / Precision RCE (CSV V2)",
		"- logs/hunt_log.csv est append-only et contient TOUT (shots/kills/loot/expense).",
		"- Unites: 1 PED = 100 PEC = 10000 uPED (stockage entier, pas de flottant en compta).",
		"- Colonnes V2: ts_unix,event_type,qty,value_uPED,kill_id,flags,raw",
		"- kill_id: assigne sur KILL; LOOT_ITEM rattache au meilleur kill recent (fenetre 60s).",
		"- Multi-loots meme seconde: label 'L 2.34 xN' = N loots groupees au meme timestamp.",
		"- Crash recovery: si le CSV finit sans \\n, la derniere ligne partielle est tronquee.",
		"",
		"4) Chasse - Actions disponibles",
		"- Start LIVE    : suit les nouvelles lignes du chat.log (temps reel).",
		"- Start REPLAY  : relit le chat.log et reconstruit (utile apres crash / reindex).",
		"- Arme active   : bouton 'Arme:' (topbar) => selecteur rapide + persistance.",
		"- Stop+Export   : stop parser + export (sessions_stats.csv) + avance l'offset.",
		"- Stop (sans export): via l'Overlay (bouton Stop).",
		"- Offset fin CSV: demarre une nouvelle session logique sans vider le fichier.",
		"- Sweat ON/OFF  : inclure/exclure Vibrant Sweat des stats + graphs.",
		"- Mob (cible)   : demande au Start; sert aux exports et a la lisibilite.",
		"",
		"5) Graph LIVE - Manipulations (ecran full-screen)",
		"- Acces: menu principal -> Graph LIVE (sous Globals).",
		"- Navigation: Fleches/Enter ou clic dans la sidebar (Molette si la liste tient).",
		"- Onglets: Shots/kill | Hit-rate | Hits/kill | Kills | Loot/kill | Loot cumul | Retour",
		"- Filtres temps: boutons 15/30/60/Tout (fenetre affichee).",
		"- Mode SESSION: si une session est chargee (range), le graphe affiche start..end.",
		"- Labels: Shots/kill, Hit-rate, Hits/kill affichent une valeur sur TOUS les points.",
		"- Loot cumul: somme exacte en uPED (precision comptable).",
		"",
		"6) Overlay (HUD discret in-game)",
		"- Toggle: touche O.",
		"- Deplacement: drag sur la bande haute (zone titre) pour le bouger.",
		"- Boutons: Start / Stop / Stop+Export (selon etat du parser).",
		"- Mob non affiche: si manquant au Start, l'overlay demande le nom une fois puis le sauvegarde.",
		"",
		"7) Sessions / Exports (rejouer une hunt)",
		"- Export snapshot : ecrit un resume de l'offset courant (sans stopper).",
		"- Charger session : ouvre un picker, puis ecrit hunt_session.range (view SESSION).",
		"- Retour courant  : efface le range et revient a la session live (offset).",
		"",
		"8) Configuration (armes.ini / markup.ini)",
		"- Armes: couts par tir (ammo/decay/amp) + MU (multiplicateur 1.0000=100%).",
		"- [PLAYER] name=TonPseudo (armes.ini) => filtre globals sur ton perso.",
		"- Markup: regles TT->MU pour afficher Loot TT / MU / TT+MU (RCE-friendly).",
		"- Valeurs conseillees: PED avec 4 decimales (ex 0.0123). MU accepte . ou ,.",
		"",
		"9) Maintenance (DANGEREUX)",
		"- Stop ALL parsers : coupe chasse + globals.",
		"- Vider CSV chasse / globals: demande confirmation YES, puis recree l'en-tete.",
		"",
		"10) Chemins (chat.log) / Override",
		"- Windows: Documents/Entropia Universe/chat.log (ou OneDrive/Documents/...).",
		"- Linux  : ~/Documents/Entropia Universe/chat.log (souvent via Wine).",
		"- Override: definir ENTROPIA_CHATLOG=/chemin/complet/vers/chat.log",
		"- Root data: dossier de l'exe (ou parent si layout bin/..) => place armes.ini/markup.ini ici.",
		"",
		"11) Fichiers importants (root/logs)",
		"- logs/hunt_log.csv        : CSV V2 chasse (core of truth)",
		"- logs/hunt_session.offset : offset (debut session logique)",
		"- logs/hunt_session.range  : range [start,end) (mode session chargee)",
		"- logs/sessions_stats.csv  : historique des exports/snapshots",
		"- logs/globals.csv         : globals / hof / ath",
		"- logs/parser_debug.log    : erreurs (chemins + errno)",
		"",
		"12) Depannage express",
		"- Rien ne bouge: verifier chat.log actif + bon chemin (statut en haut).",
		"- Erreurs parser: ouvrir logs/parser_debug.log.",
		"- CSV 'legacy': backup .legacy.bak puis regeneration V2 automatique.",
		"- Pour valider une session propre: Stop+Export (recommande).",
	};

	app_page_header(w, ui, content, "Aide", "Raccourcis + fichiers + debug", &body);

	panel = (t_rect){body.x + UI_PAD, body.y + UI_PAD, body.w - UI_PAD * 2, body.h - UI_PAD * 2};
	ui_draw_panel(w, panel, ui->theme->surface, ui->theme->border);

	/* Small status line */
	chat_ok = app_chatlog_ok(chatpath, sizeof(chatpath));
	{
		char st[1400];
		snprintf(st, sizeof(st), "chat.log: %s   |   root: %s", chatpath, tm_app_root());
		ui_draw_text(w, panel.x + 12, panel.y + 10, st, chat_ok ? ui->theme->success : ui->theme->warn);
	}

	list = (t_rect){panel.x + 8, panel.y + 36, panel.w - 16, panel.h - 44};
	ui_text_lines_scroll(w, ui, list, lines, (int)(sizeof(lines) / sizeof(lines[0])),
		16, ui->theme->text2, &app->help_scroll);
}


int	main(int argc, char **argv)
{
	t_window		w;
	t_frame_limiter	fl;
	t_ui_state	ui;
	t_app			app;
	const char		*footer;
	t_ui_layout	ly;
	t_rect			content;

	memset(&app, 0, sizeof(app));
	(void)argc;
	tm_paths_init(argv ? argv[0] : NULL);
	/* Default page on startup: Chasse (Dashboard removed). */
	app.page = PAGE_CHASSE;
	app.prev_page = PAGE_CHASSE;
	app.nav_cursor = 0;
	app.hunt_mode_live = 1;
	app.globals_mode_live = 1;
	ui.theme = &g_theme_dark;
	fl.target_ms = 16;

	if (window_init(&w, "tracker_loot", 1024, 768) != 0)
		return (1);
	ui_ensure_globals_csv();
	ui_ensure_hunt_csv();
	/*
	 * Step3 (V2 reload):
	 * Recharge immediatement le cache de graph LIVE a partir du CSV V2.
	 * (meme si le parser LIVE/REPLAY n'est pas lance)
	 */
	hunt_series_live_bootstrap();
	/* Step7: Recharge aussi le cache stats live (overlay + pages). */
	tracker_stats_live_bootstrap();

	while (w.running)
	{
		fl_begin(&fl);
		window_poll_events(&w);

		/* Hotkey: H toggles the HEALTH page (operational trust). */
		if (w.key_h)
		{
			if (app.page != PAGE_HEALTH)
			{
				app.prev_page = app.page;
				app.page = PAGE_HEALTH;
				app.nav_cursor = page_to_nav_cursor(PAGE_HEALTH);
			}
			else
			{
				app.page = (app.prev_page != PAGE_HEALTH) ? app.prev_page : PAGE_CHASSE;
				app.nav_cursor = page_to_nav_cursor(app.page);
			}
		}
		app_refresh_cached(&app);

		/* Chrome */
		footer = "Up/Down navigation  |  Enter ouvrir  |  Molette: defiler  |  Esc: fermer";
		content = ui_draw_chrome_ex(&w, &ui, NULL, NULL, footer, 280);
		ui_calc_layout_ex(&w, &ly, 280);

		/* Sidebar nav + Topbar */
		app_sidebar_nav(&w, &ui, &ly, &app);
		app_topbar(&w, &ui, &ly, &app);

		/* Pages */
		if (app.page == PAGE_CHASSE)
			app_page_chasse(&w, &ui, &app, content);
		else if (app.page == PAGE_GLOBALS)
			app_page_globals(&w, &ui, &app, content);
		else if (app.page == PAGE_SESSIONS)
			app_page_sessions(&w, &ui, &app, content);
		else if (app.page == PAGE_CONFIG)
			app_page_config(&w, &ui, &app, content);
		else if (app.page == PAGE_MAINTENANCE)
			app_page_maintenance(&w, &ui, &app, content);
		else if (app.page == PAGE_AIDE)
			app_page_aide(&w, &ui, &app, content);
		else if (app.page == PAGE_HEALTH)
			app_page_health(&w, &ui, &app, content);
		else
		{
			/* Dashboard removed: fall back to Chasse for any unknown page value. */
			app.page = PAGE_CHASSE;
			app.nav_cursor = 0;
			app_page_chasse(&w, &ui, &app, content);
		}

		/* Overlay weapon picker */
		if (w.key_escape && app.weapon_picker_open)
			app_weapon_picker_close(&app);
		app_draw_weapon_picker(&w, &ui, &app, content);

		/* Overlay session picker */
		if (w.key_escape && app.session_picker_open)
			app_session_picker_close(&app);
		app_draw_session_picker(&w, &ui, &app, content);

		/* Hotkey: O toggles overlay (comme test.tar.gz) */
		if (w.key_o)
		{
			overlay_toggle();
			if (overlay_is_enabled() && app.session_start_ms == 0)
				app.session_start_ms = ft_time_ms();
		}

		/* Keep overlay session clock in sync (overlay can also drive the clock). */
		overlay_sync_session_clock(&app.session_start_ms);

		/* Overlay tick (stats + temps) */
		if (overlay_is_enabled())
		{
			uint64_t now = ft_time_ms();
			unsigned long long elapsed = 0ULL;
			if (app.session_start_ms != 0)
				elapsed = (unsigned long long)(now - app.session_start_ms);
			overlay_tick(app.hunt_stats_ok ? &app.hunt_stats : NULL, elapsed);
			/* If overlay buttons changed the session clock, pull it back. */
			overlay_sync_session_clock(&app.session_start_ms);
		}

		window_present(&w);
		fl_end_sleep(&fl);
	}

	app_weapon_picker_close(&app);
	window_destroy(&w);
	return (0);
}
