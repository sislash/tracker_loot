/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   tracker_view.c                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: login <login@student.42.fr>                +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/01/31 00:00:00 by login             #+#    #+#             */
/*   Updated: 2026/02/12 00:00:00 by tracker_loot     ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */
/*
 * Legacy window-only menu for globals.
 *
 * NOTE: This file is excluded from the default build. Compile with:
 *   make LEGACY=1
 */

#include "menu_globals.h"
#include "menu_principale.h"

#include "window.h"
#include "utils.h"
#include "frame_limiter.h"
#include "ui_utils.h"
#include "ui_chrome.h"
#include "ui_layout.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "overlay.h"

#include "core_paths.h"
#include "globals_stats.h"
#include "globals_thread.h"
#include "csv.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/* ************************************************************************** */
/*  Helpers                                                                   */
/* ************************************************************************** */

/* shared helpers moved to:
 *  - ui_utils.c: ui_draw_lines(), ui_screen_message(), ui_action_clear_globals_csv(), ui_ensure_globals_csv()
 *  - utils.c: tm_fmt_linef()
 */

static double	safe_div(double a, double b)
{
	if (b == 0.0)
		return (0.0);
	return (a / b);
}

static double	ratio_pct(double num, double den)
{
	if (den <= 0.0)
		return (0.0);
	return ((num / den) * 100.0);
}


/* ************************************************************************** */
/*  Dashboard LIVE (pages + tops)                                             */
/* ************************************************************************** */

typedef enum e_gdash_page
{
	GDASH_RESUME = 0,
	GDASH_TOP_MOBS,
	GDASH_TOP_CRAFTS,
	GDASH_TOP_RARES,
	GDASH_COUNT
}	t_gdash_page;

static size_t	clamp_top_offset(size_t off, size_t count, size_t page_size)
{
	if (count == 0)
		return (0);
	if (page_size >= count)
		return (0);
	if (off > count - page_size)
		return (count - page_size);
	return (off);
}

static void	build_top_page(const t_globals_top *top, size_t count,
						   const char *title,
						   size_t off, size_t page_size,
						   char buf[64][256], const char **lines, int *out_n)
{
	int		n;
	size_t	i;
	size_t	end;
	
	n = 0;
	snprintf(buf[n++], sizeof(buf[0]), "%s", title);
	buf[n++][0] = '\0';
	if (count == 0)
	{
		snprintf(buf[n++], sizeof(buf[0]), "(Aucun item)");
		*out_n = n;
		i = 0;
		while (i < (size_t)n)
		{
			lines[i] = buf[i];
			i++;
		}
		return ;
	}
	off = clamp_top_offset(off, count, page_size);
	end = off + page_size;
	if (end > count)
		end = count;
	
	snprintf(buf[n++], sizeof(buf[0]),
			 "Items %zu-%zu / %zu  (Z/S scroll)",
			 off + 1, end, count);
	buf[n++][0] = '\0';
	
	i = off;
	while (i < end && n < 62)
	{
		snprintf(buf[n++], sizeof(buf[0]),
				 "%2zu) %-28s  x%ld  %.4f PED",
		   i + 1, top[i].name, top[i].count, top[i].sum_ped);
		i++;
	}
	*out_n = n;
	i = 0;
	while (i < (size_t)n)
	{
		lines[i] = buf[i];
		i++;
	}
}

