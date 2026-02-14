#include "ui_widgets.h"

#include "ui_utils.h"

#include <string.h>

static int	in_rect(int x, int y, t_rect r)
{
	return (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h);
}

/* -------------------------------------------------------------------------- */
/* Text helpers                                                               */
/* -------------------------------------------------------------------------- */

static void	ui_draw_text_ellipsis(t_window *w, int x, int y,
				const char *text, unsigned int color,
				int max_w_px, int font_px)
{
	char	buf[512];
	int		len;
	int		max_chars;
	int		keep;
	int		i;

	if (!w || !text || max_w_px <= 0)
		return ;
	if (font_px <= 0)
		font_px = 14;
	if (ui_measure_text_w(text, font_px) <= max_w_px)
	{
		window_draw_text(w, x, y, text, color);
		return ;
	}
	/* Approximate characters that can fit, based on ui_measure_text_w(). */
	max_chars = (int)((double)max_w_px / ((double)font_px * 0.60) + 0.5);
	if (max_chars < 4)
		return ;
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

int	ui_measure_text_w(const char *text, int font_px)
{
	/*
	 * Approximation: core X11 fonts are roughly monospace-ish in our use.
	 * Using 0.60 * font size per glyph yields decent sizing at 12/14/16.
	 */
	int len;
	double cw;

	if (!text)
		return (0);
	len = (int)strlen(text);
	cw = (double)font_px * 0.60;
	return ((int)(cw * (double)len + 0.5));
}

void	ui_scroll_update(t_window *w, int *scroll_y,
		int content_h, int view_h, int step_px)
{
	int max_scroll;

	if (!scroll_y)
		return ;
	if (!w)
		return ;
	if (step_px <= 0)
		step_px = 24;
	if (content_h < 0)
		content_h = 0;
	if (view_h < 0)
		view_h = 0;
	max_scroll = content_h - view_h;
	if (max_scroll < 0)
		max_scroll = 0;
	/* Wheel: +1 = up => scroll up */
	if (w->mouse_wheel > 0)
		*scroll_y -= w->mouse_wheel * step_px;
	else if (w->mouse_wheel < 0)
		*scroll_y -= w->mouse_wheel * step_px; /* negative wheel => add */
	if (*scroll_y < 0)
		*scroll_y = 0;
	if (*scroll_y > max_scroll)
		*scroll_y = max_scroll;
}

int	ui_sidebar_width_for_labels(const char **items, int count,
		int font_px, int pad_px)
{
	int		i;
	int		maxw;
	int		tw;

	if (!items || count <= 0)
		return (0);
	if (font_px <= 0)
		font_px = 14;
	if (pad_px < 0)
		pad_px = 0;
	maxw = 0;
	i = 0;
	while (i < count)
	{
		tw = ui_measure_text_w(items[i], font_px);
		if (tw > maxw)
			maxw = tw;
		i++;
	}
	/* + left padding for selection bar + inner padding */
	return (maxw + (pad_px * 2) + 28);
}

static int	clamp_i(int v, int min, int max)
{
	if (v < min)
		return (min);
	if (v > max)
		return (max);
	return (v);
}

static int	ui_id_from_rect(t_rect r, int salt)
{
	unsigned int	h;

	h = (unsigned int)r.x;
	h = h * 131u + (unsigned int)r.y;
	h = h * 131u + (unsigned int)r.w;
	h = h * 131u + (unsigned int)r.h;
	h ^= (unsigned int)salt;
	return ((int)(h & 0x7fffffff));
}

/*
 * Draggable vertical scrollbar.
 * - track is the scrollbar rect.
 * - content_h/view_h determine max scroll.
 * - scroll_y is in pixels.
 */
static void	ui_vscrollbar(t_window *w, t_ui_state *ui, t_rect track,
				int content_h, int view_h, int *scroll_y, int id)
{
	int		max_scroll;
	int		thumb_h;
	int		thumb_y;
	t_rect		thumb;
	int		hover_track;
	int		hover_thumb;
	unsigned int	bg;
	unsigned int	thumb_c;

	if (!w || !ui || !ui->theme || !scroll_y)
		return ;
	max_scroll = content_h - view_h;
	if (max_scroll < 0)
		max_scroll = 0;
	if (max_scroll == 0)
		return ;
	/* Thumb size proportional to visible area */
	thumb_h = (view_h * view_h) / content_h;
	if (thumb_h < 18)
		thumb_h = 18;
	if (thumb_h > track.h)
		thumb_h = track.h;
	thumb_y = track.y + (int)((long long)(track.h - thumb_h) * (long long)(*scroll_y) / (long long)max_scroll);
	thumb = (t_rect){track.x + 2, thumb_y, track.w - 4, thumb_h};

	hover_track = in_rect(w->mouse_x, w->mouse_y, track);
	hover_thumb = in_rect(w->mouse_x, w->mouse_y, thumb);

	/* Release capture */
	if (!w->mouse_left_down && w->ui_active_id == id)
		w->ui_active_id = 0;

	/* Start drag */
	if (w->mouse_left_click && hover_thumb)
	{
		w->ui_active_id = id;
		w->ui_drag_offset_y = w->mouse_y - thumb.y;
	}
	/* Click on track: jump */
	else if (w->mouse_left_click && hover_track)
	{
		int pos;

		pos = w->mouse_y - track.y - (thumb_h / 2);
		if (pos < 0)
			pos = 0;
		if (pos > track.h - thumb_h)
			pos = track.h - thumb_h;
		*scroll_y = (int)((long long)pos * (long long)max_scroll / (long long)(track.h - thumb_h));
	}
	/* Dragging */
	if (w->mouse_left_down && w->ui_active_id == id)
	{
		int pos;

		pos = w->mouse_y - track.y - w->ui_drag_offset_y;
		if (pos < 0)
			pos = 0;
		if (pos > track.h - thumb_h)
			pos = track.h - thumb_h;
		*scroll_y = (int)((long long)pos * (long long)max_scroll / (long long)(track.h - thumb_h));
	}

	bg = ui->theme->surface2;
	thumb_c = ui->theme->border;
	if (hover_track)
		bg = ui_color_lerp(ui->theme->surface2, ui->theme->bg, 60);
	if (hover_thumb || (w->ui_active_id == id))
		thumb_c = ui->theme->accent;
	window_fill_rect(w, track.x, track.y, track.w, track.h, bg);
	window_fill_rect(w, thumb.x, thumb.y, thumb.w, thumb.h, thumb_c);
}

/*
 * Scrollable list for small screens / long menus.
 * scroll_y is in pixels.
 */
int	ui_list_scroll(t_window *w, t_ui_state *ui, t_rect r,
		const char **items, int count, int *selected,
		int item_h, int show_icons, int *scroll_y)
{
	int			idx_clicked;
	int			content_h;
	int			max_scroll;
	int			start;
	int			yoff;
	int			i;
	t_rect			ir;
	unsigned int	bg;
	unsigned int	fg;
	unsigned int	sep;
	unsigned int	sel_bg;
	unsigned int	sel_bar;
	int			hover;

	(void)show_icons;
	if (!w || !ui || !ui->theme || !items || count <= 0)
		return (-1);
	if (item_h <= 0)
		item_h = 40;
	idx_clicked = -1;
	sep = ui->theme->border;
	sel_bg = ui_color_lerp(ui->theme->accent, ui->theme->bg, 170);
	sel_bar = ui->theme->accent;

	/* Scroll (wheel + draggable scrollbar) */
	if (scroll_y)
	{
		t_rect	list_r;
		t_rect	sb_r;
		int		sb_w;
		int		id;

		sb_w = 14;
		content_h = count * item_h;
		max_scroll = content_h - r.h;
		if (max_scroll < 0)
			max_scroll = 0;
		/* Split list and scrollbar so clicks on the bar don't select items */
		list_r = (t_rect){r.x, r.y, r.w - sb_w, r.h};
		sb_r = (t_rect){r.x + r.w - sb_w, r.y, sb_w, r.h};
		id = ui_id_from_rect(sb_r, 0x51534C); /* 'QSL' */
		/* Wheel only when hovering the list area (not the scrollbar) */
		if (in_rect(w->mouse_x, w->mouse_y, list_r))
			ui_scroll_update(w, scroll_y, content_h, r.h, item_h);
		*scroll_y = clamp_i(*scroll_y, 0, max_scroll);
		/* Drag bar */
		ui_vscrollbar(w, ui, sb_r, content_h, r.h, scroll_y, id);
		/*
		 * Keep selected visible ONLY when navigating with keyboard.
		 * Otherwise, mouse wheel scrolling would snap back to the selected item.
		 */
		if (selected && (w->key_up || w->key_down) && *selected >= 0 && *selected < count)
		{
			int sel_top = (*selected) * item_h;
			int sel_bot = sel_top + item_h;
			if (sel_top < *scroll_y)
				*scroll_y = sel_top;
			else if (sel_bot > *scroll_y + r.h)
				*scroll_y = sel_bot - r.h;
			*scroll_y = clamp_i(*scroll_y, 0, max_scroll);
		}
		start = (*scroll_y) / item_h;
		yoff = -((*scroll_y) % item_h);
		/* Use list_r for hit-testing/drawing below */
		r = list_r;
	}
	else
	{
		start = 0;
		yoff = 0;
	}

	i = start;
	while (i < count)
	{
		ir = (t_rect){r.x, r.y + (i - start) * item_h + yoff, r.w, item_h};
		if (ir.y >= r.y + r.h)
			break;
		if (ir.y + ir.h <= r.y)
		{
			i++;
			continue;
		}
		hover = in_rect(w->mouse_x, w->mouse_y, ir);
		bg = ui->theme->surface;
		fg = ui->theme->text2;
		if (selected && *selected == i)
		{
			bg = sel_bg;
			fg = ui->theme->text;
			window_fill_rect(w, ir.x, ir.y, 4, ir.h, sel_bar);
		}
		else if (hover)
			bg = ui->theme->surface2;
		window_fill_rect(w, ir.x, ir.y, ir.w, ir.h, bg);
		window_fill_rect(w, ir.x, ir.y + ir.h - 1, ir.w, 1, sep);
		ui_draw_text(w, ir.x + 12, ir.y + (ir.h / 2 - 6), items[i], fg);
		if (hover && w->mouse_left_click)
			idx_clicked = i;
		i++;
	}
	return (idx_clicked);
}

void	ui_draw_text(t_window *w, int x, int y, const char *text,
		unsigned int color)
{
	window_draw_text(w, x, y, text, color);
}

void	ui_draw_panel(t_window *w, t_rect r, unsigned int bg,
		unsigned int border)
{
	window_fill_rect(w, r.x, r.y, r.w, r.h, bg);
	/* 1px border */
	window_fill_rect(w, r.x, r.y, r.w, 1, border);
	window_fill_rect(w, r.x, r.y + r.h - 1, r.w, 1, border);
	window_fill_rect(w, r.x, r.y, 1, r.h, border);
	window_fill_rect(w, r.x + r.w - 1, r.y, 1, r.h, border);
}

int	ui_button(t_window *w, t_ui_state *ui, t_rect r,
		const char *label, t_ui_btn_style style, int enabled)
{
	unsigned int	bg;
	unsigned int	fg;
	unsigned int	bd;
	int			hover;
	int			clicked;

	if (!w || !ui || !ui->theme)
		return (0);
	hover = in_rect(w->mouse_x, w->mouse_y, r);
	clicked = (enabled && hover && w->mouse_left_click);
	bd = ui->theme->border;
	fg = ui->theme->text;
	if (!enabled)
	{
		bg = ui->theme->border;
		fg = ui->theme->text2;
	}
	else if (style == UI_BTN_PRIMARY)
	{
		bg = ui->theme->accent;
		fg = 0xFFFFFF;
		if (hover)
			bg = ui_color_lerp(bg, 0xFFFFFF, 25);
	}
	else if (style == UI_BTN_SECONDARY)
	{
		bg = ui->theme->surface;
		if (hover)
			bg = ui->theme->surface2;
	}
	else
	{
		bg = ui->theme->bg;
		if (hover)
			bg = ui->theme->surface2;
	}
	ui_draw_panel(w, r, bg, bd);
	ui_draw_text(w, r.x + 12, r.y + (r.h / 2 - 6), label, fg);
	return (clicked);
}

int	ui_input_text(t_window *w, t_ui_state *ui, t_rect r,
			char *buffer, int bufsz, int *focus_id, int my_id)
{
	unsigned int bg;
	unsigned int bd;
	unsigned int fg;
	int hover;
	int focused;
	int changed;

	if (!w || !ui || !ui->theme || !buffer || bufsz <= 1 || !focus_id)
		return (0);
	hover = in_rect(w->mouse_x, w->mouse_y, r);
	if (w->mouse_left_click && hover)
		*focus_id = my_id;
	focused = (*focus_id == my_id);
	changed = 0;
	bd = ui->theme->border;
	bg = ui->theme->surface2;
	fg = ui->theme->text;
	if (hover && !focused)
		bg = ui->theme->surface;
	if (focused)
	{
		bd = ui->theme->accent;
		bg = ui_color_lerp(ui->theme->accent, ui->theme->bg, 220);
	}
	ui_draw_panel(w, r, bg, bd);
	ui_draw_text(w, r.x + 10, r.y + (r.h / 2 - 6), buffer, fg);

	/* Edit when focused */
	if (focused)
	{
		int len = (int)strlen(buffer);
		if (w->key_backspace && len > 0)
		{
			buffer[len - 1] = '\0';
			changed = 1;
		}
		if (w->text_len > 0)
		{
			for (int i = 0; i < w->text_len; i++)
			{
				char c = w->text_input[i];
				if (c < 32 || c == 127)
					continue;
				len = (int)strlen(buffer);
				if (len < bufsz - 1)
				{
					buffer[len] = c;
					buffer[len + 1] = '\0';
					changed = 1;
				}
			}
		}
	}
	return (changed);
}

void	ui_text_lines_scroll(t_window *w, t_ui_state *ui, t_rect r,
			const char **lines, int n, int line_h,
			unsigned int color, int *scroll_y)
{
	int	content_h;
	int	scroll;
	int	pad;
	int	i;
	int	yy;
	int	min_y;
	int	max_y;
	int	text_w;

	if (!w || !ui || !ui->theme || !lines || n <= 0)
		return;
	if (line_h <= 0)
		line_h = 14;
	pad = 8;
	content_h = n * line_h + (pad * 2);
	scroll = (scroll_y ? *scroll_y : 0);
	if (scroll_y)
	{
		if (in_rect(w->mouse_x, w->mouse_y, r))
			ui_scroll_update(w, scroll_y, content_h, r.h, line_h);
		if (*scroll_y < 0)
			*scroll_y = 0;
		scroll = *scroll_y;
	}

	/* Logical clipping: never draw outside the panel bounds. */
	min_y = r.y + 2;
	max_y = r.y + r.h - 2;
	text_w = r.w - (pad * 2) - 6;
	if (text_w < 0)
		text_w = 0;

	i = 0;
	while (i < n)
	{
		yy = (r.y + pad) + i * line_h - scroll;
		if (yy >= max_y)
			break;
		if (yy + line_h <= min_y)
		{
			i++;
			continue;
		}
		if (lines[i] && lines[i][0])
			ui_draw_text_ellipsis(w, r.x + pad, yy, lines[i], color, text_w, line_h);
		i++;
	}
}

int	ui_list(t_window *w, t_ui_state *ui, t_rect r,
		const char **items, int count, int *selected,
		int item_h, int show_icons)
{
	int			i;
	int			idx_clicked;
	t_rect			ir;
	unsigned int	bg;
	unsigned int	fg;
	unsigned int	sep;
	unsigned int	sel_bg;
	unsigned int	sel_bar;
	int			hover;

	(void)show_icons;
	if (!w || !ui || !ui->theme || !items || count <= 0)
		return (-1);
	sep = ui->theme->border;
	sel_bg = ui_color_lerp(ui->theme->accent, ui->theme->bg, 170);
	sel_bar = ui->theme->accent;
	idx_clicked = -1;
	i = 0;
	while (i < count)
	{
		ir = (t_rect){r.x, r.y + i * item_h, r.w, item_h};
		if (ir.y + ir.h > r.y + r.h)
			break ;
		hover = in_rect(w->mouse_x, w->mouse_y, ir);
		bg = ui->theme->surface;
		fg = ui->theme->text2;
		if (selected && *selected == i)
		{
			bg = sel_bg;
			fg = ui->theme->text;
			window_fill_rect(w, ir.x, ir.y, 4, ir.h, sel_bar);
		}
		else if (hover)
			bg = ui->theme->surface2;
		window_fill_rect(w, ir.x, ir.y, ir.w, ir.h, bg);
		window_fill_rect(w, ir.x, ir.y + ir.h - 1, ir.w, 1, sep);
		ui_draw_text(w, ir.x + 12, ir.y + (ir.h / 2 - 6), items[i], fg);
		if (hover && w->mouse_left_click)
			idx_clicked = i;
		i++;
	}
	return (idx_clicked);
}

void	ui_card(t_window *w, t_ui_state *ui, t_rect r,
		const char *title, const char *value, unsigned int value_color)
{
	if (!w || !ui || !ui->theme)
		return ;
	ui_draw_panel(w, r, ui->theme->surface, ui->theme->border);
	ui_draw_text(w, r.x + 12, r.y + 10, title, ui->theme->text2);
	ui_draw_text(w, r.x + 12, r.y + 28, value, value_color);
}

void	ui_draw_hline(t_window *w, int x, int y, int width, unsigned int color)
{
	if (!w || width <= 0)
		return;
	window_fill_rect(w, x, y, width, 1, (int)color);
}

void	ui_section_header(t_window *w, t_ui_state *ui, t_rect r, const char *title)
{
	unsigned int bg;
	unsigned int bd;
	unsigned int fg;

	if (!w || !ui || !ui->theme)
		return;
	bg = ui->theme->surface2;
	bd = ui->theme->border;
	fg = ui->theme->text;
	ui_draw_panel(w, r, bg, bd);
	if (title)
		ui_draw_text(w, r.x + 10, r.y + (r.h / 2 - 6), title, fg);
}
