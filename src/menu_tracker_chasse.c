/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   menu_tracker_chasse.c                              :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: login <login@student.42.fr>                +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/01/31 00:00:00 by login             #+#    #+#             */
/*   Updated: 2026/02/05 00:00:00 by you              ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "menu_tracker_chasse.h"

#include "menu_principale.h" /* t_menu + menu_update/menu_render_screen */
#include "window.h"
#include "utils.h"
#include "frame_limiter.h"
#include "ui_utils.h"

/* UI pro (meme style partout) */
#include "ui_chrome.h"
#include "ui_layout.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#include "hunt_series.h"
#include "ui_graph.h"

#include "screen_graph_live.h"
#include "hunt_series_live.h"
#include "tracker_stats_live.h"

#include "core_paths.h"
#include "config_arme.h"
#include "parser_thread.h"
#include "session.h"
#include "tracker_stats.h"
#include "weapon_selected.h"
#include "mob_selected.h"
#include "mob_prompt.h"
#include "overlay.h"
#include "sweat_option.h"
#include "session_export.h"
#include "tm_money.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

static long	money_to_pec(tm_money_t ped)
{
	return ((long)tm_money_to_pec_round(ped));
}

/* shared window helpers moved to ui_utils.c (ui_draw_lines_clipped, ui_screen_message)
 * and utils.c (tm_fmt_linef).
 */

/* ************************************************************************** */
/*  Pretty formatting helpers (aligned columns)                               */
/* ************************************************************************** */

static void	fmt_kv_aligned(char *dst, size_t cap,
						   const char *label, const char *value,
						   int label_w, int value_w)
{
	char	lab[256];
	char	val[256];
	int		lw;
	int		vw;
	
	if (!dst || cap == 0)
		return ;
	if (!label)
		label = "";
	if (!value)
		value = "";
	snprintf(lab, sizeof(lab), "%s", label);
	snprintf(val, sizeof(val), "%s", value);
	lw = (label_w <= 0) ? 24 : label_w;
	vw = (value_w <= 0) ? 20 : value_w;
	/* Left label + right-aligned value */
	snprintf(dst, cap, "%-*.*s  %*.*s", lw, lw, lab, vw, vw, val);
}

static void	fmt_ped(char *dst, size_t cap, tm_money_t ped)
{
	char num[32];

	tm_money_format_ped4(num, sizeof(num), ped);
	/* fixed width number with 4 decimals */
	snprintf(dst, cap, "%10s PED", num);
}

static void	fmt_pct(char *dst, size_t cap, double pct)
{
	snprintf(dst, cap, "%8.2f %%", pct);
}

static void	fmt_i64(char *dst, size_t cap, long v)
{
	snprintf(dst, cap, "%ld", v);
}

static void	fmt_ratio2(char *dst, size_t cap, double v)
{
	snprintf(dst, cap, "%8.2f", v);
}

static void	fmt_ratio6(char *dst, size_t cap, double v)
{
	snprintf(dst, cap, "%10.6f", v);
}

static void	fmt_sep(char *dst, size_t cap)
{
	/* visually stable separator for monospace font */
	snprintf(dst, cap, "------------------------------------------------------------");
}

/* ************************************************************************** */
/*  Weapons helpers                                                           */
/* ************************************************************************** */

static int	load_db(armes_db *db)
{
	if (!db)
		return (-1);
	memset(db, 0, sizeof(*db));
	if (!armes_db_load(db, tm_path_armes_ini()))
		return (-1);
	return (0);
}

static int	load_selected_weapon(char *selected, size_t size)
{
	if (!selected || size == 0)
		return (0);
	selected[0] = '\0';
	if (weapon_selected_load(tm_path_weapon_selected(), selected, size) != 0)
		return (0);
	return (selected[0] != '\0');
}

static void	weapon_build_lines(const arme_stats *w, char out[16][256], int *n)
{
	int	k;
	char	buf[64];

	if (!n)
		return ;
	*n = 0;
	if (!w)
		return ;
	k = 0;
	snprintf(out[k++], sizeof(out[0]), "ARME ACTIVE");
	tm_fmt_linef(out[k++], sizeof(out[0]), "Nom", "%s", w->name);
	tm_fmt_linef(out[k++], sizeof(out[0]), "DPP", "%.4f", w->dpp);
	tm_money_format_ped4(buf, sizeof(buf), w->ammo_shot);
	tm_fmt_linef(out[k++], sizeof(out[0]), "Ammo / tir", "%s PED", buf);
	tm_money_format_ped4(buf, sizeof(buf), w->decay_shot);
	tm_fmt_linef(out[k++], sizeof(out[0]), "Decay / tir", "%s PED", buf);
	tm_money_format_ped4(buf, sizeof(buf), w->amp_decay_shot);
	tm_fmt_linef(out[k++], sizeof(out[0]), "Amp decay / tir", "%s PED", buf);
	tm_money_format_ped4(buf, sizeof(buf), (tm_money_t)w->markup_mu_1e4);
	tm_fmt_linef(out[k++], sizeof(out[0]), "Legacy MU", "%s", buf);
	tm_money_format_ped4(buf, sizeof(buf), (tm_money_t)w->ammo_mu_1e4);
	tm_fmt_linef(out[k++], sizeof(out[0]), "Ammo MU", "%s", buf);
	tm_money_format_ped4(buf, sizeof(buf), (tm_money_t)w->weapon_mu_1e4);
	tm_fmt_linef(out[k++], sizeof(out[0]), "Weapon MU", "%s", buf);
	tm_money_format_ped4(buf, sizeof(buf), (tm_money_t)w->amp_mu_1e4);
	tm_fmt_linef(out[k++], sizeof(out[0]), "Amp MU", "%s", buf);
	tm_money_format_ped4(buf, sizeof(buf), arme_cost_shot_uPED(w));
	tm_fmt_linef(out[k++], sizeof(out[0]), "Cout / tir total", "%s PED", buf);
	if (w->notes[0])
		tm_fmt_linef(out[k++], sizeof(out[0]), "Notes", "%s", w->notes);
	*n = k;
}

/* ************************************************************************** */
/*  Hunt stats pages (simple version fenetre)                                 */
/* ************************************************************************** */

typedef enum e_dash_page
{
	DASH_RESUME = 0,
	DASH_LOOT,
	DASH_EXPENSES,
	DASH_RESULTS,
	DASH_COUNT
}	t_dash_page;

static double	safe_div(double a, double b)
{
	if (b == 0.0)
		return (0.0);
	return (a / b);
}

static double	ratio_pct(tm_money_t num, tm_money_t den)
{
	if (den <= 0)
		return (0.0);
	return (((double)num / (double)den) * 100.0);
}

static void	fill_common_header(char lines[32][256], int *n, long offset)
{
	int	k;
	
	char	v[256];
	
	k = 0;
	
	snprintf(lines[k++], sizeof(lines[0]), "=== DASHBOARD CHASSE (fenetre) ===");
	snprintf(lines[k++], sizeof(lines[0]), "Navigation: [Q] page precedente   [D] page suivante   [Echap] quitter");
	
	lines[k++][0] = '\0';
	
	fmt_sep(lines[k++], sizeof(lines[0]));
	snprintf(v, sizeof(v), "%s", parser_thread_is_running() ? "EN COURS" : "ARRETE");
	fmt_kv_aligned(lines[k++], sizeof(lines[0]), "Parser", v, 18, 38);
	snprintf(v, sizeof(v), "%ld ligne(s)", offset);
	fmt_kv_aligned(lines[k++], sizeof(lines[0]), "Offset session", v, 18, 38);
	/* CSV path can be long: keep aligned label + value, but show full path */
	snprintf(v, sizeof(v), "%s", tm_path_hunt_csv());
	fmt_kv_aligned(lines[k++], sizeof(lines[0]), "CSV", v, 18, 38);
	fmt_sep(lines[k++], sizeof(lines[0]));
	lines[k++][0] = '\0';
	*n = k;
}

static void	dash_page_resume(const t_hunt_stats *s, char lines[32][256], int *n)
{
	int	k;
	char	v[128];
	
	k = *n;
	snprintf(lines[k++], sizeof(lines[0]), "[PAGE 1/%d] RESUME", DASH_COUNT);
	lines[k++][0] = '\0';
	/* Stats */
	fmt_sep(lines[k++], sizeof(lines[0]));
	snprintf(v, sizeof(v), "%ld / %ld", s->kills, s->shots);
	fmt_kv_aligned(lines[k++], sizeof(lines[0]), "Kills / Shots", v, 24, 24);
	{
		char num[32];
		tm_money_format_ped4(num, sizeof(num), s->loot_ped);
		snprintf(v, sizeof(v), "%10s PED  (%ld PEC)", num, money_to_pec(s->loot_ped));
	}
	fmt_kv_aligned(lines[k++], sizeof(lines[0]), "Loot total", v, 24, 24);
	{
		char num[32];
		tm_money_format_ped4(num, sizeof(num), s->expense_used);
		snprintf(v, sizeof(v), "%10s PED  (%ld PEC)", num, money_to_pec(s->expense_used));
	}
	fmt_kv_aligned(lines[k++], sizeof(lines[0]), "Depense utilisee", v, 24, 24);
	{
		char num[32];
		tm_money_format_ped4(num, sizeof(num), s->net_ped);
		snprintf(v, sizeof(v), "%10s PED  (%ld PEC)", num, money_to_pec(s->net_ped));
	}
	fmt_kv_aligned(lines[k++], sizeof(lines[0]), "Net (profit/perte)", v, 24, 24);
	lines[k++][0] = '\0';
	/* Ratios */
	fmt_pct(v, sizeof(v), ratio_pct(s->loot_ped, s->expense_used));
	fmt_kv_aligned(lines[k++], sizeof(lines[0]), "Return", v, 24, 24);
	#ifdef TM_STATS_HAS_MARKUP
	/* Return en incluant MU (si dispo) */
	fmt_pct(v, sizeof(v), ratio_pct(s->loot_total_mu_ped, s->expense_used));
	fmt_kv_aligned(lines[k++], sizeof(lines[0]), "Return (TT+MU)", v, 24, 24);
	#endif
	fmt_pct(v, sizeof(v), ratio_pct(s->net_ped, s->expense_used));
	fmt_kv_aligned(lines[k++], sizeof(lines[0]), "Profit", v, 24, 24);
	#ifdef TM_STATS_HAS_MARKUP
	/* Profit en incluant MU */
	fmt_pct(v, sizeof(v), ratio_pct(s->loot_total_mu_ped - s->expense_used, s->expense_used));
	fmt_kv_aligned(lines[k++], sizeof(lines[0]), "Profit (TT+MU)", v, 24, 24);
	#endif
	fmt_ratio2(v, sizeof(v), safe_div((double)s->shots, (double)s->kills));
	fmt_kv_aligned(lines[k++], sizeof(lines[0]), "Shots / kill", v, 24, 24);
	fmt_sep(lines[k++], sizeof(lines[0]));
	*n = k;
}

