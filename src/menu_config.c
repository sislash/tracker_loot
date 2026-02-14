#include "menu_config.h"

#include "config_arme.h"
#include "markup.h"
#include "core_paths.h"

#include "ui_chrome.h"
#include "ui_layout.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "overlay.h"
#include "ui_utils.h"

#include "menu_principale.h"

#include "tm_string.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

static double	parse_d(const char *s)
{
	char *end = NULL;
	double v;
	if (!s)
		return (0.0);
	v = strtod(s, &end);
	if (end == s)
		return (0.0);
	return (v);
}

static int	is_number_str(const char *s)
{
	char *end = NULL;
	if (!s)
		return (0);
	while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')
		s++;
	if (*s == '\0')
		return (0);
	(void)strtod(s, &end);
	if (end == s)
		return (0);
	while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')
		end++;
	return (*end == '\0');
}

static void	draw_invalid_border(t_window *w, t_rect r, unsigned int color)
{
	/* 1px border overlay */
	window_fill_rect(w, r.x, r.y, r.w, 1, (int)color);
	window_fill_rect(w, r.x, r.y + r.h - 1, r.w, 1, (int)color);
	window_fill_rect(w, r.x, r.y, 1, r.h, (int)color);
	window_fill_rect(w, r.x + r.w - 1, r.y, 1, r.h, (int)color);
}

static void	setf(char *buf, int bufsz, double v)
{
	snprintf(buf, bufsz, "%.10g", v);
}

static void	setm(char *buf, int bufsz, tm_money_t v)
{
	tm_money_format_ped4(buf, (size_t)bufsz, v);
}

static void	setmu(char *buf, int bufsz, int64_t mu_1e4)
{
	tm_money_format_ped4(buf, (size_t)bufsz, (tm_money_t)mu_1e4);
}

/* Parse PED string -> uPED. Accepts ',' or '.'. Returns 1 on success. */
static int	parse_money_flex(const char *s, tm_money_t *out)
{
	char	tmp[64];
	size_t	i;
	if (!out)
		return (0);
	*out = 0;
	if (!s)
		return (0);
	while (*s && isspace((unsigned char)*s))
		s++;
	if (*s == '\0')
		return (0);
	i = 0;
	while (s[i] && i + 1 < sizeof(tmp))
	{
		char c = s[i];
		if (!(c == '+' || c == '-' || c == '.' || c == ',' || (c >= '0' && c <= '9')))
			break;
		tmp[i] = (c == ',') ? '.' : c;
		i++;
	}
	tmp[i] = '\0';
	return (tm_money_parse_ped(tmp, out) != 0);
}

static tm_money_t	parse_money_or0(const char *s)
{
	tm_money_t v;
	if (!s || !s[0])
		return (0);
	if (!parse_money_flex(s, &v))
		return (0);
	return (v);
}

static int64_t	parse_mu_or_def(const char *s, int64_t def, int zero_ok)
{
	tm_money_t v;
	if (!s || !s[0])
		return (def);
	if (!parse_money_flex(s, &v))
		return (def);
	if (!zero_ok && v <= 0)
		v = (tm_money_t)def;
	if (zero_ok && v < 0)
		v = 0;
	return ((int64_t)v);
}

static int	armes_db_upsert(armes_db *db, int idx, const arme_stats *w)
{
	arme_stats *tmp;
	if (!db || !w)
		return (0);
	if (idx >= 0 && (size_t)idx < db->count)
	{
		db->items[idx] = *w;
		return (1);
	}
	tmp = (arme_stats *)realloc(db->items, (db->count + 1) * sizeof(*db->items));
	if (!tmp)
		return (0);
	db->items = tmp;
	db->items[db->count] = *w;
	db->count++;
	return (1);
}

static void	armes_db_delete(armes_db *db, int idx)
{
	if (!db || idx < 0 || (size_t)idx >= db->count)
		return;
	if (idx == (int)db->count - 1)
	{
		db->count--;
		return;
	}
	memmove(&db->items[idx], &db->items[idx + 1], (db->count - (size_t)idx - 1) * sizeof(*db->items));
	db->count--;
}

