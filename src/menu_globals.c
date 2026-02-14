/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   menu_globals.c                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: login <login@student.42.fr>                +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/01/31 00:00:00 by login             #+#    #+#             */
/*   Updated: 2026/02/05 00:00:00 by you              ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "menu_globals.h"
#include "menu_principale.h"

#include "window.h"
#include "utils.h"
#include "frame_limiter.h"
#include "ui_utils.h"

/* UI pro (meme style partout) */
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

/* Shared window helpers are centralized in ui_utils.c */

/* ************************************************************************** */
/*  Confirm screen (fenetre)                                                  */
/* ************************************************************************** */

/* confirm/message/clear helpers are centralized in ui_utils.c */

/* ************************************************************************** */
/*  Dashboard helpers                                                         */
/* ************************************************************************** */

#define GDASH_BAR_MAX 26

static int	gdash_bar_fill(double v, double vmax)
{
    int	nb;
    
    if (vmax <= 0.0)
        return (0);
    nb = (int)((v / vmax) * (double)GDASH_BAR_MAX);
    if (nb < 0)
        nb = 0;
    if (nb > GDASH_BAR_MAX)
        nb = GDASH_BAR_MAX;
    return (nb);
}

static void	gdash_bar_str(char out[GDASH_BAR_MAX + 1], double v, double vmax)
{
    int	j;
    int	nb;
    
    nb = gdash_bar_fill(v, vmax);
    j = 0;
    while (j < GDASH_BAR_MAX)
    {
        out[j] = (j < nb) ? '#' : ' ';
        j++;
    }
    out[GDASH_BAR_MAX] = '\0';
}

static double	gdash_max_sum(const t_globals_top *arr, size_t cnt)
{
    double	m;
    size_t	i;
    
    m = 0.0;
    i = 0;
    while (i < cnt)
    {
        if (arr[i].sum_ped > m)
            m = arr[i].sum_ped;
        i++;
    }
    return (m);
}