static void	dash_page_loot(const t_hunt_stats *s, char lines[32][256], int *n)
{
	int	k;
	char	v[128];
	char	tmp[64];
	
	k = *n;
	snprintf(lines[k++], sizeof(lines[0]), "[PAGE 2/%d] LOOT", DASH_COUNT);
	lines[k++][0] = '\0';
	fmt_sep(lines[k++], sizeof(lines[0]));
	/* Totaux TT / MU */
	#ifdef TM_STATS_HAS_MARKUP
	fmt_ped(v, sizeof(v), s->loot_tt_ped);
	fmt_kv_aligned(lines[k++], sizeof(lines[0]), "Loot TT", v, 24, 24);
	fmt_ped(v, sizeof(v), s->loot_mu_ped);
	fmt_kv_aligned(lines[k++], sizeof(lines[0]), "Loot MU", v, 24, 24);
	fmt_ped(v, sizeof(v), s->loot_total_mu_ped);
	fmt_kv_aligned(lines[k++], sizeof(lines[0]), "Loot TT+MU", v, 24, 24);
	lines[k++][0] = '\0';
	fmt_pct(v, sizeof(v), ratio_pct(s->loot_tt_ped, s->expense_used));
	fmt_kv_aligned(lines[k++], sizeof(lines[0]), "Return TT", v, 24, 24);
	fmt_pct(v, sizeof(v), ratio_pct(s->loot_total_mu_ped, s->expense_used));
	fmt_kv_aligned(lines[k++], sizeof(lines[0]), "Return TT+MU", v, 24, 24);
	lines[k++][0] = '\0';
	#endif
	fmt_ratio6(v, sizeof(v), safe_div(tm_money_to_ped_double(s->loot_ped), (double)s->shots));
	fmt_kv_aligned(lines[k++], sizeof(lines[0]), "Loot / shot", v, 24, 24);
	snprintf(v, sizeof(v), "%10.4f", safe_div(tm_money_to_ped_double(s->loot_ped), (double)s->kills));
	fmt_kv_aligned(lines[k++], sizeof(lines[0]), "Loot / kill", v, 24, 24);
	fmt_i64(v, sizeof(v), s->loot_events);
	fmt_kv_aligned(lines[k++], sizeof(lines[0]), "Loot events", v, 24, 24);
	fmt_i64(v, sizeof(v), s->sweat_events);
	fmt_kv_aligned(lines[k++], sizeof(lines[0]), "Sweat events", v, 24, 24);
	fmt_i64(v, sizeof(v), s->sweat_total);
	fmt_kv_aligned(lines[k++], sizeof(lines[0]), "Sweat total", v, 24, 24);
	lines[k++][0] = '\0';
	/* Top loot */
	if (s->top_loot_count > 0)
	{
		size_t	i;
		snprintf(lines[k++], sizeof(lines[0]), "Top loot (TT+MU):");
		snprintf(lines[k++], sizeof(lines[0]), "  %-22s | %10s | %10s | %10s | %5s", "Item", "TT", "MU", "Total", "Evts");
		snprintf(lines[k++], sizeof(lines[0]), "  %-22s-+-%10s-+-%10s-+-%10s-+-%5s", "----------------------", "----------", "----------", "----------", "-----");
		i = 0;
		while (i < s->top_loot_count && k < 32)
		{
			/* Keep names readable + aligned numeric columns */
			/* Avoid -Wformat-truncation: clamp copy with precision */
			snprintf(tmp, sizeof(tmp), "%.63s", s->top_loot[i].name);
			/* truncate long names for stable columns */
			tmp[22] = '\0';
				{
					char tt_str[32];
					char mu_str[32];
					char mu_fmt[32];
					char tot_str[32];

					tm_money_format_ped4(tt_str, sizeof(tt_str), s->top_loot[i].tt_ped);
					tm_money_format_ped4(mu_str, sizeof(mu_str), s->top_loot[i].mu_ped);
					tm_money_format_ped4(tot_str, sizeof(tot_str), s->top_loot[i].total_mu_ped);
					snprintf(mu_fmt, sizeof(mu_fmt), "%s%s", (s->top_loot[i].mu_ped >= 0) ? "+" : "", mu_str);
					snprintf(lines[k++], sizeof(lines[0]), "  %-22s | %10s | %10s | %10s | %5ld",
							 tmp, tt_str, mu_fmt, tot_str, s->top_loot[i].events);
				}
			i++;
		}
		lines[k++][0] = '\0';
	}
	fmt_sep(lines[k++], sizeof(lines[0]));
	*n = k;
}

static void	dash_page_expenses(const t_hunt_stats *s, char lines[32][256], int *n)
{
	int	k;
	char	v[128];
	
	k = *n;
	snprintf(lines[k++], sizeof(lines[0]), "[PAGE 3/%d] DEPENSES", DASH_COUNT);
	lines[k++][0] = '\0';
	fmt_sep(lines[k++], sizeof(lines[0]));
	fmt_ped(v, sizeof(v), s->expense_ped_logged);
	fmt_kv_aligned(lines[k++], sizeof(lines[0]), "Depenses (CSV)", v, 24, 24);
	fmt_ped(v, sizeof(v), s->expense_ped_calc);
	fmt_kv_aligned(lines[k++], sizeof(lines[0]), "Depenses (modele)", v, 24, 24);
	{
		char ped[32];
		tm_money_format_ped4(ped, sizeof(ped), s->cost_shot_uPED);
		snprintf(v, sizeof(v), "%s PED", ped);
	}
	fmt_kv_aligned(lines[k++], sizeof(lines[0]), "Cout / shot", v, 24, 24);
	snprintf(v, sizeof(v), "%s", s->expense_used_is_logged ? "log CSV" : "modele arme");
	fmt_kv_aligned(lines[k++], sizeof(lines[0]), "Source depense", v, 24, 24);
	fmt_sep(lines[k++], sizeof(lines[0]));
	*n = k;
}

static void	dash_page_results(const t_hunt_stats *s, char lines[32][256], int *n)
{
	int	k;
	char	v[128];
	
	k = *n;
	snprintf(lines[k++], sizeof(lines[0]), "[PAGE 4/%d] RESULTATS", DASH_COUNT);
	lines[k++][0] = '\0';
	fmt_sep(lines[k++], sizeof(lines[0]));
	fmt_ratio6(v, sizeof(v), safe_div(tm_money_to_ped_double(s->net_ped), (double)s->shots));
	fmt_kv_aligned(lines[k++], sizeof(lines[0]), "Net / shot", v, 24, 24);
	#ifdef TM_STATS_HAS_MARKUP
	/* Net en TT+MU */
	fmt_ratio6(v, sizeof(v), safe_div(tm_money_to_ped_double(s->loot_total_mu_ped - s->expense_used), (double)s->shots));
	fmt_kv_aligned(lines[k++], sizeof(lines[0]), "Net(TT+MU)/shot", v, 24, 24);
	#endif
	snprintf(v, sizeof(v), "%10.4f", safe_div(tm_money_to_ped_double(s->net_ped), (double)s->kills));
	fmt_kv_aligned(lines[k++], sizeof(lines[0]), "Net / kill", v, 24, 24);
	#ifdef TM_STATS_HAS_MARKUP
	snprintf(v, sizeof(v), "%10.4f", safe_div(tm_money_to_ped_double(s->loot_total_mu_ped - s->expense_used), (double)s->kills));
	fmt_kv_aligned(lines[k++], sizeof(lines[0]), "Net(TT+MU)/kill", v, 24, 24);
	#endif
	snprintf(v, sizeof(v), "%ld / %ld", s->loot_events, s->expense_events);
	fmt_kv_aligned(lines[k++], sizeof(lines[0]), "Loot/Expense events", v, 24, 24);
	fmt_sep(lines[k++], sizeof(lines[0]));
	*n = k;
}