static int	markup_db_upsert(t_markup_db *db, int idx, const t_markup_rule *r)
{
	t_markup_rule *tmp;
	if (!db || !r)
		return (0);
	if (idx >= 0 && (size_t)idx < db->count)
	{
		db->items[idx] = *r;
		return (1);
	}
	if (db->count + 1 > db->cap)
	{
		size_t ncap = (db->cap == 0) ? 16 : db->cap * 2;
		tmp = (t_markup_rule *)realloc(db->items, ncap * sizeof(*db->items));
		if (!tmp)
			return (0);
		db->items = tmp;
		db->cap = ncap;
	}
	db->items[db->count++] = *r;
	return (1);
}

static void	markup_db_delete(t_markup_db *db, int idx)
{
	if (!db || idx < 0 || (size_t)idx >= db->count)
		return;
	if (idx == (int)db->count - 1)
	{
		db->count--;
		return;
	}
	memmove(&db->items[idx], &db->items[idx + 1], (db->count - (size_t)idx - 1) * sizeof(*db->items));
	db->count--;
}

void	menu_config_armes(t_window *w)
{
	armes_db db;
	const char **items;
	t_menu m;
	int action;
	int clicked;
	int sw;
	static int side_scroll = 0;
	static int right_scroll = 0;
	static int focus_id = 0;
	int last_selected = -1234;
	int idx_new;
	int idx_ret;

	char name[128];
	char dpp[32], ammo_shot[32], decay_shot[32], amp_decay[32];
	char ammo_mu[32], weapon_mu[32], amp_mu[32];
	char amp_name[128];
	char notes[256];

	if (!w)
		return;
	memset(&db, 0, sizeof(db));
	if (!armes_db_load(&db, tm_path_armes_ini()))
	{
		const char *msg[] = {"[ERREUR] Impossible de charger armes.ini", tm_path_armes_ini()};
		ui_screen_message(w, "CONFIG ARMES", msg, 2);
		return;
	}

	idx_new = (int)db.count;
	idx_ret = (int)db.count + 1;
	items = (const char **)calloc(db.count + 2, sizeof(char *));
	if (!items)
	{
		armes_db_free(&db);
		return;
	}
	for (size_t i = 0; i < db.count; i++)
		items[i] = db.items[i].name;
	items[idx_new] = "(+) Nouvelle arme";
	items[idx_ret] = "Retour";
	menu_init(&m, items, (int)db.count + 2);
	/* Default buffers */
	name[0] = '\0';
	amp_name[0] = '\0';
	notes[0] = '\0';
	strcpy(dpp, "");
	strcpy(ammo_shot, "");
	strcpy(decay_shot, "");
	strcpy(amp_decay, "");
	strcpy(ammo_mu, "1.0000");
	strcpy(weapon_mu, "0.0000");
	strcpy(amp_mu, "0.0000");

	while (w->running)
	{
		t_ui_state ui;
		t_ui_layout ly;
		t_rect content;
		t_rect list_r;
		t_rect panel_r;
		int item_h = 40;
		int y0;
		int line_h = 36;
		int content_h;
		int y;
		int btn_w = 140;
		int btn_h = 32;
		int is_existing;
		ui.theme = &g_theme_dark;
		window_poll_events(w);
		overlay_tick_auto_hunt();
		if (w->key_escape)
			break;
		action = menu_update(&m, w);
		if (action == idx_ret)
			break;
		if (action >= 0)
			m.selected = action;

		/* Refresh list label widths */
		sw = ui_sidebar_width_for_labels(items, (int)db.count + 2, 14, UI_PAD);
		ui_calc_layout_ex(w, &ly, sw);
		content = ui_draw_chrome_ex(w, &ui, "Config / Armes", tm_path_armes_ini(),
			"Molette: scroll   Click: focus champ   Sauver/Supprimer", sw);
		list_r = (t_rect){ly.sidebar.x, ly.sidebar.y + UI_PAD,
			ly.sidebar.w, ly.sidebar.h - UI_PAD * 2};
		clicked = ui_list_scroll(w, &ui, list_r, items, (int)db.count + 2,
			&m.selected, item_h, 0, &side_scroll);
		if (clicked >= 0)
		{
			action = clicked;
			m.selected = clicked;
		}
		if (action == idx_ret)
			break;
		m.render_x = list_r.x;
		m.render_y = list_r.y;
		m.item_w = list_r.w;
		m.item_h = item_h;

		/* Load selection into buffers (only when selection changes) */
		if (m.selected != last_selected)
		{
			last_selected = m.selected;
			right_scroll = 0;
			focus_id = 0;
			if (m.selected >= 0 && m.selected < idx_new)
			{
				arme_stats *aw = &db.items[m.selected];
				safe_copy(name, sizeof(name), aw->name);
				setf(dpp, sizeof(dpp), aw->dpp);
				setm(ammo_shot, sizeof(ammo_shot), aw->ammo_shot);
				setm(decay_shot, sizeof(decay_shot), aw->decay_shot);
				setm(amp_decay, sizeof(amp_decay), aw->amp_decay_shot);
				setmu(ammo_mu, sizeof(ammo_mu), aw->ammo_mu_1e4);
				setmu(weapon_mu, sizeof(weapon_mu), aw->weapon_mu_1e4);
				setmu(amp_mu, sizeof(amp_mu), aw->amp_mu_1e4);
				safe_copy(amp_name, sizeof(amp_name), aw->amp_name);
				safe_copy(notes, sizeof(notes), aw->notes);
			}
			else
			{
				/* New template */
				name[0] = '\0';
				amp_name[0] = '\0';
				notes[0] = '\0';
				strcpy(dpp, "");
				strcpy(ammo_shot, "");
				strcpy(decay_shot, "");
				strcpy(amp_decay, "");
				strcpy(ammo_mu, "1.0000");
				strcpy(weapon_mu, "0.0000");
				strcpy(amp_mu, "0.0000");
			}
		}

		panel_r = (t_rect){content.x + UI_PAD, content.y + UI_PAD,
			content.w - UI_PAD * 2, content.h - UI_PAD * 2};
		ui_draw_panel(w, panel_r, ui.theme->surface, ui.theme->border);

		/* Scrollable form */
		content_h = 22 * line_h + 80;
		if (w->mouse_x >= panel_r.x && w->mouse_x < panel_r.x + panel_r.w
			&& w->mouse_y >= panel_r.y && w->mouse_y < panel_r.y + panel_r.h)
			ui_scroll_update(w, &right_scroll, content_h, panel_r.h, 32);
		y0 = panel_r.y + 12 - right_scroll;
		y = y0;

		/* Pro layout constants */
		{
			const int pad = 12;
			const int header_h = 28;
			const int input_h = 28;
			int label_w = 150;
			int xL = panel_r.x + pad;
			int xI = panel_r.x + pad + label_w;
			int wI = panel_r.w - (pad * 2 + label_w);
			unsigned int err = 0xFFE35D5D;
			unsigned int warn = 0xFFE8C15A;
			int ok_name = (name[0] != '\0');
			int ok_dpp = (dpp[0] == '\0' || is_number_str(dpp));
			int ok_ammo_shot = (ammo_shot[0] == '\0' || is_number_str(ammo_shot));
			int ok_decay_shot = (decay_shot[0] == '\0' || is_number_str(decay_shot));
			int ok_amp_decay = (amp_decay[0] == '\0' || is_number_str(amp_decay));
			int ok_ammo_mu = (ammo_mu[0] == '\0' || is_number_str(ammo_mu));
			int ok_weapon_mu = (weapon_mu[0] == '\0' || is_number_str(weapon_mu));
			int ok_amp_mu = (amp_mu[0] == '\0' || is_number_str(amp_mu));
			int form_ok = ok_name && ok_dpp && ok_ammo_shot && ok_decay_shot
				&& ok_amp_decay && ok_ammo_mu && ok_weapon_mu && ok_amp_mu;

			/* Header + quick help */
			ui_section_header(w, &ui, (t_rect){xL, y, panel_r.w - pad * 2, header_h},
				"Arme (armes.ini) - modele a completer");
			y += header_h + 8;
			if (!form_ok)
				ui_draw_text(w, xL, y + 2, "! Champs invalides: corrige avant de sauvegarder.", warn);
			y += line_h;

			/* Section: Identite */
			ui_section_header(w, &ui, (t_rect){xL, y, panel_r.w - pad * 2, header_h}, "Identite");
			y += header_h + 8;
			ui_draw_text(w, xL, y + 8, "Nom *", ui.theme->text2);
			{
				t_rect rr = (t_rect){xI, y, wI, input_h};
				ui_input_text(w, &ui, rr, name, (int)sizeof(name), &focus_id, 1);
				if (!ok_name)
					draw_invalid_border(w, rr, err);
			}
		y += line_h;

			/* Section: Couts */
			ui_section_header(w, &ui, (t_rect){xL, y, panel_r.w - pad * 2, header_h}, "Couts (par tir / shot)");
			y += header_h + 8;
			ui_draw_text(w, xL, y + 8, "DPP", ui.theme->text2);
			{
				t_rect rr = (t_rect){xI, y, 180, input_h};
				ui_input_text(w, &ui, rr, dpp, (int)sizeof(dpp), &focus_id, 2);
				if (!ok_dpp)
					draw_invalid_border(w, rr, err);
			}
		y += line_h;
			ui_draw_text(w, xL, y + 8, "Ammo/shot", ui.theme->text2);
			{
				t_rect rr = (t_rect){xI, y, 180, input_h};
				ui_input_text(w, &ui, rr, ammo_shot, (int)sizeof(ammo_shot), &focus_id, 3);
				if (!ok_ammo_shot)
					draw_invalid_border(w, rr, err);
			}
		y += line_h;
			ui_draw_text(w, xL, y + 8, "Decay/shot", ui.theme->text2);
			{
				t_rect rr = (t_rect){xI, y, 180, input_h};
				ui_input_text(w, &ui, rr, decay_shot, (int)sizeof(decay_shot), &focus_id, 4);
				if (!ok_decay_shot)
					draw_invalid_border(w, rr, err);
			}
		y += line_h;
			ui_draw_text(w, xL, y + 8, "Amp decay/shot", ui.theme->text2);
			{
				t_rect rr = (t_rect){xI, y, 180, input_h};
				ui_input_text(w, &ui, rr, amp_decay, (int)sizeof(amp_decay), &focus_id, 5);
				if (!ok_amp_decay)
					draw_invalid_border(w, rr, err);
			}
		y += line_h;

			/* Section: Markup */
			ui_section_header(w, &ui, (t_rect){xL, y, panel_r.w - pad * 2, header_h}, "Markup (MU)");
			y += header_h + 8;
			ui_draw_text(w, xL, y + 8, "Ammo MU (def: 1.0)", ui.theme->text2);
			{
				t_rect rr = (t_rect){xI, y, 180, input_h};
				ui_input_text(w, &ui, rr, ammo_mu, (int)sizeof(ammo_mu), &focus_id, 6);
				if (!ok_ammo_mu)
					draw_invalid_border(w, rr, err);
			}
		y += line_h;
			ui_draw_text(w, xL, y + 8, "Weapon MU", ui.theme->text2);
			{
				t_rect rr = (t_rect){xI, y, 180, input_h};
				ui_input_text(w, &ui, rr, weapon_mu, (int)sizeof(weapon_mu), &focus_id, 7);
				if (!ok_weapon_mu)
					draw_invalid_border(w, rr, err);
			}
		y += line_h;
			ui_draw_text(w, xL, y + 8, "Amp MU", ui.theme->text2);
			{
				t_rect rr = (t_rect){xI, y, 180, input_h};
				ui_input_text(w, &ui, rr, amp_mu, (int)sizeof(amp_mu), &focus_id, 8);
				if (!ok_amp_mu)
					draw_invalid_border(w, rr, err);
			}
		y += line_h;

			/* Section: Aide */
			ui_section_header(w, &ui, (t_rect){xL, y, panel_r.w - pad * 2, header_h}, "Aide (precision RCE)");
			y += header_h + 8;
			ui_draw_text(w, xL, y + 2, "Montants en PED: ex 0.01234 (4 dec) - accepte . ou ,", ui.theme->text2);
			y += line_h;
			ui_draw_text(w, xL, y + 2, "MU = multiplicateur: 1.0000=100% | 1.1000=110% (vide => 1.0)", ui.theme->text2);
			y += line_h;
			ui_draw_text(w, xL, y + 2, "Calcul cout/shot en entier (uPED) => pas d'arrondi flottant.", ui.theme->text2);
			y += line_h;

			/* Section: Amplifier */
			ui_section_header(w, &ui, (t_rect){xL, y, panel_r.w - pad * 2, header_h}, "Amplifier (optionnel)");
			y += header_h + 8;
			ui_draw_text(w, xL, y + 8, "Amp name", ui.theme->text2);
			ui_input_text(w, &ui, (t_rect){xI, y, wI, input_h}, amp_name, (int)sizeof(amp_name), &focus_id, 9);
		y += line_h;

			/* Section: Notes */
			ui_section_header(w, &ui, (t_rect){xL, y, panel_r.w - pad * 2, header_h}, "Notes");
			y += header_h + 8;
			ui_draw_text(w, xL, y + 8, "Notes", ui.theme->text2);
			ui_input_text(w, &ui, (t_rect){xI, y, wI, input_h}, notes, (int)sizeof(notes), &focus_id, 10);
		y += line_h;

			/* Buttons (always visible: pinned) */
			is_existing = (m.selected >= 0 && m.selected < idx_new);
			{
				t_rect bsave = (t_rect){panel_r.x + 12, panel_r.y + panel_r.h - (btn_h + 12), btn_w, btn_h};
				t_rect bdel  = (t_rect){panel_r.x + 12 + btn_w + 10, panel_r.y + panel_r.h - (btn_h + 12), btn_w, btn_h};
				t_rect bnew  = (t_rect){panel_r.x + 12 + (btn_w + 10) * 2, panel_r.y + panel_r.h - (btn_h + 12), btn_w, btn_h};

				if (ui_button(w, &ui, bsave, "Sauver", UI_BTN_PRIMARY, form_ok))
				{
					arme_stats aw;
					memset(&aw, 0, sizeof(aw));
					safe_copy(aw.name, sizeof(aw.name), name);
					safe_copy(aw.amp_name, sizeof(aw.amp_name), amp_name);
					safe_copy(aw.notes, sizeof(aw.notes), notes);
					aw.dpp = parse_d(dpp);
					aw.ammo_shot = parse_money_or0(ammo_shot);
					aw.decay_shot = parse_money_or0(decay_shot);
					aw.amp_decay_shot = parse_money_or0(amp_decay);
					aw.ammo_mu_1e4 = parse_mu_or_def(ammo_mu, 10000, 0);
					aw.weapon_mu_1e4 = parse_mu_or_def(weapon_mu, 0, 1);
					aw.amp_mu_1e4 = parse_mu_or_def(amp_mu, 0, 1);
					aw.markup_mu_1e4 = 10000;
					if (armes_db_upsert(&db, is_existing ? m.selected : -1, &aw))
					{
						armes_db_save(&db, tm_path_armes_ini());
						ui_screen_message(w, "CONFIG ARMES", (const char *[]) {"Sauvegarde OK.", "(Esc pour revenir)"}, 2);
						break;
					}
				}
				if (ui_button(w, &ui, bdel, "Supprimer", UI_BTN_SECONDARY, is_existing))
				{
					armes_db_delete(&db, m.selected);
					armes_db_save(&db, tm_path_armes_ini());
					ui_screen_message(w, "CONFIG ARMES", (const char *[]) {"Suppression OK.", "(Esc pour revenir)"}, 2);
					break;
				}
				if (ui_button(w, &ui, bnew, "Nouveau", UI_BTN_SECONDARY, 1))
				{
					m.selected = idx_new;
					last_selected = -1234;
				}
			}
		}

		window_present(w);
		ui_sleep_ms(16);
	}

	free(items);
	armes_db_free(&db);
}