static void	screen_dashboard_globals_live(t_window *w)
{
	t_globals_stats	s;
	t_gdash_page		page;
	size_t			top_off;
	const size_t		page_size = 10;
	
	char			buf[64][256];
	const char		*lines[64];
	int				n;
	int				scroll_y;
	
	page = GDASH_RESUME;
	top_off = 0;
	scroll_y = 0;
	/* Smoother input: render every frame (~60fps), recompute stats ~250ms */
	{
		t_frame_limiter	fl;
		uint64_t		last_ms;
		uint64_t		now_ms;
		double			dt;
		double			refresh_acc;
		int				first;

		fl.target_ms = 16;
		last_ms = ft_time_ms();
		refresh_acc = 1.0;
		first = 1;
		while (w->running)
		{
			fl_begin(&fl);
			now_ms = ft_time_ms();
			dt = (double)(now_ms - last_ms) / 1000.0;
			last_ms = now_ms;
			if (dt < 0.0)
				dt = 0.0;

			window_poll_events(w);
		overlay_tick_auto_hunt();
			if (w->key_escape)
				break ;
			/* page switch */
			if (w->key_q)
			{
				page = (t_gdash_page)((page + GDASH_COUNT - 1) % GDASH_COUNT);
				top_off = 0;
				refresh_acc = 1.0;
					scroll_y = 0;
			}
			else if (w->key_d)
			{
				page = (t_gdash_page)((page + 1) % GDASH_COUNT);
				top_off = 0;
				refresh_acc = 1.0;
					scroll_y = 0;
			}
			/* scroll tops */
			if (page != GDASH_RESUME)
			{
				if (w->key_z)
				{
					if (top_off > 0)
						top_off--;
				}
				else if (w->key_s)
					top_off++;
			}

			refresh_acc += dt;
			if (first || refresh_acc >= 0.250)
			{
				first = 0;
				refresh_acc = 0.0;
				memset(&s, 0, sizeof(s));
				(void)globals_stats_compute(tm_path_globals_csv(), 0, &s);
			}

			{
				t_ui_state	ui;
				t_ui_layout	ly;
				t_rect		content;
				int			max_y;

				ui.theme = &g_theme_dark;
				/* Sidebar width fits the longest page label */
				{
					const char *labels[] = {"Resume", "Tops Mobs", "Tops Items", "Tops Loot"};
					int i;
					int maxw;
					int sw;
					maxw = 0;
					i = 0;
					while (i < (int)(sizeof(labels)/sizeof(labels[0])))
					{
						int tw = ui_measure_text_w(labels[i], 14);
						if (tw > maxw)
							maxw = tw;
						i++;
					}
					sw = maxw + 4 + (UI_PAD * 2) + 24;
					ui_calc_layout_ex(w, &ly, sw);
					content = ui_draw_chrome_ex(w, &ui, "Globals / Live",
					globals_thread_is_running() ? "Parser: OK" : "Parser: STOP",
						"Q/D pages  |  Z/S tops  |  molette scroll  |  Echap retour", sw);
				}
				ui_draw_panel(w, content, ui.theme->bg, ui.theme->border);
				max_y = content.y + content.h - UI_PAD;
				if (max_y < 0)
					max_y = 0x7FFFFFFF;

				if (page == GDASH_RESUME)
			{
				n = 0;
				snprintf(buf[n++], sizeof(buf[0]),
						 "[PAGE 1/%d] RESUME", GDASH_COUNT);
				tm_fmt_linef(buf[n++], sizeof(buf[0]), "CSV", "%s", tm_path_globals_csv());
				buf[n++][0] = '\0';
				tm_fmt_linef(buf[n++], sizeof(buf[0]), "Lignes lues", "%ld", s.data_lines_read);
				buf[n++][0] = '\0';
				tm_fmt_linef(buf[n++], sizeof(buf[0]), "Mob events", "%ld", s.mob_events);
				tm_fmt_linef(buf[n++], sizeof(buf[0]), "Mob sum", "%.4f PED", s.mob_sum_ped);
				tm_fmt_linef(buf[n++], sizeof(buf[0]), "Avg / mob", "%.4f PED",
						  safe_div(s.mob_sum_ped, (double)s.mob_events));
				buf[n++][0] = '\0';
				tm_fmt_linef(buf[n++], sizeof(buf[0]), "Craft events", "%ld", s.craft_events);
				tm_fmt_linef(buf[n++], sizeof(buf[0]), "Craft sum", "%.4f PED", s.craft_sum_ped);
				tm_fmt_linef(buf[n++], sizeof(buf[0]), "Avg / craft", "%.4f PED",
						  safe_div(s.craft_sum_ped, (double)s.craft_events));
				buf[n++][0] = '\0';
				tm_fmt_linef(buf[n++], sizeof(buf[0]), "Rare events", "%ld", s.rare_events);
				tm_fmt_linef(buf[n++], sizeof(buf[0]), "Rare sum", "%.4f PED", s.rare_sum_ped);
				tm_fmt_linef(buf[n++], sizeof(buf[0]), "Avg / rare", "%.4f PED",
						  safe_div(s.rare_sum_ped, (double)s.rare_events));
				buf[n++][0] = '\0';
				tm_fmt_linef(buf[n++], sizeof(buf[0]), "Rare ratio", "%.2f %%",
						  ratio_pct((double)s.rare_events, (double)(s.mob_events + s.craft_events)));
				for (int ii = 0; ii < n; ii++)
					lines[ii] = buf[ii];
				if (w->mouse_x >= content.x && w->mouse_x < content.x + content.w
					&& w->mouse_y >= content.y && w->mouse_y < content.y + content.h)
					ui_scroll_update(w, &scroll_y, (n * 14) + UI_PAD * 2,
						content.h, 24);
				ui_draw_lines_clipped(w, content.x + UI_PAD, content.y + UI_PAD - scroll_y,
					lines, n, 14, ui.theme->text2, max_y);
			}
			else if (page == GDASH_TOP_MOBS)
			{
				snprintf(buf[0], sizeof(buf[0]), "[PAGE 2/%d] TOP MOBS", GDASH_COUNT);
				build_top_page(s.top_mobs, s.top_mobs_count,
						   buf[0], top_off, page_size, buf, lines, &n);
				top_off = clamp_top_offset(top_off, s.top_mobs_count, page_size);
				if (w->mouse_x >= content.x && w->mouse_x < content.x + content.w
					&& w->mouse_y >= content.y && w->mouse_y < content.y + content.h)
					ui_scroll_update(w, &scroll_y, (n * 14) + UI_PAD * 2,
						content.h, 24);
				ui_draw_lines_clipped(w, content.x + UI_PAD, content.y + UI_PAD - scroll_y,
					lines, n, 14, ui.theme->text2, max_y);
			}
			else if (page == GDASH_TOP_CRAFTS)
			{
				snprintf(buf[0], sizeof(buf[0]), "[PAGE 3/%d] TOP CRAFTS", GDASH_COUNT);
				build_top_page(s.top_crafts, s.top_crafts_count,
						   buf[0], top_off, page_size, buf, lines, &n);
				top_off = clamp_top_offset(top_off, s.top_crafts_count, page_size);
				if (w->mouse_x >= content.x && w->mouse_x < content.x + content.w
					&& w->mouse_y >= content.y && w->mouse_y < content.y + content.h)
					ui_scroll_update(w, &scroll_y, (n * 14) + UI_PAD * 2,
						content.h, 24);
				ui_draw_lines_clipped(w, content.x + UI_PAD, content.y + UI_PAD - scroll_y,
					lines, n, 14, ui.theme->text2, max_y);
			}
			else
			{
				snprintf(buf[0], sizeof(buf[0]), "[PAGE 4/%d] TOP RARES", GDASH_COUNT);
				build_top_page(s.top_rares, s.top_rares_count,
						   buf[0], top_off, page_size, buf, lines, &n);
				top_off = clamp_top_offset(top_off, s.top_rares_count, page_size);
				if (w->mouse_x >= content.x && w->mouse_x < content.x + content.w
					&& w->mouse_y >= content.y && w->mouse_y < content.y + content.h)
					ui_scroll_update(w, &scroll_y, (n * 14) + UI_PAD * 2,
						content.h, 24);
				ui_draw_lines_clipped(w, content.x + UI_PAD, content.y + UI_PAD - scroll_y,
					lines, n, 14, ui.theme->text2, max_y);
			}
				window_present(w);
			}
			fl_end_sleep(&fl);
		}
	}
}