static void	dash_build_lines(const t_hunt_stats *s, long offset, t_dash_page page,
							 char lines[32][256], int *n)
{
	fill_common_header(lines, n, offset);
	if (page == DASH_RESUME)
		dash_page_resume(s, lines, n);
	else if (page == DASH_LOOT)
		dash_page_loot(s, lines, n);
	else if (page == DASH_EXPENSES)
		dash_page_expenses(s, lines, n);
	else
		dash_page_results(s, lines, n);
}

/* ************************************************************************** */
/*  Window screens                                                            */
/* ************************************************************************** */

static void	screen_weapon_active(t_window *w)
{
	armes_db				db;
	char					selected[128];
	const arme_stats		*wstat;
	char					buf[16][256];
	const char				*lines[16];
	int					n;
	int					i;
	
	if (!load_selected_weapon(selected, sizeof(selected)))
	{
		const char *msg[] = {
			"Aucune arme active.",
			"Va dans 'Choisir une arme active'."
		};
		ui_screen_message(w, "ARME ACTIVE", msg, 2);
		return ;
	}
	if (load_db(&db) != 0)
	{
		const char *msg[] = { "[ERREUR] Impossible de charger armes.ini", tm_path_armes_ini() };
		ui_screen_message(w, "ARME ACTIVE", msg, 2);
		return ;
	}
	wstat = armes_db_find(&db, selected);
	if (!wstat)
	{
		const char *msg[] = {
			"[WARNING] Arme active introuvable.",
			"Verifie le nom exact dans armes.ini."
		};
		armes_db_free(&db);
		ui_screen_message(w, "ARME ACTIVE", msg, 2);
		return ;
	}
	weapon_build_lines(wstat, buf, &n);
	i = 0;
	while (i < n)
	{
		lines[i] = buf[i];
		i++;
	}
	armes_db_free(&db);
	ui_screen_message(w, "ARME ACTIVE", lines, n);
}

static void	screen_weapon_choose(t_window *w)
{
	armes_db			db;
	const char		**items;
	t_menu			m;
	int				action;
	int				i;
	
	if (load_db(&db) != 0)
	{
		const char *msg[] = { "[ERREUR] Impossible de charger armes.ini", tm_path_armes_ini() };
		ui_screen_message(w, "CHOISIR UNE ARME", msg, 2);
		return ;
	}
	if (db.count == 0)
	{
		const char *msg[] = { "Aucune arme dans armes.ini", tm_path_armes_ini() };
		armes_db_free(&db);
		ui_screen_message(w, "CHOISIR UNE ARME", msg, 2);
		return ;
	}
	items = (const char **)calloc(db.count + 1, sizeof(char *));
	if (!items)
	{
		armes_db_free(&db);
		return ;
	}
	i = 0;
	while ((size_t)i < db.count)
	{
		items[i] = db.items[i].name;
		i++;
	}
	/* Bouton retour (au lieu d'un simple Echap) */
	items[i] = "Retour";
	menu_init(&m, items, (int)db.count + 1);
	static int	side_scroll = 0;
	static int	right_scroll = 0;
	while (w->running)
	{
		t_ui_state	ui;
		t_ui_layout	ly;
		t_rect		content;
		t_rect		list_r;
		int		clicked;
		int		sw;
		
		ui.theme = &g_theme_dark;
		window_poll_events(w);
		if (w->key_escape)
			break ;
		/* Keyboard navigation (mouse uses ui_list_scroll) */
		action = -1;
		if (w->key_up)
			m.selected--;
		if (w->key_down)
			m.selected++;
		if (m.selected < 0)
			m.selected = 0;
		if (m.selected > m.count - 1)
			m.selected = m.count - 1;
		if (w->key_enter)
			action = m.selected;
		if (w->key_escape)
			action = m.count - 1;
		if (action == m.count - 1)
			break ;
		/* rendu themed + sidebar auto-width */
		sw = ui_sidebar_width_for_labels(items, (int)db.count + 1, 14, UI_PAD);
		ui_calc_layout_ex(w, &ly, sw);
		content = ui_draw_chrome_ex(w, &ui, "Chasse / Armes", NULL,
									"\x18\x19 naviguer   Enter choisir   Echap retour", sw);
		list_r = (t_rect){ly.sidebar.x, ly.sidebar.y + UI_PAD,
			ly.sidebar.w, ly.sidebar.h - UI_PAD * 2};
			/* souris + scroll sidebar (petits ecrans) */
			clicked = ui_list_scroll(w, &ui, list_r, items, (int)db.count + 1,
									 &m.selected, 40, 0, &side_scroll);
			if (clicked >= 0)
			{
				action = clicked;
				if ((size_t)action == db.count)
					break ;
				m.selected = clicked;
			}
			/* Keep coords for legacy hit-test inside menu_update() */
			m.render_x = list_r.x;
			m.render_y = list_r.y;
			m.item_w = list_r.w;
			m.item_h = 40;
			
			
			/* Zone contenu: details arme selectionnee */
			ui_draw_text(w, content.x + UI_PAD, content.y + UI_PAD,
						 "Arme selectionnee", ui.theme->text);
			ui_draw_text(w, content.x + UI_PAD, content.y + UI_PAD + 22,
						 "(La liste est a gauche - molette pour defiler)", ui.theme->text2);
			{
				t_rect p;
				char wbuf[16][256];
				const char *wlines[16];
				int wn;
				
				p = (t_rect){content.x + UI_PAD,
					content.y + UI_PAD + 56,
					content.w - UI_PAD * 2,
					content.h - (UI_PAD + 56) - UI_PAD};
					ui_draw_panel(w, p, ui.theme->surface, ui.theme->border);
					wn = 0;
					if (m.selected >= 0 && (size_t)m.selected < db.count)
						weapon_build_lines(&db.items[m.selected], wbuf, &wn);
				else
				{
					snprintf(wbuf[0], sizeof(wbuf[0]), "(Retour)");
					wn = 1;
				}
				for (int ii = 0; ii < wn; ii++)
					wlines[ii] = wbuf[ii];
				ui_text_lines_scroll(w, &ui, (t_rect){p.x, p.y, p.w, p.h},
									 wlines, wn, 14, ui.theme->text2, &right_scroll);
				
				/* Confirm button */
				{
					t_rect br;
					int enabled = (m.selected >= 0 && (size_t)m.selected < db.count);
					br = (t_rect){p.x + p.w - 220, p.y + p.h - 44, 200, 32};
					if (ui_button(w, &ui, br, "Confirmer", UI_BTN_PRIMARY, enabled))
					{
						if (weapon_selected_save(tm_path_weapon_selected(), db.items[m.selected].name) == 0)
						{
							char	line[256];
							const char *msg[2];
							snprintf(line, sizeof(line), "OK : Arme active = %s", db.items[m.selected].name);
							msg[0] = line;
							msg[1] = "(Echap pour revenir)";
							ui_screen_message(w, "CHOISIR UNE ARME", msg, 2);
						}
						break ;
					}
				}
			}
			overlay_tick_auto_hunt();
		window_present(w);
			ui_sleep_ms(16);
	}
	free(items);
	armes_db_free(&db);
}

static void	screen_stats_once(t_window *w)
{
	long			offset;
	const t_hunt_stats	*ps;
	t_hunt_stats	s;
	char			buf[32][256];
	const char		*lines[32];
	int				n;
	int				i;
	
	offset = session_load_offset(tm_path_session_offset());
	tracker_stats_live_tick();
	ps = tracker_stats_live_get();
	memset(&s, 0, sizeof(s));
	if (!ps)
	{
		const char *msg[] = { "Aucun CSV trouve.", tm_path_hunt_csv(), "Lance le parser LIVE/REPLAY." };
		ui_screen_message(w, "STATS", msg, 3);
		return ;
	}
	s = *ps;
	n = 0;
	fill_common_header(buf, &n, offset);
	buf[n++][0] = '\0';
	tm_fmt_linef(buf[n++], sizeof(buf[0]), "Kills / Shots", "%ld / %ld", s.kills, s.shots);
	{
		char num[32];
		tm_money_format_ped4(num, sizeof(num), s.loot_ped);
		tm_fmt_linef(buf[n++], sizeof(buf[0]), "Loot total", "%s PED (%ld PEC)", num, money_to_pec(s.loot_ped));
		tm_money_format_ped4(num, sizeof(num), s.expense_used);
		tm_fmt_linef(buf[n++], sizeof(buf[0]), "Depense utilisee", "%s PED (%ld PEC)", num, money_to_pec(s.expense_used));
		tm_money_format_ped4(num, sizeof(num), s.net_ped);
		tm_fmt_linef(buf[n++], sizeof(buf[0]), "Net", "%s PED (%ld PEC)", num, money_to_pec(s.net_ped));
	}
	tm_fmt_linef(buf[n++], sizeof(buf[0]), "Return", "%.2f %%", ratio_pct(s.loot_ped, s.expense_used));
	i = 0;
	while (i < n)
	{
		lines[i] = buf[i];
		i++;
	}
	ui_screen_message(w, "STATS (resume)", lines, n);
}

