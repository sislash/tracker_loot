/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   mob_prompt.c                                       :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: login <login@student.42.fr>                +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/02/12 00:00:00 by login             #+#    #+#             */
/*   Updated: 2026/02/12 00:00:00 by you              ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "mob_prompt.h"

#include "mob_selected.h"
#include "core_paths.h"

#include "ui_chrome.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "ui_utils.h"

#include "utils.h"

#include <string.h>
#include <stdio.h>


static void	draw_invalid_border(t_window *w, t_rect r, unsigned int color)
{
	/* 1px border overlay */
	window_fill_rect(w, r.x, r.y, r.w, 1, (int)color);
	window_fill_rect(w, r.x, r.y + r.h - 1, r.w, 1, (int)color);
	window_fill_rect(w, r.x, r.y, 1, r.h, (int)color);
	window_fill_rect(w, r.x + r.w - 1, r.y, 1, r.h, (int)color);
}

static int	mob_is_valid(const char *s)
{
	if (!s)
		return (0);
	while (*s)
	{
		if (*s != ' ' && *s != '\t')
			return (1);
		s++;
	}
	return (0);
}

static int	mob_prompt_modal(t_window *w, char *io, size_t cap)
{
	t_ui_state	ui;
	t_rect		content;
	t_rect		panel;
	t_rect		in;
	t_rect		b_ok;
	t_rect		b_cancel;
	int			focus_id;
	int			done;
	int			ok;
	int			changed;
	unsigned int warn;
	unsigned int err;

	if (!w || !io || cap == 0)
		return (0);
	ui.theme = &g_theme_dark;
	focus_id = 1;
	done = 0;
	ok = 0;
	warn = 0xFFE8C15A;
	err = 0xFFE35D5D;

	while (w->running && !done)
	{
		window_poll_events(w);
		if (w->key_escape)
		{
			ok = 0;
			done = 1;
		}
		content = ui_draw_chrome_ex(w, &ui,
			"Chasse > Mob", "Nom du mob obligatoire", "Enter: Valider   Esc: Annuler", 0);

		/* Centered panel */
		panel.w = (content.w < 560) ? (content.w - UI_PAD * 2) : 540;
		if (panel.w < 360)
			panel.w = content.w - UI_PAD * 2;
		panel.h = 170;
		if (panel.h > content.h - UI_PAD * 2)
			panel.h = content.h - UI_PAD * 2;
		panel.x = content.x + (content.w - panel.w) / 2;
		panel.y = content.y + (content.h - panel.h) / 2;
		ui_draw_panel(w, panel, ui.theme->surface2, ui.theme->border);

		ui_draw_text(w, panel.x + 16, panel.y + 18, "Nom du mob (cible) *", ui.theme->text);
		ui_draw_text(w, panel.x + 16, panel.y + 40, "Ex: Atrox, Argonaut, Longu...", ui.theme->text2);

		in = (t_rect){panel.x + 16, panel.y + 68, panel.w - 32, 28};
		changed = ui_input_text(w, &ui, in, io, (int)cap, &focus_id, 1);
		(void)changed;

		/* Validate on Enter */
		if (w->key_enter)
		{
			char tmp[128];
			snprintf(tmp, sizeof(tmp), "%s", io);
			mob_selected_sanitize(tmp);
			if (mob_is_valid(tmp))
			{
				snprintf(io, cap, "%s", tmp);
				ok = 1;
				done = 1;
			}
		}

		/* Buttons */
		b_ok = (t_rect){panel.x + 16, panel.y + panel.h - 44, (panel.w - 32) / 2 - 6, 30};
		b_cancel = (t_rect){b_ok.x + b_ok.w + 12, b_ok.y, b_ok.w, b_ok.h};
		{
			char tmp[128];
			snprintf(tmp, sizeof(tmp), "%s", io);
			mob_selected_sanitize(tmp);
			if (ui_button(w, &ui, b_ok, "Valider", UI_BTN_PRIMARY, mob_is_valid(tmp)))
			{
				snprintf(io, cap, "%s", tmp);
				ok = 1;
				done = 1;
			}
			if (ui_button(w, &ui, b_cancel, "Annuler", UI_BTN_SECONDARY, 1))
			{
				ok = 0;
				done = 1;
			}
			if (!mob_is_valid(tmp))
			{
				ui_draw_text(w, panel.x + 16, b_ok.y - 18,
					"* Champ obligatoire (non vide).", warn);
				/* small invalid border */
				draw_invalid_border(w, in, err);
			}
		}

		window_present(w);
		ui_sleep_ms(16);
	}
	return (ok);
}

int	mob_prompt_ensure(t_window *w, char *out, size_t outsz)
{
	char	mob[128];

	if (out && outsz)
		out[0] = '\0';
	if (!w)
		return (0);
	mob[0] = '\0';
	(void)mob_selected_load(tm_path_mob_selected(), mob, sizeof(mob));
	mob_selected_sanitize(mob);
	if (mob_is_valid(mob))
	{
		if (out && outsz)
			snprintf(out, outsz, "%s", mob);
		return (1);
	}

	/* Ask user */
	mob[0] = '\0';
	if (!mob_prompt_modal(w, mob, sizeof(mob)))
		return (0);
	mob_selected_save(tm_path_mob_selected(), mob);
	if (out && outsz)
		snprintf(out, outsz, "%s", mob);
	return (1);
}