static void	screen_dashboard_globals_live(t_window *w)
{
	t_ui_state		ui;
	t_ui_layout		ly;
	t_rect			content;
	t_rect			list_r;
	const char		*items[1] = {"Retour"};
	int				selected;
	int				clicked;

	t_globals_stats	s;
	char				buf[96][256];
	const char		*lines[96];
	int				n;
	int				i;
	size_t			k;
	double			mx;
	int				cache_n;
	int				max_y;
	int				scroll_y;

	t_frame_limiter	fl;
	uint64_t		last_ms;
	uint64_t		now_ms;
	double			dt;
	double			refresh_acc;

	if (!w)
		return;
	ui.theme = &g_theme_dark;
	selected = 0;
	fl.target_ms = 16;
	last_ms = ft_time_ms();
	refresh_acc = 1.0;
	cache_n = 0;
	scroll_y = 0;
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
		if (w->key_escape || w->key_enter)
			break;

		refresh_acc += dt;
		if (refresh_acc >= 0.250)
		{
			refresh_acc = 0.0;
			memset(&s, 0, sizeof(s));
			(void)globals_stats_compute(tm_path_globals_csv(), 0, &s);

			n = 0;
			snprintf(buf[n++], sizeof(buf[0]), "DASHBOARD GLOBALS LIVE");
			tm_fmt_linef(buf[n++], sizeof(buf[0]), "CSV", "%s", tm_path_globals_csv());
			buf[n++][0] = '\0';
			tm_fmt_linef(buf[n++], sizeof(buf[0]), "Lignes lues", "%ld", s.data_lines_read);
			tm_fmt_linef(buf[n++], sizeof(buf[0]), "Mob events", "%ld (%.4f PED)", s.mob_events, s.mob_sum_ped);
			tm_fmt_linef(buf[n++], sizeof(buf[0]), "Craft events", "%ld (%.4f PED)", s.craft_events, s.craft_sum_ped);
			if (s.rare_events > 0)
				tm_fmt_linef(buf[n++], sizeof(buf[0]), "Rare events", "%ld (%.4f PED)", s.rare_events, s.rare_sum_ped);
			buf[n++][0] = '\0';

			snprintf(buf[n++], sizeof(buf[0]), "TOP MOBS (somme PED)");
			snprintf(buf[n++], sizeof(buf[0]), "  #  %-24s %10s |%*s| %6s",
					"Nom", "PED", GDASH_BAR_MAX, "", "Count");
			mx = gdash_max_sum(s.top_mobs, s.top_mobs_count);
			if (s.top_mobs_count == 0)
				snprintf(buf[n++], sizeof(buf[0]), "  (aucun)");
			k = 0;
			while (k < s.top_mobs_count && k < 10 && n < 94)
			{
				char bar[GDASH_BAR_MAX + 1];
				gdash_bar_str(bar, s.top_mobs[k].sum_ped, mx);
				snprintf(buf[n++], sizeof(buf[0]),
						"%3zu) %-24.24s %10.2f |%s| %6ld",
						k + 1, s.top_mobs[k].name, s.top_mobs[k].sum_ped,
						bar, s.top_mobs[k].count);
				k++;
			}
			buf[n++][0] = '\0';

			snprintf(buf[n++], sizeof(buf[0]), "TOP CRAFTS (somme PED)");
			snprintf(buf[n++], sizeof(buf[0]), "  #  %-24s %10s |%*s| %6s",
					"Nom", "PED", GDASH_BAR_MAX, "", "Count");
			mx = gdash_max_sum(s.top_crafts, s.top_crafts_count);
			if (s.top_crafts_count == 0)
				snprintf(buf[n++], sizeof(buf[0]), "  (aucun)");
			k = 0;
			while (k < s.top_crafts_count && k < 10 && n < 94)
			{
				char bar[GDASH_BAR_MAX + 1];
				gdash_bar_str(bar, s.top_crafts[k].sum_ped, mx);
				snprintf(buf[n++], sizeof(buf[0]),
						"%3zu) %-24.24s %10.2f |%s| %6ld",
						k + 1, s.top_crafts[k].name, s.top_crafts[k].sum_ped,
						bar, s.top_crafts[k].count);
				k++;
			}
			buf[n++][0] = '\0';

			if (s.rare_events > 0)
			{
				snprintf(buf[n++], sizeof(buf[0]), "TOP RARE ITEMS (somme PED)");
				snprintf(buf[n++], sizeof(buf[0]), "  #  %-24s %10s |%*s| %6s",
						"Nom", "PED", GDASH_BAR_MAX, "", "Count");
				mx = gdash_max_sum(s.top_rares, s.top_rares_count);
				if (s.top_rares_count == 0)
					snprintf(buf[n++], sizeof(buf[0]), "  (aucun)");
				k = 0;
				while (k < s.top_rares_count && k < 10 && n < 94)
				{
					char bar[GDASH_BAR_MAX + 1];
					gdash_bar_str(bar, s.top_rares[k].sum_ped, mx);
					snprintf(buf[n++], sizeof(buf[0]),
							"%3zu) %-24.24s %10.2f |%s| %6ld",
							k + 1, s.top_rares[k].name, s.top_rares[k].sum_ped,
							bar, s.top_rares[k].count);
					k++;
				}
				buf[n++][0] = '\0';
			}

			snprintf(buf[n++], sizeof(buf[0]), "(Auto-refresh ~250ms)");
			cache_n = n;
		}

		i = 0;
		while (i < cache_n)
		{
			lines[i] = buf[i];
			i++;
		}

		ui_calc_layout(w, &ly);
		content = ui_draw_chrome(w, &ui, "Globals / Live", 
			globals_thread_is_running() ? "Parser: OK" : "Parser: STOP",
			"Enter/Echap retour");
		list_r = (t_rect){ly.sidebar.x, ly.sidebar.y + UI_PAD,
			ly.sidebar.w, ly.sidebar.h - UI_PAD * 2};
		clicked = ui_list(w, &ui, list_r, items, 1, &selected, 40, 0);
		if (clicked == 0)
			break;

		ui_draw_panel(w, content, ui.theme->bg, ui.theme->border);
		max_y = content.y + content.h - UI_PAD;
		if (max_y < 0)
			max_y = 0x7FFFFFFF;
		if (w->mouse_x >= content.x && w->mouse_x < content.x + content.w
			&& w->mouse_y >= content.y && w->mouse_y < content.y + content.h)
			ui_scroll_update(w, &scroll_y, (cache_n * 14) + UI_PAD * 2,
				content.h, 24);
		ui_draw_lines_clipped(w, content.x + UI_PAD, content.y + UI_PAD - scroll_y,
			lines, cache_n, 14, ui.theme->text2, max_y);
		window_present(w);
		fl_end_sleep(&fl);
	}
}