static void	screen_dashboard_live(t_window *w)
{
	t_ui_state		ui;
	t_ui_layout		ly;
	t_rect			content;
	t_rect			list_r;
	
	const char		*tabs[] = {"Resume", "Loot", "Depenses", "Resultats", "Retour"};
	int			selected;
	int			clicked;
	
	t_dash_page		page;
	long			offset;
	const t_hunt_stats	*ps;
	t_hunt_stats		s;
	char			buf[32][256];
	const char		*lines[32];
	int			n;
	int			i;
	int			max_y;
	int			scroll_y;
	int			sw;
	
	t_frame_limiter	fl;
	uint64_t		last_ms;
	uint64_t		now_ms;
	double			dt;
	double			refresh_acc;
	int			cache_ok;
	int			cache_n;
	
	ui.theme = &g_theme_dark;
	page = DASH_RESUME;
	selected = (int)page;
	fl.target_ms = 16;
	last_ms = ft_time_ms();
	refresh_acc = 1.0;
	cache_ok = 0;
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
		
		/* Navigation clavier */
		if (w->key_escape)
			break ;
		if (w->key_up && selected > 0)
			selected--;
		if (w->key_down && selected < 5)
			selected++;
		if (w->key_q)
		{
			page = (t_dash_page)((page - 1 + DASH_COUNT) % DASH_COUNT);
			selected = (int)page;
			refresh_acc = 1.0;
		}
		else if (w->key_d)
		{
			page = (t_dash_page)((page + 1) % DASH_COUNT);
			selected = (int)page;
			refresh_acc = 1.0;
		}
		if (w->key_enter)
		{
			if (selected == 4)
				break ;
			page = (t_dash_page)selected;
			refresh_acc = 1.0;
		}
		
		/* Cache refresh */
		refresh_acc += dt;
		if (refresh_acc >= 0.100)
		{
			refresh_acc = 0.0;
			offset = session_load_offset(tm_path_session_offset());
			tracker_stats_live_tick();
			ps = tracker_stats_live_get();
			memset(&s, 0, sizeof(s));
			if (!ps)
			{
				cache_ok = 0;
				cache_n = 0;
			}
			else
			{
				s = *ps;
				n = 0;
				dash_build_lines(&s, offset, page, buf, &n);
				cache_ok = 1;
				cache_n = n;
			}
		}
		
		/* Rendu pro (meme chrome que le menu principal) */
		sw = 0;
		{
			int i;
			int maxw;
			maxw = 0;
			i = 0;
			while (i < 5)
			{
				int tw = ui_measure_text_w(tabs[i], 14);
				if (tw > maxw)
					maxw = tw;
				i++;
			}
			sw = maxw + 4 + (UI_PAD * 2) + 24;
		}
		ui_calc_layout_ex(w, &ly, sw);
		content = ui_draw_chrome_ex(w, &ui,
									"Session / Live", parser_thread_is_running() ? "Parser: OK" : "Parser: STOP",
									"Q/D page  |  molette scroll  |  \\x18\\x19 menu  |  Enter valider  |  Echap retour", sw);
		list_r = (t_rect){ly.sidebar.x, ly.sidebar.y + UI_PAD,
			ly.sidebar.w, ly.sidebar.h - UI_PAD * 2};
			clicked = ui_list(w, &ui, list_r, tabs, 6, &selected, 40, 0);
			if (clicked >= 0)
			{
				selected = clicked;
				if (clicked == 4)
					break ;
				page = (t_dash_page)clicked;
				refresh_acc = 1.0;
			}
			
			/* Contenu */
			ui_draw_panel(w, content, ui.theme->bg, ui.theme->border);
			max_y = content.y + content.h - UI_PAD;
			if (max_y < 0)
				max_y = 0x7FFFFFFF;
		/* Content scrolling (mouse wheel) */
		if (w->mouse_x >= content.x && w->mouse_x < content.x + content.w
			&& w->mouse_y >= content.y && w->mouse_y < content.y + content.h)
		{
			int lines_h = (cache_ok ? cache_n : 3) * 14;
			ui_scroll_update(w, &scroll_y, lines_h + UI_PAD * 2,
							 content.h, 24);
		}
		if (!cache_ok)
		{
			const char *msg[] = {"Aucun CSV trouve.", tm_path_hunt_csv(),
				"Lance le parser LIVE/REPLAY."};
				ui_draw_lines_clipped(w, content.x + UI_PAD,
									  content.y + UI_PAD - scroll_y,
						  msg, 3, 14, ui.theme->text2, max_y);
		}
		else
		{
			i = 0;
			while (i < cache_n)
			{
				lines[i] = buf[i];
				i++;
			}
			ui_draw_lines_clipped(w, content.x + UI_PAD,
								  content.y + UI_PAD - scroll_y,
						 lines, cache_n, 14, ui.theme->text2, max_y);
		}
		overlay_tick_auto_hunt();
		window_present(w);
		fl_end_sleep(&fl);
	}
}

static void	fmt_elapsed(char *dst, size_t cap, double sec)
{
	int	h;
	int	m;
	int	s;

	if (!dst || cap == 0)
		return ;
	if (sec < 0.0)
		sec = 0.0;
	h = (int)(sec / 3600.0);
	sec -= (double)h * 3600.0;
	m = (int)(sec / 60.0);
	s = (int)(sec - (double)m * 60.0);
	snprintf(dst, cap, "%02d:%02d:%02d", h, m, s);
}

