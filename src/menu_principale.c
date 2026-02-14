/* ************************************************************************** */
/*                                                                            */
/*                                                                            */
/*   menu.c                                                                   */
/*                                                                            */
/*   By: you <you@student.42.fr>                                              */
/*                                                                            */
/*   Created: 2026/02/04 00:00:00 by you                                      */
/*   Updated: 2026/02/05 00:00:00 by you                                      */
/*                                                                            */
/* ************************************************************************** */

#include "menu_principale.h"
#include "parser_thread.h"
#include "globals_thread.h"
#include "ui_utils.h"
#include "ui_chrome.h"
#include "ui_layout.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/*
 * ASCII ART du menu (version fenetre)
 *
 * window_draw_text ne gere qu'une seule ligne, donc on dessine l'art
 * ligne par ligne.
 */

static void	draw_lines(t_window *w, int x, int y, const char **lines, int n,
		unsigned int color)
{
    int	i;
    int	line_h;
    
    if (!w || !lines || n <= 0)
        return ;
    line_h = 14;
    i = 0;
    while (i < n)
    {
        if (lines[i])
            window_draw_text(w, x, y + i * line_h, lines[i], color);
        i++;
    }
}

static void	draw_menu_art_top(t_window *w, int x, int y, unsigned int color)
{
    const char	*lines[] = {
        "|---------------------------------------------------------------------------|",
        "|       ####### ###    ## ######## ####### ####### ####### ## #######       |",
        "|       ##      ## #   ##    ##    ##   ## ##   ## ##   ## ## ##   ##       |",
        "|       #####   ##  #  ##    ##    ####### ##   ## ####### ## #######       |",
        "|       ##      ##   # ##    ##    ## ##   ##   ## ##      ## ##   ##       |",
        "|       ####### ##    ###    ##    ##   ## ####### ##      ## ##   ##       |",
        "|                                                                           |",
        "|                 #  # ##   # # #   # #### #### #### ####                   |",
        "|                 #  # # #  # # #   # #    #  # #    #                      |",
        "|=====            #  # # ## # # #   # ###  #### #### ###               =====|",
        "|                 #  # #  # # #  # #  #    # #     # #                      |",
        "|                 #### #   ## #   #   #### #  # #### ####                   |",
        "|---------------------------------------------------------------------------|",
    };
    
    draw_lines(w, x, y, lines, (int)(sizeof(lines) / sizeof(lines[0])), color);
}

static int	sidebar_width_for_items(const char **items, int count)
{
	int i;
	int maxw;
	int tw;

	maxw = 0;
	i = 0;
	while (items && i < count)
	{
		tw = ui_measure_text_w(items[i], 14);
		if (tw > maxw)
			maxw = tw;
		i++;
	}
	/* 4px selection bar + inner padding + a little safety. */
	return (maxw + 4 + (UI_PAD * 2) + 24);
}

void	stop_all_parsers(t_window *w, int x, int y)
{
    (void)w;
    (void)x;
    (void)y;

    (void)x;
    (void)y;
    
    parser_thread_stop();
    globals_thread_stop();
}
    
static int	ft_clamp(int v, int min, int max)
{
    if (v < min)
        return (min);
    if (v > max)
        return (max);
    return (v);
}

void	menu_init(t_menu *m, const char **items, int count)
{
    m->items = items;
    m->count = count;
    m->selected = 0;
	m->render_x = 0;
	m->render_y = 0;
	m->item_w = 600;
	m->item_h = 36;
}

int	menu_update(t_menu *m, t_window *w)
{
	int	idx;

    if (w->key_up)
        m->selected--;
    if (w->key_down)
        m->selected++;
    m->selected = ft_clamp(m->selected, 0, m->count - 1);
	if (w->mouse_left_click)
	{
		if (w->mouse_x >= m->render_x && w->mouse_x < m->render_x + m->item_w
			&& w->mouse_y >= m->render_y
			&& w->mouse_y < m->render_y + m->count * m->item_h)
		{
			idx = (w->mouse_y - m->render_y) / m->item_h;
			idx = ft_clamp(idx, 0, m->count - 1);
			m->selected = idx;
			return (m->selected);
		}
	}
    if (w->key_enter)
        return (m->selected);
    if (w->key_escape)
        return (m->count - 1);
    return (-1);
}

static void	draw_item(t_menu *m, t_window *w, int x, int base_y, int idx)
{
	unsigned int	bg;
	unsigned int	fg;
	unsigned int	sep;
	unsigned int	sel_bg;
	unsigned int	sel_bar;
	int			item_h;

	item_h = m->item_h;
	sep = g_theme_dark.border;
	sel_bg = ui_color_lerp(g_theme_dark.accent, g_theme_dark.bg, 170);
	sel_bar = g_theme_dark.accent;
	bg = g_theme_dark.surface;
	fg = g_theme_dark.text2;
	if (m->selected == idx)
	{
		bg = sel_bg;
		fg = g_theme_dark.text;
		window_fill_rect(w, x, base_y + idx * item_h, 4, item_h, sel_bar);
	}
	window_fill_rect(w, x, base_y + idx * item_h, m->item_w, item_h, bg);
	window_fill_rect(w, x, base_y + idx * item_h + item_h - 1, m->item_w, 1, sep);
	window_draw_text(w, x + 12, base_y + idx * item_h + (item_h / 2 - 6),
		m->items[idx], fg);
}

void	menu_render(t_menu *m, t_window *w, int x, int y)
{
    int	i;

	/* Store last render position for mouse hit-testing */
	m->render_x = x;
	m->render_y = y;
    
	if (m->item_w <= 0)
		m->item_w = 600;
	if (m->item_h <= 0)
		m->item_h = 36;

    i = 0;
    while (i < m->count)
    {
        draw_item(m, w, x, y, i);
        i++;
    }
}

void	menu_render_screen(t_menu *m, t_window *w, int x, int y)
{
	t_ui_state	ui;
	t_ui_layout	ly;
	t_rect		content;
	t_rect		list_r;
	int		sw;

	(void)x;
	(void)y;
	ui.theme = &g_theme_dark;
	sw = sidebar_width_for_items(m->items, m->count);
	ui_calc_layout_ex(w, &ly, sw);
	content = ui_draw_chrome_ex(w, &ui, "Accueil", "tracker_loot", 
		"\x18\x19 naviguer   Enter ouvrir   Esc quitter", sw);

	/* Sidebar: navigation list */
	list_r = (t_rect){ly.sidebar.x, ly.sidebar.y + UI_PAD, ly.sidebar.w,
		ly.sidebar.h - UI_PAD * 2};
	/* Keep coordinates for legacy menu_update() mouse handling */
	m->render_x = list_r.x;
	m->render_y = list_r.y;
	m->item_w = list_r.w;
	m->item_h = 40;
	ui_list(w, &ui, list_r, m->items, m->count, &m->selected, m->item_h, 0);

	/* Content: welcome */
	ui_draw_text(w, content.x + UI_PAD, content.y + UI_PAD,
		"Bienvenue", ui.theme->text);
	ui_draw_text(w, content.x + UI_PAD, content.y + UI_PAD + 22,
		"Choisissez une section dans la barre laterale.", ui.theme->text2);

	/* Optional ASCII header in content (kept, but themed) */
	draw_menu_art_top(w, content.x + UI_PAD, content.y + UI_PAD + 60,
		ui.theme->text2);
	window_present(w);
}