/* ************************************************************************** */
/*  Entry                                                                     */
/* ************************************************************************** */

void	menu_globals(t_window *w)
{
    t_menu		menu;
    int			action;
    const char	*items[6];
    uint64_t	frame_start;
    uint64_t	frame_ms;
    int			sleep_ms;
    int         done;
	static int	side_scroll = 0;
    
    done = 0;
    
    if (tm_ensure_logs_dir() != 0)
    {
        /* pas bloquant */
    }
	ui_ensure_globals_csv();
    
    items[0] = "Demarrer LIVE (globals)";
    items[1] = "Demarrer REPLAY (globals)";
    items[2] = "Arreter le parser globals";
    items[3] = "Dashboard LIVE";
    items[4] = "Vider CSV globals (confirmation YES)";
    items[5] = "Retour";
    
    menu_init(&menu, items, 6);
    /* Utilise la fenetre existante (mode fenetre unique) */
    while (w && w->running && !done)
    {
        t_ui_state	ui;
        t_ui_layout	ly;
        t_rect		content;
        t_rect		list_r;
        int			sidebar_item_h;
		int			sw;

        frame_start = ft_time_ms();
        window_poll_events(w);
		overlay_tick_auto_hunt();

		ui.theme = &g_theme_dark;
		sw = ui_sidebar_width_for_labels(items, 6, 14, UI_PAD);
		ui_calc_layout_ex(w, &ly, sw);
        sidebar_item_h = 40;
        list_r = (t_rect){ly.sidebar.x, ly.sidebar.y + UI_PAD,
            ly.sidebar.w, ly.sidebar.h - UI_PAD * 2};
        /* Keyboard navigation (mouse uses ui_list_scroll below) */
        action = -1;
        if (w->key_up)
            menu.selected--;
        if (w->key_down)
            menu.selected++;
        if (menu.selected < 0)
            menu.selected = 0;
        if (menu.selected > menu.count - 1)
            menu.selected = menu.count - 1;
        if (w->key_enter)
            action = menu.selected;
        if (w->key_escape)
            action = menu.count - 1;

		content = ui_draw_chrome_ex(w, &ui, "Globals", 
            globals_thread_is_running() ? "Parser: OK" : "Parser: STOP",
			"\x18\x19 naviguer   Enter ouvrir   Echap retour", sw);

		/* Sidebar (scrollable for small screens) */
		{
			int	clicked;

			clicked = ui_list_scroll(w, &ui, list_r, items, 6, &menu.selected,
				sidebar_item_h, 0, &side_scroll);
			if (clicked >= 0)
				action = clicked;
		}

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
					ui_screen_message(w, "PARSER GLOBALS", msg, 1);
				}
			}
			else if (action == 3)
				screen_dashboard_globals_live(w);
			else if (action == 4)
				ui_action_clear_globals_csv(w);
			else
				done = 1;
		}

        /* Content */
        ui_draw_text(w, content.x + UI_PAD, content.y + UI_PAD,
            "Menu Globals", ui.theme->text);
        ui_draw_text(w, content.x + UI_PAD, content.y + UI_PAD + 22,
            "Live parser, dashboard, maintenance CSV.", ui.theme->text2);
        window_present(w);
        
        frame_ms = ft_time_ms() - frame_start;
        sleep_ms = 16 - (int)frame_ms;
        if (sleep_ms > 0)
            ft_sleep_ms(sleep_ms);
    }
    /* Reset ESC pour ne pas quitter le menu principal immediatement */
    if (w)
        w->key_escape = 0;
}