void	screen_graph_live(t_window *w)
{
	t_ui_state	ui;
	t_ui_layout	ly;
	t_rect		content;
	t_rect		list_r;
	/* Sidebar scroll (mouse wheel + draggable bar via ui_list_scroll). */
	static int	sidebar_scroll = 0;

	/*
	** Tabs du graphe LIVE.
	**
	** Objectif "pro hunter": suivre l'efficacite par mob:
	**  - Tirs / kill (Shots/Kill)
	**  - Hit-rate / kill
	**  - (optionnel) Hits / kill
	*/
	const char	*tabs[] = {
		"Shots/kill",
		"Hit-rate",
		"Hits/kill",
		"Kills",
		"Loot/kill",
		"Loot cumul",
		"Cost cumul",
		"ROI %",
		"Retour"
	};
	const int	tabs_n = (int)(sizeof(tabs) / sizeof(tabs[0]));
	/*
	 * UI state persiste entre les entrees/sorties de l'ecran:
	 * - pas de reset sur "Retour"
	 */
	static int	selected = 0;
	int		clicked;

	const t_hunt_series	*hs;
	const t_hunt_stats	*st;

	static int	range_min = 60;
	/* One zoom state per tab (keep some slack for future tabs). */
	static t_ui_graph_zoom	zoom_states[12];

	t_frame_limiter	fl;
	uint64_t	last_ms;
	uint64_t	now_ms;
	double		dt;
	double		refresh_acc;

	char		buf_hits[64];
	char		buf_kills[64];
	char		buf_loot[64];
	char		buf_shots[64];
	char		buf_time[64];
	char		status_left[192];

	double		values[HS_MAX_POINTS];
	int		xsec[HS_MAX_POINTS];
	int		groupc[HS_MAX_POINTS];
	int		nplot;
	double		vmax;
	t_hs_metric	metric;
	unsigned int	line_col;
	const char	*title;
	const char	*unit;
	const char	*y_label;
	int		last_n_buckets;
	int		cumulative;

	ui.theme = &g_theme_dark;
	/* Always have an up-to-date cache (also updated outside this screen). */
	hunt_series_live_tick();
	tracker_stats_live_tick();
	hs = hunt_series_live_get();
	st = tracker_stats_live_get();

	fl.target_ms = 16;
	last_ms = ft_time_ms();
	refresh_acc = 1.0;
	while (w->running)
	{
		fl_begin(&fl);
		now_ms = ft_time_ms();
		dt = (double)(now_ms - last_ms) / 1000.0;
		last_ms = now_ms;
		if (dt < 0.0)
			dt = 0.0;
		window_poll_events(w);

		/* Exit */
		if (w->key_escape)
			break ;
		if (w->key_up && selected > 0)
			selected--;
		if (w->key_down && selected < tabs_n - 1)
			selected++;
		if (w->key_enter && selected == tabs_n - 1)
			break ;

		/* Refresh series */
		refresh_acc += dt;
		if (refresh_acc >= 0.100)
		{
			refresh_acc = 0.0;
			hunt_series_live_tick();
			tracker_stats_live_tick();
			hs = hunt_series_live_get();
			st = tracker_stats_live_get();
		}

		/* Format KPIs (safe defaults when no data) */
		if (hs && hs->initialized)
		{
			double esc;
			esc = difftime(hs->last_t ? hs->last_t : time(NULL), hs->t0);
			snprintf(buf_shots, sizeof(buf_shots), "%ld", hs->shots_total);
			snprintf(buf_hits, sizeof(buf_hits), "%ld", hs->hits_total);
			snprintf(buf_kills, sizeof(buf_kills), "%ld", hs->kills_total);
			tm_money_format_ped2(buf_loot, sizeof(buf_loot), hs->loot_total_uPED);
			fmt_elapsed(buf_time, sizeof(buf_time), esc);
		}
		else
		{
			snprintf(buf_shots, sizeof(buf_shots), "0");
			snprintf(buf_hits, sizeof(buf_hits), "0");
			snprintf(buf_kills, sizeof(buf_kills), "0");
			snprintf(buf_loot, sizeof(buf_loot), "0.00");
			snprintf(buf_time, sizeof(buf_time), "00:00:00");
		}

		/* Chrome */
		{
			int sw = ui_sidebar_width_for_labels(tabs, tabs_n, 14, UI_PAD);
			ui_calc_layout_ex(w, &ly, sw);
				{
					long rs, re_raw, re_res;
					const char *p = parser_thread_is_running() ? "OK" : "STOP";
					const char *warn = hunt_series_live_warning_text();
					if (hunt_series_live_is_range())
					{
						hunt_series_live_get_range(&rs, &re_raw, &re_res);
						if (re_raw < 0)
							snprintf(status_left, sizeof(status_left),
								"Parser: %s | View: SESSION %ld..EOF", p, rs);
						else
							snprintf(status_left, sizeof(status_left),
								"Parser: %s | View: SESSION %ld..%ld", p, rs, re_raw);
					}
					else
						snprintf(status_left, sizeof(status_left),
							"Parser: %s | View: LIVE off %ld", p,
							hunt_series_live_get_offset());
					if (warn && *warn)
					{
						size_t len = strlen(status_left);
						if (len + 3 < sizeof(status_left))
						{
							strncat(status_left, " | ", sizeof(status_left) - len - 1);
							len = strlen(status_left);
							strncat(status_left, warn, sizeof(status_left) - len - 1);
						}
					}
				}
				content = ui_draw_chrome_ex(w, &ui,
							"Session / Graph LIVE",
							status_left,
							"Echap retour  |  \x18\x19 menu  |  Molette: scroll  |  LIVE (quitter session)  |  15/30/60/Tout",
							sw);
			list_r = (t_rect){ly.sidebar.x, ly.sidebar.y + UI_PAD,
				ly.sidebar.w, ly.sidebar.h - UI_PAD * 2};
		}
	/*
	** Sidebar (Graph LIVE):
	**  - If the list OVERFLOWS (small heights): real scrolling with wheel + bar.
	**  - If the list FITS: use the wheel to move the selection (pro UX: no dead wheel).
	*/
	{
		const int item_h = 40;
		const int overflow = (tabs_n * item_h > list_r.h);

		/* Wheel tab navigation when everything is visible */
		if (!overflow && w->mouse_wheel != 0
			&& w->mouse_x >= list_r.x && w->mouse_x < list_r.x + list_r.w
			&& w->mouse_y >= list_r.y && w->mouse_y < list_r.y + list_r.h)
		{
			/* Wheel: +1 up => previous, -1 down => next */
			selected -= w->mouse_wheel;
			if (selected < 0)
				selected = 0;
			if (selected > tabs_n - 1)
				selected = tabs_n - 1;
			/* consume wheel to avoid double-processing later in the frame */
			w->mouse_wheel = 0;
		}

		if (overflow)
		{
			clicked = ui_list_scroll(w, &ui, list_r, tabs, tabs_n,
					&selected, item_h, 0, &sidebar_scroll);
			/* consume wheel (ui_list_scroll does not clear it) */
			if (w->mouse_wheel != 0
				&& w->mouse_x >= list_r.x && w->mouse_x < list_r.x + list_r.w
				&& w->mouse_y >= list_r.y && w->mouse_y < list_r.y + list_r.h)
				w->mouse_wheel = 0;
		}
		else
		{
			clicked = ui_list(w, &ui, list_r, tabs, tabs_n,
					&selected, item_h, 0);
		}
	}
		if (clicked >= 0)
		{
			selected = clicked;
			if (clicked == tabs_n - 1)
				break ;
		}

		/* Content panel (responsive + scroll for small screens) */
		{
			/*
			 * Small screens: KPI cards can push the graph out of view.
			 * We prioritize the graph:
			 *  - Make the KPI grid more compact (more columns, smaller cards).
			 *  - If still not enough, enable vertical scrolling for the right panel.
			 */
			static int	content_scroll = 0;
			/*
			 * Small screens ("petit ecran"):
			 * The zoom widget draws a mini "overview" graph only if the graph panel
			 * is tall enough (see ui_graph_timeseries_zoom_impl()).
			 *
			 * Before this fix, KPI cards could squeeze the graph area just below the
			 * overview threshold, while the right panel wouldn't switch to scrolling.
			 * Result: impossible to reach the mini-graph.
			 *
			 * Fix:
			 * - enforce a larger minimum graph height (overview always reachable)
			 * - allow the graph panel to be taller than the viewport so scrolling can
			 *   reveal the lower part (overview) instead of hard-clamping it.
			 */
			const int	min_graph_h_cfg = (content.h < 520 ? 240 : 260);
			const int	min_graph_draw_h = 140; /* below this, graph UI becomes unreadable */
			const int	cards_n = 9;
			const char	*ct[cards_n];
			const char	*cv[cards_n];
			unsigned int	cc[cards_n];
			int		gap;
			int		card_h;
			int		min_card_w;
			int		max_cols;
			int		cols;
			int		rows;
			int		card_w;
			int		header_y;
			int		cards_top;
			int		y_after_cards;
			int		graph_y;
			int		graph_h;
			int		need_scroll;
			int		content_h_needed;
			int		max_scroll;
			int		yoff;
			int		i;

			/* Fill KPI values */
			{
				static char buf_spk[64];
				static char buf_hr[64];
				static char buf_cost[64];
				static char buf_roi[64];
				double spk = 0.0;
				double hr = 0.0;
				double roi = 0.0;
				tm_money_t cost_uPED = 0;

				if (hs && hs->kills_total > 0)
					spk = (double)hs->shots_total / (double)hs->kills_total;
				if (hs && hs->shots_total > 0)
					hr = (100.0 * (double)hs->hits_total) / (double)hs->shots_total;
				if (st)
					cost_uPED = st->expense_used;
				if (cost_uPED > 0 && hs)
					roi = ((double)hs->loot_total_uPED * 100.0) / (double)cost_uPED;
				snprintf(buf_spk, sizeof(buf_spk), "%.2f", spk);
				snprintf(buf_hr, sizeof(buf_hr), "%.1f%%", hr);
				tm_money_format_ped2(buf_cost, sizeof(buf_cost), cost_uPED);
				snprintf(buf_roi, sizeof(buf_roi), "%.1f%%", roi);

				ct[0] = "Tirs";      cv[0] = buf_shots; cc[0] = ui.theme->text;
				ct[1] = "Hits";      cv[1] = buf_hits;  cc[1] = ui.theme->accent;
				ct[2] = "Hit-rate";  cv[2] = buf_hr;    cc[2] = ui.theme->accent;
				ct[3] = "Kills";     cv[3] = buf_kills; cc[3] = ui.theme->success;
				ct[4] = "Loot";      cv[4] = buf_loot;  cc[4] = ui.theme->warn;
				ct[5] = "Shots/Kill";cv[5] = buf_spk;   cc[5] = ui.theme->text;
				ct[6] = "Temps";     cv[6] = buf_time;  cc[6] = ui.theme->text;
				ct[7] = "Co\xC3\xBBt";     cv[7] = buf_cost;  cc[7] = ui.theme->success;
				ct[8] = "ROI";      cv[8] = buf_roi;   cc[8] = ui.theme->success;
			}

			/* Responsive grid computation (favor fewer rows on small heights). */
			gap = UI_PAD;
			card_h = (content.h < 520 ? 52 : 60);
			min_card_w = (content.h < 520 ? 78 : 92);
			if (min_card_w < 60)
				min_card_w = 60;
			max_cols = (content.w - UI_PAD * 2 + gap) / (min_card_w + gap);
			if (max_cols < 2)
				max_cols = 2;
			if (max_cols > cards_n)
				max_cols = cards_n;

			/* Base choice from width (keeps readability on large screens) */
			if (content.w >= 980)
				cols = 5;
			else if (content.w >= 720)
				cols = 3;
			else
				cols = 2;
			if (cols > max_cols)
				cols = max_cols;

			header_y = content.y + UI_PAD;
			cards_top = content.y + UI_PAD + 44;

			/* If height is tight, increase columns to reduce rows until the graph fits. */
			for (i = 0; i < 8; ++i)
			{
				rows = (cards_n + cols - 1) / cols;
				y_after_cards = cards_top + rows * (card_h + gap);
				graph_y = y_after_cards + 42;
				graph_h = content.h - (graph_y - content.y) - UI_PAD;
				if (graph_h >= min_graph_h_cfg || cols >= max_cols)
					break;
				cols++;
			}
			rows = (cards_n + cols - 1) / cols;
			y_after_cards = cards_top + rows * (card_h + gap);
			graph_y = y_after_cards + 42;
			graph_h = content.h - (graph_y - content.y) - UI_PAD;

			/* If still too small, enforce a minimum graph height and scroll the panel. */
			need_scroll = 0;
			content_h_needed = content.h;
			if (graph_h < min_graph_h_cfg)
			{
				need_scroll = 1;
				graph_h = min_graph_h_cfg;
				if (graph_h < min_graph_draw_h)
					graph_h = min_graph_draw_h;
				content_h_needed = (graph_y - content.y) + graph_h + UI_PAD;
			}

			/* Update scroll offset (wheel) when hovering the content area. */
			if (!need_scroll)
			{
				content_scroll = 0;
			}
			else
			{
				max_scroll = content_h_needed - content.h;
				if (max_scroll < 0)
					max_scroll = 0;
				if (w->mouse_wheel != 0
					&& w->mouse_x >= content.x && w->mouse_x < content.x + content.w
					&& w->mouse_y >= content.y && w->mouse_y < content.y + content.h
					&& !(w->mouse_x >= list_r.x && w->mouse_x < list_r.x + list_r.w
						&& w->mouse_y >= list_r.y && w->mouse_y < list_r.y + list_r.h))
				{
					ui_scroll_update(w, &content_scroll, content_h_needed, content.h, 32);
					/* Consume wheel so it doesn't leak to other widgets in the same frame. */
					w->mouse_wheel = 0;
				}
				if (content_scroll < 0)
					content_scroll = 0;
				if (content_scroll > max_scroll)
					content_scroll = max_scroll;
			}
			yoff = -content_scroll;
			/*
			 * Clipping policy (simple + portable):
			 * We don't have a backend scissor in the window abstraction.
			 * So we ensure scrollable widgets never draw outside the right panel by
			 * skipping draws when the whole widget box is not fully inside the panel.
			 */
			{
				/* no-op scope, keeps the comment close to the checks below */
			}

			/* Right panel background */
			ui_draw_panel(w, content, ui.theme->bg, ui.theme->border);

			/* Back button (also available in sidebar + Echap) */
			{
				t_rect back;
				back = (t_rect){content.x + content.w - UI_PAD - 110,
					content.y + UI_PAD + yoff,
					110, 32};
				if (back.y >= content.y && back.y + back.h <= content.y + content.h)
				{
					if (ui_button(w, &ui, back, "Retour", UI_BTN_GHOST, 1))
						break;
				}
			}

			/* Header */
			{
				t_rect hr;
				hr = (t_rect){content.x + UI_PAD, header_y + yoff,
					content.w - UI_PAD * 2 - 120, 36};
				if (hr.y >= content.y && hr.y + hr.h <= content.y + content.h)
					ui_section_header(w, &ui, hr, "Graph temps reel");
			}

			/* Draw KPI cards */
			card_w = (content.w - UI_PAD * 2 - gap * (cols - 1)) / cols;
			if (card_w < 60)
				card_w = 60;
			for (i = 0; i < cards_n; ++i)
			{
				int row = i / cols;
				int col = i % cols;
				t_rect r;
				r = (t_rect){content.x + UI_PAD + col * (card_w + gap),
					cards_top + row * (card_h + gap) + yoff,
					card_w, card_h};
				/* Avoid drawing outside the panel (no clipping support) */
				if (r.y >= content.y && r.y + r.h <= content.y + content.h)
					ui_card(w, &ui, r, ct[i], cv[i], cc[i]);
			}

			/* Range buttons + mode toggle */
			{
				t_rect b;
				int bx;
				int by;
				int bw;
				int bh;

				by = y_after_cards + yoff;
				bx = content.x + UI_PAD;
				bw = 72;
				bh = 30;
				if (by >= content.y && by + bh <= content.y + content.h)
				{
					if (hunt_series_live_is_range())
					{
						b = (t_rect){bx, by, 96, bh};
						if (ui_button(w, &ui, b, "LIVE", UI_BTN_PRIMARY, 1))
						{
							session_clear_range(tm_path_session_range());
							hunt_series_live_force_reset();
							hunt_series_live_tick();
							hs = hunt_series_live_get();
						}
						bx += b.w + 10;
					}
					b = (t_rect){bx, by, bw, bh};
					if (ui_button(w, &ui, b, "15m",
						range_min == 15 ? UI_BTN_PRIMARY : UI_BTN_SECONDARY, 1))
				{
					range_min = 15;
					ui_graph_zoom_reset(&zoom_states[selected]);
					if (hs)
					{
						int end = (int)hunt_series_elapsed_seconds(hs);
						zoom_states[selected].t1 = end;
						zoom_states[selected].t0 = end - 15 * 60;
						if (zoom_states[selected].t0 < 0)
							zoom_states[selected].t0 = 0;
					}
				}
					b.x += bw + 8;
					if (ui_button(w, &ui, b, "30m",
						range_min == 30 ? UI_BTN_PRIMARY : UI_BTN_SECONDARY, 1))
				{
					range_min = 30;
					ui_graph_zoom_reset(&zoom_states[selected]);
					if (hs)
					{
						int end = (int)hunt_series_elapsed_seconds(hs);
						zoom_states[selected].t1 = end;
						zoom_states[selected].t0 = end - 30 * 60;
						if (zoom_states[selected].t0 < 0)
							zoom_states[selected].t0 = 0;
					}
				}
					b.x += bw + 8;
					if (ui_button(w, &ui, b, "60m",
						range_min == 60 ? UI_BTN_PRIMARY : UI_BTN_SECONDARY, 1))
				{
					range_min = 60;
					ui_graph_zoom_reset(&zoom_states[selected]);
					if (hs)
					{
						int end = (int)hunt_series_elapsed_seconds(hs);
						zoom_states[selected].t1 = end;
						zoom_states[selected].t0 = end - 60 * 60;
						if (zoom_states[selected].t0 < 0)
							zoom_states[selected].t0 = 0;
					}
				}
					b.x += bw + 8;
					if (ui_button(w, &ui, b, "Tout",
						range_min == 0 ? UI_BTN_PRIMARY : UI_BTN_SECONDARY, 1))
				{
					range_min = 0;
					ui_graph_zoom_reset(&zoom_states[selected]);
				}
				}
			}

			/* Graph panel */
			{
				t_rect gr;
				int top_clip;
				int bottom_clip;

				gr = (t_rect){content.x + UI_PAD,
					graph_y + yoff,
					content.w - UI_PAD * 2,
					graph_h};

				/* Clamp graph rect inside the panel (no true scissor available) */
				top_clip = content.y + UI_PAD;
				bottom_clip = content.y + content.h - UI_PAD;
				if (gr.y < top_clip)
				{
					int d = top_clip - gr.y;
					gr.y += d;
					gr.h -= d;
				}
				if (gr.y + gr.h > bottom_clip)
					gr.h = bottom_clip - gr.y;

				/*
				 * UX FIX (pro hunter):
				 * When the sidebar selection is on the "Retour" item, we must NOT
				 * render any graph/plot.
				 * Otherwise, the user sees a chart while being on a navigation action,
				 * which feels like a UI bug.
				 */
				if (selected == tabs_n - 1)
				{
					ui_draw_panel(w, gr, ui.theme->surface2, ui.theme->border);
					/* Keep it minimal: no plot, just a hint. */
					ui_draw_text(w, gr.x + 12, gr.y + 12,
						"(Retour) Clique ou Enter pour quitter le graphe.",
						ui.theme->text2);
				}
				else if (gr.h < min_graph_draw_h)
				{
					const char *msg[] = {"Fenetre trop petite pour afficher le graphe.",
						"Agrandis la hauteur ou utilise la molette pour scroller."};
					ui_draw_panel(w, (t_rect){content.x + UI_PAD, content.y + UI_PAD, content.w - UI_PAD * 2, content.h - UI_PAD * 2},
						ui.theme->surface2, ui.theme->border);
					ui_text_lines_scroll(w, &ui,
						(t_rect){content.x + UI_PAD, content.y + UI_PAD, content.w - UI_PAD * 2, content.h - UI_PAD * 2},
						msg, 2, 14, ui.theme->text2, NULL);
				}
				else
				{
					/* Choose metric based on tab */
					metric = HS_METRIC_SHOTS;
					cumulative = 0;
					title = "Shots / kill";
					unit = "";
					y_label = "Shots";
					line_col = ui.theme->text;
					if (selected == 1)
					{
						metric = HS_METRIC_HITS;
						cumulative = 0;
						title = "Hit-rate / kill";
						unit = " %";
						y_label = "Hit-rate";
						line_col = ui.theme->accent;
					}
					else if (selected == 2)
					{
						metric = HS_METRIC_HITS;
						cumulative = 0;
						title = "Hits / kill";
						unit = "";
						y_label = "Hits";
						line_col = ui.theme->accent;
					}
					else if (selected == 3)
					{
						metric = HS_METRIC_KILLS;
						cumulative = 0;
						title = "Kills (cumul)";
						unit = "";
						y_label = "Kills";
						line_col = ui.theme->success;
					}
					else if (selected == 4)
					{
						metric = HS_METRIC_LOOT_PED;
						cumulative = 0;
						title = "Loot / kill";
						unit = " PED";
						y_label = "Valeur";
						line_col = ui.theme->warn;
					}
					else if (selected == 5)
					{
						metric = HS_METRIC_LOOT_PED;
						cumulative = 1;
						title = "Loot cumul";
						unit = " PED";
						y_label = "Valeur";
						line_col = ui.theme->warn;
					}
					else if (selected == 6)
					{
						metric = HS_METRIC_SHOTS;
						cumulative = 1;
						title = "Cost cumul";
						unit = " PED";
						y_label = "Co\xC3\xBBt";
						line_col = ui.theme->success;
					}
					else if (selected == 7)
					{
						metric = HS_METRIC_SHOTS;
						cumulative = 1;
						title = "ROI cumul";
						unit = " %";
						y_label = "ROI";
						line_col = ui.theme->success;
					}

					/*
					** Zoom/Overview UX:
					** Always build the FULL series; range buttons drive the zoom window.
					*/
					last_n_buckets = 0;
					nplot = 0;
					vmax = 0.0;
					if (hs)
					{
						if (selected == 0)
							hunt_series_build_shots_events(hs, 0, values, xsec, &nplot, &vmax);
						else if (selected == 1)
							hunt_series_build_hit_rate_events(hs, 0, values, xsec, &nplot, &vmax);
						else if (selected == 2)
							hunt_series_build_hits_events(hs, 0, values, xsec, &nplot, &vmax);
						else if (selected == 3)
							hunt_series_build_kill_events(hs, 0, values, xsec, &nplot, &vmax);
						else if (selected == 4)
							hunt_series_build_loot_events_ex(hs, 0, 0, values, xsec, groupc, &nplot, &vmax);
						else if (selected == 5)
							hunt_series_build_loot_events(hs, 0, 1, values, xsec, &nplot, &vmax);
						else if (selected == 6)
						{
							tm_money_t cs = (st ? st->cost_shot_uPED : 0);
							if (hs->expense_total_uPED != 0 || cs > 0)
								hunt_series_build_cost_cumulative(hs, 0, cs, values, xsec, &nplot, &vmax);
							else
								nplot = 0;
						}
						else if (selected == 7)
						{
							tm_money_t cs = (st ? st->cost_shot_uPED : 0);
							if (hs->expense_total_uPED != 0 || cs > 0)
								hunt_series_build_roi_cumulative(hs, 0, cs, values, xsec, &nplot, &vmax);
							else
								nplot = 0;
						}
						else
							hunt_series_build_plot(hs, last_n_buckets, metric, cumulative, values, xsec, &nplot, &vmax);
					}

					if (nplot <= 0)
					{
						const char *msg[] = {"Aucune donnee pour cette session.",
							"Lance le parser LIVE ou REPLAY."};
						const char *msg_cost[] = {"Cout/ROI indisponible.",
							"Selectionne une arme (Weapon) ou log AMMO/DECAY/REPAIR dans le CSV."};
						ui_draw_panel(w, gr, ui.theme->surface2, ui.theme->border);
						if (selected == 6 || selected == 7)
							ui_text_lines_scroll(w, &ui, gr, msg_cost, 2, 14, ui.theme->text2, NULL);
						else
							ui_text_lines_scroll(w, &ui, gr, msg, 2, 14, ui.theme->text2, NULL);
					}
					else
					{
						/* Build point annotations (kill/loot labels). */
						enum { ANN_MAX = HS_MAX_POINTS };
						static t_ui_graph_annot ann[ANN_MAX];
						static char ann_text[ANN_MAX][48];
						int ann_n = 0;
						int i0;
						int j;
						int max_points;

						/*
						 * Pro hunter UX (requested): small labels on ALL points for:
						 *  - Shots/kill
						 *  - Hit-rate
						 *  - Hits/kill
						 * Keep texts compact to avoid clutter.
						 */
						max_points = nplot;
						if (selected == 0 || selected == 1 || selected == 2
							|| selected == 6 || selected == 7)
						{
							/* Avoid clutter on long sessions: label last N points. */
							if (selected == 6 || selected == 7)
								j = (max_points > 60) ? (max_points - 60) : 0;
							else
								j = (max_points > 200) ? (max_points - 200) : 0;
							while (j < max_points && ann_n < ANN_MAX)
							{
								if (selected == 1 || selected == 7)
									snprintf(ann_text[ann_n], sizeof(ann_text[ann_n]), "%.0f%%", values[j]);
								else if (selected == 6)
									snprintf(ann_text[ann_n], sizeof(ann_text[ann_n]), "%.2f", values[j]);
								else
									snprintf(ann_text[ann_n], sizeof(ann_text[ann_n]), "%d", (int)(values[j] + 0.5));
								ann[ann_n].point_index = j;
								ann[ann_n].kind = UI_GRAPH_ANNOT_VALUE;
								ann[ann_n].text = ann_text[ann_n];
								ann_n++;
								j++;
							}
						}

						if (selected == 3)
						{
							i0 = 0;
							j = i0;
							while (j < nplot && ann_n < ANN_MAX)
							{
								/*
								 * UI consistency (Graph LIVE): keep point tags identical across tabs.
								 * Prefixes/symbols (K/L/Sigma) were making Kills/Loot tags look
								 * different from the other graphs, and also impacted width estimates.
								 */
								snprintf(ann_text[ann_n], sizeof(ann_text[ann_n]),
									"%d", (int)(values[j] + 0.5));
								ann[ann_n].point_index = j;
								ann[ann_n].kind = UI_GRAPH_ANNOT_VALUE;
								ann[ann_n].text = ann_text[ann_n];
								ann_n++;
								j++;
							}
						}
						else if (selected == 4)
						{
							i0 = 0;
							j = i0;
							while (j < nplot && ann_n < ANN_MAX)
							{
								if (groupc[j] > 1)
									snprintf(ann_text[ann_n], sizeof(ann_text[ann_n]),
										"%.2f x%d", values[j], groupc[j]);
								else
									snprintf(ann_text[ann_n], sizeof(ann_text[ann_n]),
										"%.2f", values[j]);
								ann[ann_n].point_index = j;
								ann[ann_n].kind = UI_GRAPH_ANNOT_VALUE;
								ann[ann_n].text = ann_text[ann_n];
								ann_n++;
								j++;
							}
						}
						else if (selected == 5)
						{
							i0 = 0;
							j = i0;
							while (j < nplot && ann_n < ANN_MAX)
							{
								/* Keep money readable and consistent with other money graphs (2 decimals). */
								snprintf(ann_text[ann_n], sizeof(ann_text[ann_n]),
									"%.2f", values[j]);
								ann[ann_n].point_index = j;
								ann[ann_n].kind = UI_GRAPH_ANNOT_VALUE;
								ann[ann_n].text = ann_text[ann_n];
								ann_n++;
								j++;
							}
						}

						if (selected == 4)
							ui_graph_timeseries_zoom_badges_annotations(w, &ui, gr, title,
								values, xsec, groupc, nplot,
								(vmax <= 0.0 ? 1.0 : vmax * 1.10),
								y_label, unit, line_col,
								(ann_n > 0 ? ann : NULL), ann_n,
								&zoom_states[selected], hs ? hs->version : 0);
						else if (selected == 0 || selected == 1 || selected == 2
							|| selected == 3 || selected == 5
							|| selected == 6 || selected == 7)
							ui_graph_timeseries_zoom_annotations(w, &ui, gr, title,
								values, xsec, nplot,
								(vmax <= 0.0 ? 1.0 : vmax * 1.10),
								y_label, unit, line_col,
								(ann_n > 0 ? ann : NULL), ann_n,
								&zoom_states[selected], hs ? hs->version : 0);
						else
							ui_graph_timeseries_zoom(w, &ui, gr, title,
								values, xsec, nplot,
								(vmax <= 0.0 ? 1.0 : vmax * 1.10),
								y_label, unit, line_col,
								&zoom_states[selected], hs ? hs->version : 0);
					}
				}
			}
		}

			overlay_tick_auto_hunt();
			window_present(w);
		fl_end_sleep(&fl);
	}
}

