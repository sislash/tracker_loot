#include "ui_layout.h"

static int	clamp_i(int v, int min, int max)
{
	if (v < min)
		return (min);
	if (v > max)
		return (max);
	return (v);
}

void	ui_calc_layout_ex(t_window *w, t_ui_layout *out, int desired_sidebar_w)
{
	int sw;
	int max_sw;
	int min_sw;

	if (!w || !out)
		return ;
	out->root = (t_rect){0, 0, w->width, w->height};
	out->top_h = UI_TOP_H;
	out->footer_h = UI_FOOTER_H;

	/* Default: responsive-ish base width, overridden by desired_sidebar_w. */
	sw = (desired_sidebar_w > 0) ? desired_sidebar_w : UI_SIDEBAR_W;
	if (desired_sidebar_w <= 0 && w->width < 700)
		sw = w->width / 3;

	/* Keep enough room for content, even on small windows. */
	max_sw = UI_SIDEBAR_MAX;
	if (w->width / 2 < max_sw)
		max_sw = w->width / 2;
	if (w->width - 320 < max_sw)
		max_sw = w->width - 320;
	if (max_sw < UI_SIDEBAR_MIN)
		max_sw = UI_SIDEBAR_MIN;
	min_sw = UI_SIDEBAR_MIN;
	if (w->width - 200 < min_sw)
		min_sw = w->width - 200;
	if (min_sw < 160)
		min_sw = 160;

	sw = clamp_i(sw, min_sw, max_sw);
	out->sidebar_w = sw;
	out->topbar = (t_rect){0, 0, w->width, out->top_h};
	out->footer = (t_rect){0, w->height - out->footer_h, w->width, out->footer_h};
	out->sidebar = (t_rect){0, out->top_h, out->sidebar_w,
		w->height - out->top_h - out->footer_h};
	out->content = (t_rect){out->sidebar_w, out->top_h,
		w->width - out->sidebar_w,
		w->height - out->top_h - out->footer_h};
}

void	ui_calc_layout(t_window *w, t_ui_layout *out)
{
	ui_calc_layout_ex(w, out, 0);
}