void	menu_config_markup(t_window *w)
{
	t_markup_db db;
	const char **items;
	t_menu m;
	int action;
	int clicked;
	int sw;
	static int side_scroll = 0;
	static int right_scroll = 0;
	static int focus_id = 0;
	int last_selected = -1234;
	int idx_new;
	int idx_ret;

	char name[128];
	char type[32];
	char value[32];

	if (!w)
		return;
	markup_db_init(&db);
	if (markup_db_load(&db, tm_path_markup_ini()) != 0)
	{
		const char *msg[] = {"[ERREUR] Impossible de charger markup.ini", tm_path_markup_ini()};
		ui_screen_message(w, "CONFIG MARKUP", msg, 2);
		return;
	}
	idx_new = (int)db.count;
	idx_ret = (int)db.count + 1;
	items = (const char **)calloc(db.count + 2, sizeof(char *));
	if (!items)
	{
		markup_db_free(&db);
		return;
	}
	for (size_t i = 0; i < db.count; i++)
		items[i] = db.items[i].name;
	items[idx_new] = "(+) Nouvelle regle";
	items[idx_ret] = "Retour";
	menu_init(&m, items, (int)db.count + 2);
	name[0] = '\0';
	strcpy(type, "percent");
	strcpy(value, "1.0");

	while (w->running)
	{
		t_ui_state ui;
		t_ui_layout ly;
		t_rect content;
		t_rect list_r;
		t_rect panel_r;
		int item_h = 40;
		int line_h = 36;
		int y0;
		int y;
		int content_h;
		int is_existing;
		int btn_w = 140;
		int btn_h = 32;

		ui.theme = &g_theme_dark;
		window_poll_events(w);
		overlay_tick_auto_hunt();
		if (w->key_escape)
			break;
		action = menu_update(&m, w);
		if (action == idx_ret)
			break;
		if (action >= 0)
			m.selected = action;

		sw = ui_sidebar_width_for_labels(items, (int)db.count + 2, 14, UI_PAD);
		ui_calc_layout_ex(w, &ly, sw);
		content = ui_draw_chrome_ex(w, &ui, "Config / Markup", tm_path_markup_ini(),
			"Molette: scroll   Click: focus champ   Sauver/Supprimer", sw);
		list_r = (t_rect){ly.sidebar.x, ly.sidebar.y + UI_PAD,
			ly.sidebar.w, ly.sidebar.h - UI_PAD * 2};
		clicked = ui_list_scroll(w, &ui, list_r, items, (int)db.count + 2,
			&m.selected, item_h, 0, &side_scroll);
		if (clicked >= 0)
		{
			action = clicked;
			m.selected = clicked;
		}
		if (action == idx_ret)
			break;
		m.render_x = list_r.x;
		m.render_y = list_r.y;
		m.item_w = list_r.w;
		m.item_h = item_h;

		if (m.selected != last_selected)
		{
			last_selected = m.selected;
			right_scroll = 0;
			focus_id = 0;
			if (m.selected >= 0 && m.selected < idx_new)
			{
				t_markup_rule *r = &db.items[m.selected];
				safe_copy(name, sizeof(name), r->name);
				if (r->type == MARKUP_TT_PLUS)
					safe_copy(type, sizeof(type), "tt_plus");
				else
					safe_copy(type, sizeof(type), "percent");
				setf(value, sizeof(value), r->value);
			}
			else
			{
				name[0] = '\0';
				safe_copy(type, sizeof(type), "percent");
				safe_copy(value, sizeof(value), "1.0");
			}
		}

		panel_r = (t_rect){content.x + UI_PAD, content.y + UI_PAD,
			content.w - UI_PAD * 2, content.h - UI_PAD * 2};
		ui_draw_panel(w, panel_r, ui.theme->surface, ui.theme->border);
		content_h = 12 * line_h + 48;
		if (w->mouse_x >= panel_r.x && w->mouse_x < panel_r.x + panel_r.w
			&& w->mouse_y >= panel_r.y && w->mouse_y < panel_r.y + panel_r.h)
			ui_scroll_update(w, &right_scroll, content_h, panel_r.h, 32);
		y0 = panel_r.y + 12 - right_scroll;
		y = y0;

		/* Pro layout constants */
		{
			const int pad = 12;
			const int header_h = 28;
			const int input_h = 28;
			int label_w = 170;
			int xL = panel_r.x + pad;
			int xI = panel_r.x + pad + label_w;
			int wI = panel_r.w - (pad * 2 + label_w);
			unsigned int err = 0xFFE35D5D;
			unsigned int warn = 0xFFE8C15A;
			int ok_name = (name[0] != '\0');
			int ok_value = (value[0] != '\0' && is_number_str(value));
			int ok_type = (strcmp(type, "percent") == 0 || strcmp(type, "tt_plus") == 0);
			int form_ok = ok_name && ok_value && ok_type;

			ui_section_header(w, &ui, (t_rect){xL, y, panel_r.w - pad * 2, header_h},
				"Markup (markup.ini) - regle loot");
			y += header_h + 8;
			ui_draw_text(w, xL, y + 2, "Template: Nom_item + type + value", ui.theme->text2);
			y += line_h;
			if (!form_ok)
				ui_draw_text(w, xL, y + 2, "! Champs invalides: corrige avant de sauvegarder.", warn);
			y += line_h;

			ui_section_header(w, &ui, (t_rect){xL, y, panel_r.w - pad * 2, header_h}, "Regle");
			y += header_h + 8;
			ui_draw_text(w, xL, y + 8, "Nom item *", ui.theme->text2);
			{
				t_rect rr = (t_rect){xI, y, wI, input_h};
				ui_input_text(w, &ui, rr, name, (int)sizeof(name), &focus_id, 1);
				if (!ok_name)
					draw_invalid_border(w, rr, err);
			}
			y += line_h;

			ui_draw_text(w, xL, y + 8, "Type * (percent/tt_plus)", ui.theme->text2);
			{
				t_rect rr = (t_rect){xI, y, 200, input_h};
				ui_input_text(w, &ui, rr, type, (int)sizeof(type), &focus_id, 2);
				if (!ok_type)
					draw_invalid_border(w, rr, err);
			}
			y += line_h;

			ui_draw_text(w, xL, y + 8, "Value *", ui.theme->text2);
			{
				t_rect rr = (t_rect){xI, y, 200, input_h};
				ui_input_text(w, &ui, rr, value, (int)sizeof(value), &focus_id, 3);
				if (!ok_value)
					draw_invalid_border(w, rr, err);
			}
			y += line_h;

			ui_section_header(w, &ui, (t_rect){xL, y, panel_r.w - pad * 2, header_h}, "Aide");
			y += header_h + 8;
			ui_draw_text(w, xL, y + 2, "percent: value=1.20 => +20% sur TT", ui.theme->text2);
			y += line_h;
			ui_draw_text(w, xL, y + 2, "tt_plus: value=0.50 => +0.50 PED au TT", ui.theme->text2);
			y += line_h;

			is_existing = (m.selected >= 0 && m.selected < idx_new);
			{
				t_rect bsave = (t_rect){panel_r.x + 12, panel_r.y + panel_r.h - (btn_h + 12), btn_w, btn_h};
				t_rect bdel  = (t_rect){panel_r.x + 12 + btn_w + 10, panel_r.y + panel_r.h - (btn_h + 12), btn_w, btn_h};
				t_rect bnew  = (t_rect){panel_r.x + 12 + (btn_w + 10) * 2, panel_r.y + panel_r.h - (btn_h + 12), btn_w, btn_h};
				if (ui_button(w, &ui, bsave, "Sauver", UI_BTN_PRIMARY, form_ok))
				{
					t_markup_rule r;
					memset(&r, 0, sizeof(r));
					safe_copy(r.name, sizeof(r.name), name);
					r.value = parse_d(value);
					r.type = MARKUP_PERCENT;
					if (strcmp(type, "tt_plus") == 0)
						r.type = MARKUP_TT_PLUS;
					markup_db_upsert(&db, is_existing ? m.selected : -1, &r);
					markup_db_save(&db, tm_path_markup_ini());
					ui_screen_message(w, "CONFIG MARKUP", (const char *[]) {"Sauvegarde OK.", "(Esc pour revenir)"}, 2);
					break;
				}
				if (ui_button(w, &ui, bdel, "Supprimer", UI_BTN_SECONDARY, is_existing))
				{
					markup_db_delete(&db, m.selected);
					markup_db_save(&db, tm_path_markup_ini());
					ui_screen_message(w, "CONFIG MARKUP", (const char *[]) {"Suppression OK.", "(Esc pour revenir)"}, 2);
					break;
				}
				if (ui_button(w, &ui, bnew, "Nouveau", UI_BTN_SECONDARY, 1))
				{
					m.selected = idx_new;
					last_selected = -1234;
				}
			}
		}

		window_present(w);
		ui_sleep_ms(16);
	}

	free(items);
	markup_db_free(&db);
}