/* ************************************************************************** */
/*  Entry                                                                     */
/* ************************************************************************** */

void	tracker_view_menu_globals(void)
{
	t_window	w;
	t_menu		menu;
	int			action;
	const char	*items[6];
	uint64_t	frame_start;
	uint64_t	frame_ms;
	int			sleep_ms;
	
	if (tm_ensure_logs_dir() != 0)
	{
		/* pas bloquant */
	}
	ui_ensure_globals_csv();
	
	items[0] = "Demarrer LIVE (globals)";
	items[1] = "Demarrer REPLAY (globals)";
	items[2] = "Arreter le parser globals";
	items[3] = "Dashboard LIVE (pages + tops)";
	items[4] = "Vider CSV globals (confirmation YES)";
	items[5] = "Retour";
	
	menu_init(&menu, items, 6);
	if (window_init(&w, "tracker - globals", 900, 650) != 0)
		return ;
	
	while (w.running)
	{
		frame_start = ft_time_ms();
		window_poll_events(&w);
		
		action = menu_update(&menu, &w);
		if (action >= 0)
		{
			if (action == 0)
				globals_thread_start_live();
			else if (action == 1)
				globals_thread_start_replay();
			else if (action == 2)
			{
				globals_thread_stop();
				{
					const char *msg[] = { "OK : parser GLOBALS arrete." };
					ui_screen_message(&w, "PARSER GLOBALS", msg, 1);
				}
			}
			else if (action == 3)
				screen_dashboard_globals_live(&w);
			else if (action == 4)
				ui_action_clear_globals_csv(&w);
			else
				w.running = 0;
		}
		if (w.key_escape)
			w.running = 0;
		
		window_clear(&w, 0xFFFFFF);
		window_draw_text(&w, 30, 20, "MENU GLOBALS / GLOBAUX (mode fenetre)", 0x000000);
		window_draw_text(&w, 30, 45, "Fleches: selection | Entree: valider | Echap: quitter", 0x000000);
		{
			char st[128];
			snprintf(st, sizeof(st), "Etat parser globals : %s",
					globals_thread_is_running() ? "EN COURS" : "ARRETE");
			window_draw_text(&w, 30, 65, st, 0x000000);
		}
		menu_render(&menu, &w, 30, 90);
		window_present(&w);
		
		frame_ms = ft_time_ms() - frame_start;
		sleep_ms = 16 - (int)frame_ms;
		if (sleep_ms > 0)
			ft_sleep_ms(sleep_ms);
	}
	window_destroy(&w);
}