/* ************************************************************************** */
/*  Actions                                                                   */
/* ************************************************************************** */

static int	export_compute(t_hunt_stats *s, long start_off, long *end_off,
						   char *ts_start, char *ts_end)
{
	*end_off = session_count_data_lines(tm_path_hunt_csv());
	if (tracker_stats_compute_range(tm_path_hunt_csv(), start_off, *end_off, s) != 0)
		return (0);
	session_extract_range_timestamps_ex(tm_path_hunt_csv(), start_off, *end_off,
								 ts_start, 64, ts_end, 64);
	return (1);
}

static void	action_stop_and_export(t_window *w)
{
	t_hunt_stats	export_stats;
	long		start_off;
	long		end_off;
	char		ts_start[64];
	char		ts_end[64];
	char		l1[256];
	char		l2[256];
	char		l3[256];
	const char	*msg[4];
	
	parser_thread_stop();
	start_off = session_load_offset(tm_path_session_offset());
	memset(&export_stats, 0, sizeof(export_stats));
	if (export_compute(&export_stats, start_off, &end_off, ts_start, ts_end))
	{
		if (session_export_stats_csv_ex(tm_path_sessions_stats_csv(),
			&export_stats, ts_start, ts_end, start_off, end_off))
			snprintf(l1, sizeof(l1), "OK : session exportee dans %s", tm_path_sessions_stats_csv());
		else
			snprintf(l1, sizeof(l1), "[WARN] export session CSV impossible.");
		session_save_offset(tm_path_session_offset(), end_off);
		/* Force la serie LIVE a re-demarrer sur le nouvel offset. */
		hunt_series_live_force_reset();
			/* Nouveau: on termine une session => le mob doit etre re-saisi au prochain Start. */
			mob_selected_clear(tm_path_mob_selected());
		snprintf(l2, sizeof(l2), "OK : Offset session = %ld ligne(s)", end_off);
		snprintf(l3, sizeof(l3), "Range: %s -> %s", ts_start, ts_end);
		msg[0] = l1;
		msg[1] = l2;
		msg[2] = l3;
		msg[3] = "(Echap pour revenir)";
		ui_screen_message(w, "STOP + EXPORT", msg, 4);
	}
	else
	{
		const char *err[] = { "[ERROR] cannot compute stats for export.", "(Verifie le CSV + l'offset)" };
		ui_screen_message(w, "STOP + EXPORT", err, 2);
	}
}

static void	action_reload_armes(t_window *w)
{
	armes_db	db;
	char		line1[256];
	char		line2[256];
	char		line3[256];
	const char	*msg[3];
	
	if (load_db(&db) != 0)
	{
		const char *err[] = { "[ERREUR] Impossible de charger armes.ini", tm_path_armes_ini() };
		ui_screen_message(w, "RECHARGER ARMES", err, 2);
		return ;
	}
	snprintf(line1, sizeof(line1), "OK : %zu arme(s) chargee(s)", db.count);
	snprintf(line2, sizeof(line2), "Depuis : %s", tm_path_armes_ini());
	if (db.player_name[0])
		snprintf(line3, sizeof(line3), "Joueur : %s", db.player_name);
	else
		line3[0] = '\0';
	msg[0] = line1;
	msg[1] = line2;
	msg[2] = line3;
	armes_db_free(&db);
	ui_screen_message(w, "RECHARGER ARMES", msg, 3);
}

static void	action_toggle_sweat(t_window *w)
{
	int		enabled;
	char		line[128];
	const char	*msg[2];
	
	enabled = 0;
	sweat_option_load(tm_path_options_cfg(), &enabled);
	enabled = !enabled;
	if (sweat_option_save(tm_path_options_cfg(), enabled) != 0)
	{
		const char *err[] = { "[ERREUR] impossible d'ecrire options.cfg", tm_path_options_cfg() };
		ui_screen_message(w, "SWEAT", err, 2);
		return ;
	}
	snprintf(line, sizeof(line), "Tracker Sweat : %s", enabled ? "ON" : "OFF");
	msg[0] = line;
	msg[1] = "(Echap pour revenir)";
	ui_screen_message(w, "SWEAT", msg, 2);
}

static void	action_set_offset_to_end(t_window *w)
{
	long		offset;
	char		line[256];
	const char	*msg[2];
	
	offset = session_count_data_lines(tm_path_hunt_csv());
	session_save_offset(tm_path_session_offset(), offset);
	/* Force la serie LIVE a se recaler immediatement sur ce nouvel offset. */
	hunt_series_live_force_reset();
	snprintf(line, sizeof(line), "OK : Offset session = %ld ligne(s) (fin CSV)", offset);
	msg[0] = line;
	msg[1] = "(Echap pour revenir)";
	ui_screen_message(w, "OFFSET", msg, 2);
}

/* ************************************************************************** */
/*  Entry (mode fenetre)                                                      */
/* ************************************************************************** */

void	menu_tracker_chasse(t_window *w)
{
	t_menu		menu;
	int			action;
	const char	*items[11];
	uint64_t	frame_start;
	static uint64_t	session_start_ms = 0;
	static uint64_t	last_stats_ms = 0;
	static uint64_t	last_series_ms = 0;
	static t_hunt_stats	cached_stats;
	static int		cached_stats_ok = 0;
	uint64_t	frame_ms;
	int			sleep_ms;
	
	if (!w)
		return ;
	
	/* Items menu (dernier = retour) */
	items[0] = "Demarrer LIVE (parser chasse)";
	items[1] = "Demarrer REPLAY (parser chasse)";
	items[2] = "Arreter le parser + Export session + Offset";
	items[3] = "Recharger armes.ini";
	items[4] = "Choisir une arme active";
	items[5] = "Afficher l'arme active";
	items[6] = "Afficher les stats (resume)";
	items[7] = "Definir offset = fin actuelle du CSV";
	items[8] = "Overlay ON/OFF";
	items[9] = "Activer/Desactiver tracker Sweat";
	items[10] = "Retour";
	
	menu_init(&menu, items, 11);
	
	/* Loop "modal" dans la meme fenetre */
	static int	side_scroll = 0;
	while (w->running)
	{
		t_ui_state	ui;
		t_ui_layout	ly;
		t_rect		content;
		t_rect		list_r;
		int			sidebar_item_h;
		int			sw;
		
		ui.theme = &g_theme_dark;
		sw = ui_sidebar_width_for_labels(items, 11, 14, UI_PAD);
		ui_calc_layout_ex(w, &ly, sw);
		sidebar_item_h = 40;
		list_r = (t_rect){ly.sidebar.x, ly.sidebar.y + UI_PAD,
			ly.sidebar.w, ly.sidebar.h - UI_PAD * 2};
			/* hitbox souris pour menu_update() */
			menu.render_x = list_r.x;
			menu.render_y = list_r.y;
			menu.item_w = list_r.w;
			menu.item_h = sidebar_item_h;
			
			frame_start = ft_time_ms();
			window_poll_events(w);
			
			/* Keyboard navigation (menu_update() doesn't know about scroll) */
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
		
		/* Escape = menu_update renvoie dernier item => Retour */
		if (action == menu.count - 1)
			break ;
		
		/* Render (meme chrome/style que menu principal) */
		content = ui_draw_chrome_ex(w, &ui, "Chasse",
									parser_thread_is_running() ? "Parser: OK" : "Parser: STOP",
									"\x18\x19 naviguer   Enter ouvrir   Echap retour   O: overlay", sw);
		{
			int clicked;
			clicked = ui_list_scroll(w, &ui, list_r, items, 11, &menu.selected,
									 sidebar_item_h, 0, &side_scroll);
			if (clicked >= 0)
				action = clicked;
		}
		
		/*
		 * * "Retour" : le test doit etre fait APRES le hit-testing souris.
		 ** Sinon, un clic sur "Retour" ne sort pas du menu.
		 */
		if (action == menu.count - 1)
			break ;
		
		/* Execute action after UI hit-testing so clicks match visible items */
		if (action >= 0)
		{
			if (action == 0)
			{
				char	mob[128];
				const char	*msg[] = {
					"[ERREUR] Le parser LIVE n'a pas pu demarrer.",
					"Ca arrive si chat.log est introuvable ou si logs/ n'est pas accessible.",
					"Regarde: logs/parser_debug.log (paths + errno)"
				};

				/* MOB obligatoire avant de demarrer une session */
				mob[0] = '\0';
				if (mob_prompt_ensure(w, mob, sizeof(mob)))
				{
					int rc = parser_thread_start_live();
					/*
					 * rc != 0 : impossible de creer (ou relancer) le thread.
					 * running == 0 juste apres : le moteur a quitte immediatement.
					 */
					if (rc != 0 || !parser_thread_is_running())
						ui_screen_message(w, "PARSER LIVE", msg, 3);
					else if (session_start_ms == 0)
						session_start_ms = ft_time_ms();
				}
			}
			else if (action == 1)
			{
				char	mob[128];
				const char	*msg[] = {
					"[ERREUR] Le parser REPLAY n'a pas pu demarrer.",
					"Regarde: logs/parser_debug.log (paths + errno)",
					"Astuce: configure le chemin chat.log via ENTROPIA_CHATLOG si besoin."
				};

				mob[0] = '\0';
				if (mob_prompt_ensure(w, mob, sizeof(mob)))
				{
					int rc = parser_thread_start_replay();
					if (rc != 0 || !parser_thread_is_running())
						ui_screen_message(w, "PARSER REPLAY", msg, 3);
					else if (session_start_ms == 0)
						session_start_ms = ft_time_ms();
				}
			}
			else if (action == 2)
			{
				action_stop_and_export(w);
				/* Reset timer + cached stats so the next run starts a fresh session. */
				session_start_ms = 0;
				last_stats_ms = 0;
				memset(&cached_stats, 0, sizeof(cached_stats));
				cached_stats_ok = 0;
			}
			else if (action == 3)
				action_reload_armes(w);
			else if (action == 4)
				screen_weapon_choose(w);
			else if (action == 5)
				screen_weapon_active(w);
			else if (action == 6)
				screen_stats_once(w);
			else if (action == 7)
				action_set_offset_to_end(w);
			else if (action == 8)
			{
				overlay_toggle();
				if (overlay_is_enabled() && session_start_ms == 0)
					session_start_ms = ft_time_ms();
			}
			else if (action == 9)
				action_toggle_sweat(w);
		}
		ui_draw_text(w, content.x + UI_PAD, content.y + UI_PAD,
					 "Menu Chasse", ui.theme->text);
		ui_draw_text(w, content.x + UI_PAD, content.y + UI_PAD + 22,
					 "Parser, armes, stats, offset, overlay, sweat.", ui.theme->text2);
		window_present(w);
		
		/* Hotkey: O toggles overlay */
		if (w->key_o)
		{
			overlay_toggle();
			if (overlay_is_enabled() && session_start_ms == 0)
				session_start_ms = ft_time_ms();
		}
		/* Legacy dashboard: hidden hotkey (debug/profiling). */
		if (w->key_tab)
			screen_dashboard_live(w);
		/* Keep overlay session clock in sync (overlay can also drive the clock). */
		overlay_sync_session_clock(&session_start_ms);

		/* Overlay tick (stats + time) */
		if (overlay_is_enabled())
		{
			uint64_t now;
			unsigned long long elapsed;
			long offset;
			
			now = ft_time_ms();
			elapsed = 0ULL;
			if (session_start_ms != 0)
				elapsed = (unsigned long long)(now - session_start_ms);
			/* Throttle heavy CSV parsing (prevents menu lag) */
			if (now - last_stats_ms >= 250)
			{
				const t_hunt_stats *ps;
				offset = session_load_offset(tm_path_session_offset());
				(void)offset;
				tracker_stats_live_tick();
				ps = tracker_stats_live_get();
				memset(&cached_stats, 0, sizeof(cached_stats));
				if (ps)
				{
					cached_stats = *ps;
					cached_stats_ok = 1;
				}
				else
					cached_stats_ok = 0;
				last_stats_ms = now;
			}
			overlay_tick(cached_stats_ok ? &cached_stats : NULL, elapsed);
			/* If overlay buttons changed the session clock, pull it back. */
			overlay_sync_session_clock(&session_start_ms);
		}

		/*
		 * Live series tick (Graph LIVE)
		 * - garde le cache a jour meme si on n'est pas dans l'ecran graphe
		 * - evite un "reset" visuel quand on ressort/re-rentre
		 */
		{
			uint64_t now;

			now = ft_time_ms();
			if (now - last_series_ms >= 100)
			{
				/* inutile de marteler le disque si rien ne tourne */
				if (parser_thread_is_running())
					hunt_series_live_tick();
				last_series_ms = now;
			}
		}
		
		frame_ms = ft_time_ms() - frame_start;
		sleep_ms = 16 - (int)frame_ms;
		if (sleep_ms > 0)
			ft_sleep_ms(sleep_ms);
	}
}
