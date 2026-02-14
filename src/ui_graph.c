#include "ui_graph.h"

#include "ui_downsample.h"

#include "window.h"

#include "utils.h"

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>


/*
** High-stakes UX:
** Do NOT truncate long sessions to a fixed point count (it destroys peaks/creux).
** We downsample deterministically by screen column (min/max per pixel).
*/

typedef struct s_ui_graph_dsbuf
{
	t_point_i	*poly_pts;
	int			poly_pts_cap;

	int			*poly_idx;
	int			poly_idx_cap;

	t_point_i	*rep_pts;
	int			rep_pts_cap;

	int			*rep_idx;
	int			rep_idx_cap;
} 	t_ui_graph_dsbuf;

typedef struct s_ui_graph_cache
{
	t_ui_graph_dsbuf	buf;
	int			poly_n;
	int			rep_n;

	/* Cache key (deterministic): recompute only when any key changes. */
	uint32_t		series_version;
	const double		*values;
	const int		*x_seconds;
	int			n;
	int			xmin;
	int			xmax;
	double			vmin;
	double			vmax;
	t_rect			plot;
	int			valid;
} 	t_ui_graph_cache;

/* Forward declarations used by graph helpers inserted near the top. */
static int	tm_isnan(double v);
static int	clamp_int(int v, int lo, int hi);

static void	dsbuf_ensure_points(t_point_i **p, int *cap, int need)
{
	if (!p || !cap)
		return ;
	if (*cap >= need)
		return ;
	while (*cap < need)
		*cap = (*cap == 0) ? 256 : (*cap * 2);
	*p = (t_point_i *)realloc(*p, sizeof(**p) * (size_t)(*cap));
}

static void	dsbuf_ensure_ints(int **p, int *cap, int need)
{
	if (!p || !cap)
		return ;
	if (*cap >= need)
		return ;
	while (*cap < need)
		*cap = (*cap == 0) ? 256 : (*cap * 2);
	*p = (int *)realloc(*p, sizeof(**p) * (size_t)(*cap));
}

static int	rect_eq(t_rect a, t_rect b)
{
	return (a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h);
}

static void	ui_graph_cache_compute(t_ui_graph_cache *c,
			const double *values, const int *x_seconds, int n,
			int xmin, int xmax, double vmin, double vmax, t_rect plot,
			uint32_t series_version)
{
	int	need_poly;
	int	need_rep;
	int	poly_cap;
	int	rep_cap;

	if (!c)
		return ;
	if (c->valid
		&& c->series_version == series_version
		&& c->values == values
		&& c->x_seconds == x_seconds
		&& c->n == n
		&& c->xmin == xmin
		&& c->xmax == xmax
		&& fabs(c->vmin - vmin) < 1e-9
		&& fabs(c->vmax - vmax) < 1e-9
		&& rect_eq(c->plot, plot))
		return ;

	c->values = values;
	c->x_seconds = x_seconds;
	c->n = n;
	c->xmin = xmin;
	c->xmax = xmax;
	c->vmin = vmin;
	c->vmax = vmax;
	c->plot = plot;
	c->series_version = series_version;
	c->valid = 1;

	need_poly = (plot.w + 1) * 4 + 16;
	need_rep = (plot.w + 1) + 16;
	dsbuf_ensure_points(&c->buf.poly_pts, &c->buf.poly_pts_cap, need_poly);
	dsbuf_ensure_ints(&c->buf.poly_idx, &c->buf.poly_idx_cap, need_poly);
	dsbuf_ensure_points(&c->buf.rep_pts, &c->buf.rep_pts_cap, need_rep);
	dsbuf_ensure_ints(&c->buf.rep_idx, &c->buf.rep_idx_cap, need_rep);

	poly_cap = c->buf.poly_pts_cap;
	if (c->buf.poly_idx_cap < poly_cap)
		poly_cap = c->buf.poly_idx_cap;
	rep_cap = c->buf.rep_pts_cap;
	if (c->buf.rep_idx_cap < rep_cap)
		rep_cap = c->buf.rep_idx_cap;

	c->rep_n = 0;
	c->poly_n = ui_downsample_minmax_pixels(values, x_seconds, n,
		xmin, xmax, vmin, vmax, plot,
		c->buf.poly_pts, c->buf.poly_idx, poly_cap,
		c->buf.rep_pts, c->buf.rep_idx, rep_cap, &c->rep_n);
}

static int	graph_point_for_index(t_rect plot,
						int xmin, int xmax,
						double ymax,
						const double *values,
						const int *x_seconds,
						int n,
						int idx,
						t_point_i *out)
{
	double	v;
	double	xv;
	double	xn;
	int		px;
	int		py;

	if (!out || !values || idx < 0 || idx >= n)
		return (0);
	v = values[idx];
	if (tm_isnan(v))
		return (0);
	if (v < 0.0)
		v = 0.0;
	if (ymax <= 0.0)
		ymax = 1.0;
	if (x_seconds)
		xv = (double)x_seconds[idx];
	else
		xv = (double)idx;
	xn = (xv - (double)xmin) / (double)(xmax - xmin);
	/* Skip points outside the requested X range (important for zoom). */
	if (xn < 0.0 || xn > 1.0)
		return (0);
	px = plot.x + (int)round(xn * (double)plot.w);
	py = plot.y + plot.h - (int)round((v / ymax) * (double)plot.h);
	py = clamp_int(py, plot.y, plot.y + plot.h);
	out->x = px;
	out->y = py;
	return (1);
}

static int	tm_isnan(double v)
{
#if defined(_MSC_VER)
	return (_isnan(v));
#else
	return (isnan(v));
#endif
}

static int	clamp_int(int v, int lo, int hi)
{
	if (v < lo)
		return (lo);
	if (v > hi)
		return (hi);
	return (v);
}

static void	fmt_time(char *dst, size_t cap, int seconds, int show_seconds)
{
	int	h;
	int	m;
	int	s;

	if (!dst || cap == 0)
		return ;
	if (seconds < 0)
		seconds = 0;
	h = seconds / 3600;
	seconds %= 3600;
	m = seconds / 60;
	s = seconds % 60;
	if (h > 0)
		snprintf(dst, cap, "%dh%02d", h, m);
	else if (show_seconds)
		snprintf(dst, cap, "%dm%02d", m, s);
	else
		snprintf(dst, cap, "%dm", m);
}

/* "Nice number" helper for Y axis steps (1/2/5 * 10^n). */
static double	nice_num(double x, int do_round)
{
	double	exp10;
	double	f;
	double	nf;

	if (x <= 0.0)
		return (1.0);
	exp10 = pow(10.0, floor(log10(x)));
	f = x / exp10;
	if (do_round)
	{
		if (f < 1.5)
			nf = 1.0;
		else if (f < 3.0)
			nf = 2.0;
		else if (f < 7.0)
			nf = 5.0;
		else
			nf = 10.0;
	}
	else
	{
		if (f <= 1.0)
			nf = 1.0;
		else if (f <= 2.0)
			nf = 2.0;
		else if (f <= 5.0)
			nf = 5.0;
		else
			nf = 10.0;
	}
	return (nf * exp10);
}

static double	nice_step(double minv, double maxv, int target_ticks)
{
	double	range;
	double	step;

	if (target_ticks < 2)
		target_ticks = 2;
	range = maxv - minv;
	if (range <= 0.0)
		range = (maxv > 0.0 ? maxv : 1.0);
	step = nice_num(range / (double)(target_ticks - 1), 1);
	if (step <= 0.0)
		step = 1.0;
	return (step);
}

/* Friendly time step selection for X axis (seconds). */
static int	time_step_seconds(int range_sec, int target_ticks)
{
	static const int	steps[] = {
		1, 2, 5, 10, 15, 30,
		60, 120, 300, 600, 900,
		1800, 3600, 7200, 14400,
		21600, 43200, 86400
	};
	double				rough;
	int					need;
	int					i;

	if (target_ticks < 2)
		target_ticks = 2;
	if (range_sec < 1)
		range_sec = 1;
	rough = (double)range_sec / (double)(target_ticks - 1);
	need = (int)ceil(rough);
	i = 0;
	while (i < (int)(sizeof(steps) / sizeof(steps[0])))
	{
		if (steps[i] >= need)
			return (steps[i]);
		i++;
	}
	return (steps[(int)(sizeof(steps) / sizeof(steps[0])) - 1]);
}

/*
 * Group count badges were removed from the Loot/kill graph (too noisy).
 * If you want to re-enable them later, reintroduce the helper here and call it
 * from the marker loop.
 */

static int	rect_intersects(t_rect a, t_rect b)
{
	if (a.x + a.w <= b.x)
		return (0);
	if (b.x + b.w <= a.x)
		return (0);
	if (a.y + a.h <= b.y)
		return (0);
	if (b.y + b.h <= a.y)
		return (0);
	return (1);
}

/*
 * Fit a label inside a max pixel width using a cheap ellipsis strategy.
 * We keep it local to ui_graph.c (no extra deps) and reuse ui_measure_text_w().
 */
static const char	*fit_text_ellipsis(t_ui_state *ui,
				const char *text,
				char *dst,
				size_t cap,
				int max_w_px,
				int font_px)
{
	int		len;
	int		max_chars;
	int		keep;
	int		i;

	(void)ui;
	if (!text || !dst || cap == 0 || max_w_px <= 0)
		return (text);
	if (font_px <= 0)
		font_px = 12;
	if (ui_measure_text_w(text, font_px) <= max_w_px)
		return (text);
	/* Approximate number of glyphs that can fit (same heuristic as ui_widgets.c). */
	max_chars = (int)((double)max_w_px / ((double)font_px * 0.60) + 0.5);
	if (max_chars < 4)
	{
		dst[0] = '\0';
		return (dst);
	}
	if (max_chars > (int)cap - 1)
		max_chars = (int)cap - 1;
	keep = max_chars - 3;
	len = (int)strlen(text);
	if (keep > len)
		keep = len;
	i = 0;
	while (i < keep)
	{
		dst[i] = text[i];
		i++;
	}
	dst[i++] = '.';
	dst[i++] = '.';
	dst[i++] = '.';
	dst[i] = '\0';
	return (dst);
}

static t_rect	compute_annotation_box(t_ui_state *ui, t_rect plot,
					const t_point_i *p, const char *text,
					int dist, int prefer_left, int prefer_below)
{
	int			tw;
	int			bw;
	int			bh;
	int			x;
	int			y;
	const int	base_gap = 14;
	int			max_bw;

	if (!ui || !ui->theme || !p || !text)
		return ((t_rect){0, 0, 0, 0});
	tw = ui_measure_text_w(text, 12);
	bw = tw + 10;
	bh = 18;
	/* Hard clamp: never allow the annotation box to exceed plot bounds. */
	max_bw = plot.w - 4;
	if (max_bw < 4)
		max_bw = 4;
	if (bw > max_bw)
		bw = max_bw;
	/*
	 * Label side selection:
	 * - Prefer right of the point by default (pro HUD conventions)
	 * - Allow the placement code to request left side to avoid overlaps.
	 */
	{
		int x_right = p->x + 10;
		int x_left = p->x - 10 - bw;
		x = (prefer_left ? x_left : x_right);
		/* Try to keep chosen side, but fall back if it goes out of the plot. */
		if (x + bw > plot.x + plot.w - 2)
			x = x_left;
		if (x < plot.x + 2)
			x = x_right;
		/* Final clamp */
		if (x + bw > plot.x + plot.w - 2)
			x = plot.x + plot.w - 2 - bw;
		if (x < plot.x + 2)
			x = plot.x + 2;
	}
	/*
	 * Vertical placement:
	 * - Keep labels off the curve/point (base_gap)
	 * - Allow the placement code to prefer above/below.
	 * - If the preferred side doesn't fit, flip.
	 */
	if (!prefer_below)
		y = p->y - bh - base_gap - dist;
	else
		y = p->y + base_gap + dist;
	/* Flip if it doesn't fit */
	if (y < plot.y + 2)
		y = p->y + base_gap + dist;
	if (y + bh > plot.y + plot.h - 2)
		y = p->y - bh - base_gap - dist;
	if (y + bh > plot.y + plot.h - 2)
		y = plot.y + plot.h - 2 - bh;
	if (y < plot.y + 2)
		y = plot.y + 2;
	return ((t_rect){x, y, bw, bh});
}


static void	draw_annotation(t_window *w, t_ui_state *ui, t_rect plot,
					const t_point_i *p, const char *text,
					unsigned int accent, int dist, int prefer_left, int prefer_below)
{
	t_rect		box;
	unsigned int	fg;
	unsigned int	shadow;
	char			fitted[256];
	const char		*label;
	int			tx;
	int			ty;
	int	anchor_x;
	int	anchor_y;

	if (!w || !ui || !ui->theme || !p || !text || !text[0])
		return ;
	box = compute_annotation_box(ui, plot, p, text, dist, prefer_left, prefer_below);
	/*
	 * UX: annotations must never hide the curve.
	 * => transparent background (no filled panel), but keep readability with a
	 * small "halo" shadow.
	 */
	fg = ui_color_lerp(accent, ui->theme->text, 80);
	shadow = ui_color_lerp(ui->theme->bg, 0x000000, 160);
	tx = box.x + 5;
	ty = box.y + 4;
	{
		int max_text_w = box.w - 10;
		if (max_text_w < 1)
			max_text_w = 1;
		label = fit_text_ellipsis(ui, text, fitted, sizeof(fitted), max_text_w, 12);
	}
	/* 1px halo */
	ui_draw_text(w, tx - 1, ty, label, shadow);
	ui_draw_text(w, tx + 1, ty, label, shadow);
	ui_draw_text(w, tx, ty - 1, label, shadow);
	ui_draw_text(w, tx, ty + 1, label, shadow);
	ui_draw_text(w, tx, ty, label, fg);

	/* Leader line from point to the nearest corner of the label */
	anchor_x = box.x + (box.w / 2);
	anchor_y = (box.y > p->y) ? box.y : (box.y + box.h);
	window_draw_line(w, p->x, p->y, anchor_x, anchor_y,
		(int)ui_color_lerp(ui->theme->text2, ui->theme->bg, 160));
}

static void	ui_graph_timeseries_impl(t_window *w, t_ui_state *ui, t_rect r,
					const char *title,
					const double *values,
					const int *x_seconds,
					const int *badge_counts,
					const t_ui_graph_annot *annots,
					int annots_n,
					int n,
					double vmax,
					const char *y_label,
					const char *unit,
					unsigned int line_color)
{
	const int	pad_r = 18;
	const int	pad_t = 48;
	const int	pad_b = 42;
	t_rect		plot;
	unsigned int	grid;
	unsigned int	bd;
	char		buf[96];
	int		gx;
	int		gy;
	int		i;
	int		pad_l;
	int		xmin;
	int		xmax;
	double		ymax;
	double		ystep;
	int		y_ticks;
	int		xstep;
	int		hover_src;
	int		thresh2;
	static t_ui_graph_dsbuf	ds;
	int		poly_n;
	int		rep_n;
	int		hover_poly_i;
	t_point_i	hover_p;
	int		text_w;
	int		tt_w;
	int		tt_h;
	int		tt_x;
	int		tt_y;
	char		lab_time[32];
	char		lab_val[96];
	unsigned int	grid2;
	unsigned int	cross;
	const char	*ylab;
	int		show_seconds;

	if (!w || !ui || !ui->theme)
		return ;
	ui_draw_panel(w, r, ui->theme->surface2, ui->theme->border);
	if (title)
		ui_draw_text(w, r.x + 12, r.y + 10, title, ui->theme->text);

	/* Prepare axis ranges */
	xmin = 0;
	xmax = (n > 0 ? (n - 1) : 0);
	if (n > 0 && x_seconds)
	{
		xmin = x_seconds[0];
		xmax = x_seconds[n - 1];
		i = 0;
		while (i < n)
		{
			if (x_seconds[i] < xmin)
				xmin = x_seconds[i];
			if (x_seconds[i] > xmax)
				xmax = x_seconds[i];
			i++;
		}
	}
	if (xmax < xmin)
	{
		int t = xmin;
		xmin = xmax;
		xmax = t;
	}
	if (xmax == xmin)
		xmax = xmin + 1;

	ymax = vmax;
	if (ymax <= 0.0)
		ymax = 1.0;

	/* Compute a left padding that can fit the largest Y label */
	snprintf(buf, sizeof(buf), "%.2f%s", ymax, unit ? unit : "");
	text_w = ui_measure_text_w(buf, 14);
	pad_l = 14 + text_w + 12;
	if (pad_l < 64)
		pad_l = 64;
	if (pad_l > 110)
		pad_l = 110;

	plot.x = r.x + pad_l;
	plot.y = r.y + pad_t;
	plot.w = r.w - pad_l - pad_r;
	plot.h = r.h - pad_t - pad_b;
	if (plot.w <= 10 || plot.h <= 10)
		return ;

	grid = ui->theme->border;
	grid2 = ui_color_lerp(ui->theme->border, ui->theme->bg, 180);
	bd = ui->theme->text2;
	cross = ui_color_lerp(ui->theme->text2, ui->theme->bg, 140);

	/* Axis titles (no rotation -> keep it readable) */
	ylab = (y_label && y_label[0]) ? y_label : "Valeur";
	if (unit && unit[0])
	{
		const char *u = unit;
		while (*u == ' ')
			u++;
		snprintf(buf, sizeof(buf), "%s (%s)", ylab, u);
	}
	else
		snprintf(buf, sizeof(buf), "%s", ylab);
	ui_draw_text(w, r.x + 12, r.y + 28, buf, bd);
	{
		int tw = ui_measure_text_w("Temps", 14);
		ui_draw_text(w, plot.x + (plot.w / 2) - (tw / 2), plot.y + plot.h + 26,
			"Temps", bd);
	}

	/* Grid + labels: Y */
	ystep = nice_step(0.0, ymax, 5);
	y_ticks = (int)ceil(ymax / ystep);
	if (y_ticks < 1)
		y_ticks = 1;
	if (y_ticks > 10)
		y_ticks = 10;
	i = 0;
	while (i <= y_ticks)
	{
		double v = ystep * (double)i;
		int dec;
		if (v > ymax)
			v = ymax;
		gy = plot.y + plot.h - (int)round((v / ymax) * (double)plot.h);
		gy = clamp_int(gy, plot.y, plot.y + plot.h);
		window_draw_line(w, plot.x, gy, plot.x + plot.w, gy,
			(int)(i == 0 ? grid : grid2));
		dec = 0;
		if (ystep < 1.0)
			dec = 2;
		else if (ystep < 10.0)
			dec = 1;
		if (dec == 0)
			snprintf(buf, sizeof(buf), "%.0f", v);
		else if (dec == 1)
			snprintf(buf, sizeof(buf), "%.1f", v);
		else
			snprintf(buf, sizeof(buf), "%.2f", v);
		ui_draw_text(w, r.x + 12, gy - 7, buf, bd);
		i++;
	}

	/* Grid + labels: X (time) */
	xstep = time_step_seconds(xmax - xmin, 6);
	show_seconds = (xstep < 60);
	{
		int x;
		int ticks_n;
		int max_tw;
		double tick_px;
		int skip;
		int tick_i;
		int last_right;
		const int min_gap = 10;

		/* First pass: estimate label densiy and choose a skip factor */
		ticks_n = 0;
		max_tw = 0;
		x = (xmin / xstep) * xstep;
		if (x < xmin)
			x += xstep;
		while (x <= xmax && ticks_n < 2048)
		{
			int tw;
			fmt_time(buf, sizeof(buf), x, show_seconds);
			tw = ui_measure_text_w(buf, 14);
			if (tw > max_tw)
				max_tw = tw;
			ticks_n++;
			x += xstep;
		}
		tick_px = ((double)plot.w * (double)xstep) / (double)(xmax - xmin);
		skip = 1;
		if (tick_px > 1.0)
		{
			int need_px = max_tw + min_gap * 2;
			if ((double)need_px > tick_px)
				skip = (int)ceil((double)need_px / tick_px);
		}
		if (skip < 1)
			skip = 1;

		/* Second pass: draw ticks and labels with strict anti-overlap */
		tick_i = 0;
		last_right = -100000;
		x = (xmin / xstep) * xstep;
		if (x < xmin)
			x += xstep;
		while (x <= xmax)
		{
			double t;
			int tw;
			int left;
			int right;

			t = (double)(x - xmin) / (double)(xmax - xmin);
			gx = plot.x + (int)round(t * (double)plot.w);
			gx = clamp_int(gx, plot.x, plot.x + plot.w);
			window_draw_line(w, gx, plot.y, gx, plot.y + plot.h, (int)grid2);
			fmt_time(buf, sizeof(buf), x, show_seconds);
			tw = ui_measure_text_w(buf, 14);
			left = gx - (tw / 2);
			right = left + tw;
			/* Label skipping + minimal spacing (uses right edge, not center) */
			if ((tick_i % skip) == 0 && left >= last_right + min_gap)
			{
				ui_draw_text(w, left, plot.y + plot.h + 8, buf, bd);
				last_right = right;
			}
			tick_i++;
			x += xstep;
		}
	}

	/* Series points (supports gaps when values contain NaN) */
	if (!values || n <= 0)
		return ;
	{
		int need_poly;
		int need_rep;
		int valid_n;
		int stride;
		int drawn;
		int marker_sz;
		double avg_dx;
		int idx;
		int is_group;

		need_poly = (plot.w + 1) * 4 + 16;
		need_rep = (plot.w + 1) + 16;
		dsbuf_ensure_points(&ds.poly_pts, &ds.poly_pts_cap, need_poly);
		dsbuf_ensure_ints(&ds.poly_idx, &ds.poly_idx_cap, need_poly);
		dsbuf_ensure_points(&ds.rep_pts, &ds.rep_pts_cap, need_rep);
		dsbuf_ensure_ints(&ds.rep_idx, &ds.rep_idx_cap, need_rep);

		{
			int poly_cap = ds.poly_pts_cap;
			int rep_cap = ds.rep_pts_cap;
			if (ds.poly_idx_cap < poly_cap)
				poly_cap = ds.poly_idx_cap;
			if (ds.rep_idx_cap < rep_cap)
				rep_cap = ds.rep_idx_cap;
			rep_n = 0;
			poly_n = ui_downsample_minmax_pixels(values, x_seconds, n,
				xmin, xmax, 0.0, ymax, plot,
				ds.poly_pts, ds.poly_idx, poly_cap,
				ds.rep_pts, ds.rep_idx, rep_cap, &rep_n);
		}
		if (poly_n <= 0)
			return ;
		if (poly_n >= 2)
			window_draw_polyline(w, ds.poly_pts, poly_n, (int)line_color);

		/* Point markers: keep events visible even when dense (stride-based) */
		valid_n = rep_n;
		stride = 1;
		if (valid_n > 220)
			stride = (valid_n + 219) / 220;
		avg_dx = (valid_n > 1) ? ((double)plot.w / (double)(valid_n - 1)) : 999.0;
		if (avg_dx > 0.1 && avg_dx < 4.0)
		{
			int need = (int)ceil(4.0 / avg_dx);
			if (need > stride)
				stride = need;
		}
		if (stride < 1)
			stride = 1;
		marker_sz = (valid_n <= 120 && stride == 1) ? 5 : 3;
		drawn = 0;
		i = 0;
		while (i < rep_n)
		{
			idx = ds.rep_idx[i];
			if (idx < 0 || idx >= n)
			{
				i++;
				continue ;
			}
			is_group = (badge_counts && badge_counts[idx] > 1);
			drawn++;
			if (is_group || (drawn % stride) == 0)
			{
				window_fill_rect(w,
					ds.rep_pts[i].x - (marker_sz / 2),
					ds.rep_pts[i].y - (marker_sz / 2),
					marker_sz, marker_sz, (int)line_color);
				if (is_group)
				{
					/* stacked marker (2nd square) */
					window_fill_rect(w,
						ds.rep_pts[i].x - (marker_sz / 2) - 2,
						ds.rep_pts[i].y - (marker_sz / 2) - 2,
						marker_sz, marker_sz, (int)line_color);
					(void)plot;
				}
			}
			i++;
		}
	}

	/* Point annotations (kill/loot labels). */
	if (annots && annots_n > 0)
	{
		/*
		 * Anti-collision placement for annotations:
		 * We try multiple label anchors around each point (right/left + above/below)
		 * and increase distance until we find a non-overlapping placement.
		 *
		 * Pro HUD goal: labels must not sit on the curve and must not overlap.
		 */
		enum { STACK_MAX = 4096, STACK_ITER_MAX = 48 };
		t_ui_graph_annot	ordered[STACK_MAX];
		int			ordered_n;
		t_rect			placed_rects[STACK_MAX];
		int			placed_n;
		int			ai;

		ordered_n = annots_n;
		if (ordered_n > STACK_MAX)
			ordered_n = STACK_MAX;
		ai = 0;
		while (ai < ordered_n)
		{
			ordered[ai] = annots[ai];
			ai++;
		}
		/* Most callers build annots in chronological order already.
		 * Only sort if the list is not non-decreasing in time.
		 */
		{
			int need_sort = 0;
			ai = 1;
			while (ai < ordered_n)
			{
				int a_idx = ordered[ai - 1].point_index;
				int b_idx = ordered[ai].point_index;
				int ta = (x_seconds && a_idx >= 0 && a_idx < n) ? x_seconds[a_idx] : a_idx;
				int tb = (x_seconds && b_idx >= 0 && b_idx < n) ? x_seconds[b_idx] : b_idx;
				if (tb < ta)
				{
					need_sort = 1;
					break ;
				}
				ai++;
			}
			if (need_sort)
			{
				ai = 1;
				while (ai < ordered_n)
				{
					t_ui_graph_annot key = ordered[ai];
					int key_idx = key.point_index;
					int key_t = (x_seconds && key_idx >= 0 && key_idx < n)
						? x_seconds[key_idx] : key_idx;
					int j = ai - 1;
					while (j >= 0)
					{
						int j_idx = ordered[j].point_index;
						int j_t = (x_seconds && j_idx >= 0 && j_idx < n)
							? x_seconds[j_idx] : j_idx;
						if (j_t <= key_t)
							break ;
						ordered[j + 1] = ordered[j];
						j--;
					}
					ordered[j + 1] = key;
					ai++;
				}
			}
		}

		placed_n = 0;
		ai = 0;
		while (ai < ordered_n)
		{
			int idx = ordered[ai].point_index;
			t_point_i p;
			unsigned int accent = line_color;
			int dist;
			int prefer_left;
			int prefer_below;
			int iter;
			t_rect box;
			int found;
			int cand;

			if (ordered[ai].kind == UI_GRAPH_ANNOT_KILL)
				accent = ui->theme->success;
			else if (ordered[ai].kind == UI_GRAPH_ANNOT_LOOT)
				accent = ui->theme->warn;
			else if (ordered[ai].kind == UI_GRAPH_ANNOT_VALUE)
				accent = ui_color_lerp(line_color, ui->theme->bg, 170);
			if (!graph_point_for_index(plot, xmin, xmax, ymax,
					values, x_seconds, n, idx, &p))
			{
				ai++;
				continue ;
			}
			dist = 0;
			prefer_left = 0;
			prefer_below = 0;
			iter = 0;
			found = 0;
			box = compute_annotation_box(ui, plot, &p,
				ordered[ai].text, dist, prefer_left, prefer_below);
			while (iter < STACK_ITER_MAX)
			{
				int collided;
				int pj;
				/* Candidate order: (right,above) (left,above) (right,below) (left,below) */
				static const unsigned char cand_lr[4] = {0, 1, 0, 1};
				static const unsigned char cand_bl[4] = {0, 0, 1, 1};

				cand = 0;
				while (cand < 4 && !found)
				{
					prefer_left = cand_lr[cand];
					prefer_below = cand_bl[cand];
					box = compute_annotation_box(ui, plot, &p, ordered[ai].text,
						dist, prefer_left, prefer_below);
					collided = 0;
					pj = 0;
					while (pj < placed_n)
					{
						/* Cheap rejection on X before full intersection test */
						if (placed_rects[pj].x + placed_rects[pj].w < box.x - 6
							|| placed_rects[pj].x > box.x + box.w + 6)
						{
							pj++;
							continue ;
						}
						if (rect_intersects(box, placed_rects[pj]))
						{
							collided = 1;
							break ;
						}
						pj++;
					}
					if (!collided)
						found = 1;
					cand++;
				}
				if (found)
					break ;
				/* Increase distance and retry all anchors */
				dist += 10;
				iter++;
			}
			draw_annotation(w, ui, plot, &p, ordered[ai].text,
				accent, dist, prefer_left, prefer_below);
			if (placed_n < STACK_MAX)
			{
				placed_rects[placed_n] = box;
				placed_n++;
			}
			ai++;
		}
	}

	/* Hover (nearest point) - search on downsampled polyline points */
	hover_poly_i = -1;
	hover_src = -1;
	thresh2 = 9 * 9;
	if (w->mouse_x >= plot.x && w->mouse_x <= plot.x + plot.w
		&& w->mouse_y >= plot.y && w->mouse_y <= plot.y + plot.h)
	{
		i = 0;
		while (i < poly_n)
		{
			int src = ds.poly_idx[i];
			if (src < 0 || src >= n)
			{
				i++;
				continue ;
			}
			int dx = w->mouse_x - ds.poly_pts[i].x;
			int dy = w->mouse_y - ds.poly_pts[i].y;
			int d2 = dx * dx + dy * dy;
			if (d2 <= thresh2)
			{
				hover_poly_i = i;
				hover_src = src;
				thresh2 = d2;
			}
			i++;
		}
	}
	if (hover_src >= 0)
	{
		double v;
		const char *name;
		long iv;
		int is_int;
		char lab_grp[48];
		int lines;
		int grp_count;

		/* crosshair */
		hover_p = ds.poly_pts[hover_poly_i];
		window_draw_line(w, hover_p.x, plot.y,
			hover_p.x, plot.y + plot.h, (int)cross);
		window_draw_line(w, plot.x, hover_p.y,
			plot.x + plot.w, hover_p.y, (int)cross);
		/* highlight point */
		window_fill_rect(w, hover_p.x - 3, hover_p.y - 3, 7, 7,
			(int)line_color);
		window_fill_rect(w, hover_p.x - 2, hover_p.y - 2, 5, 5,
			(int)ui->theme->bg);

		name = (y_label && y_label[0]) ? y_label : "Valeur";
		fmt_time(lab_time, sizeof(lab_time), x_seconds ? x_seconds[hover_src] : hover_src,
			show_seconds);
		snprintf(buf, sizeof(buf), "Temps: %s", lab_time);

		v = values[hover_src];
		iv = (long)llround(v);
		is_int = (fabs(v - (double)iv) < 1e-6);
		if (is_int && (!unit || !unit[0]))
			snprintf(lab_val, sizeof(lab_val), "%s: %ld", name, iv);
		else
			snprintf(lab_val, sizeof(lab_val), "%s: %.2f%s", name, v,
				(unit ? unit : ""));

		grp_count = 0;
		if (badge_counts && hover_src < n)
			grp_count = badge_counts[hover_src];
		lines = (grp_count > 1) ? 3 : 2;
		tt_w = ui_measure_text_w(buf, 14);
		text_w = ui_measure_text_w(lab_val, 14);
		if (text_w > tt_w)
			tt_w = text_w;
		if (lines == 3)
		{
			snprintf(lab_grp, sizeof(lab_grp), "Groupe: x%d", grp_count);
			text_w = ui_measure_text_w(lab_grp, 14);
			if (text_w > tt_w)
				tt_w = text_w;
		}
		tt_w += 16;
		tt_h = 18 * lines + 12;
		tt_x = w->mouse_x + 12;
		tt_y = w->mouse_y + 12;
		if (tt_x + tt_w > r.x + r.w - 6)
			tt_x = w->mouse_x - 12 - tt_w;
		if (tt_y + tt_h > r.y + r.h - 6)
			tt_y = w->mouse_y - 12 - tt_h;
		if (tt_x < r.x + 6)
			tt_x = r.x + 6;
		if (tt_y < r.y + 6)
			tt_y = r.y + 6;
		ui_draw_panel(w, (t_rect){tt_x, tt_y, tt_w, tt_h},
			ui->theme->surface, ui->theme->border);
		ui_draw_text(w, tt_x + 8, tt_y + 8, buf, ui->theme->text);
		ui_draw_text(w, tt_x + 8, tt_y + 8 + 18, lab_val, ui->theme->text2);
		if (lines == 3)
			ui_draw_text(w, tt_x + 8, tt_y + 8 + 18 * 2, lab_grp, ui->theme->text2);
	}
}

/* -------------------------------------------------------------------------- */
/*  Overview + Zoom                                                           */
/* -------------------------------------------------------------------------- */

void	ui_graph_zoom_reset(t_ui_graph_zoom *z)
{
	if (!z)
		return ;
	z->t0 = 0;
	z->t1 = 0;
	z->mode = UI_GRAPH_ZOOM_NONE;
	z->drag_start_x = 0;
	z->drag_t0 = 0;
	z->drag_t1 = 0;
	z->last_click_ms = 0;
}

static int	map_time_to_px(t_rect plot, int xmin, int xmax, int t)
{
	double	xn;
	if (xmax == xmin)
		xmax = xmin + 1;
	xn = ((double)(t - xmin)) / (double)(xmax - xmin);
	if (xn < 0.0)
		xn = 0.0;
	if (xn > 1.0)
		xn = 1.0;
	return (plot.x + (int)llround(xn * (double)plot.w));
}

static int	map_px_to_time(t_rect plot, int xmin, int xmax, int px)
{
	double	xn;
	if (xmax == xmin)
		xmax = xmin + 1;
	xn = ((double)(px - plot.x)) / (double)plot.w;
	if (xn < 0.0)
		xn = 0.0;
	if (xn > 1.0)
		xn = 1.0;
	return (xmin + (int)llround(xn * (double)(xmax - xmin)));
}

static double	max_in_range(const double *values, const int *x_seconds, int n,
				int t0, int t1)
{
	double	m;
	int		i;

	if (!values || n <= 0)
		return (1.0);
	if (t1 < t0)
	{
		int tmp = t0;
		t0 = t1;
		t1 = tmp;
	}
	m = 0.0;
	i = 0;
	while (i < n)
	{
		double v = values[i];
		double xv = x_seconds ? (double)x_seconds[i] : (double)i;
		if (!tm_isnan(v) && xv >= (double)t0 && xv <= (double)t1)
		{
			if (v > m)
				m = v;
		}
		i++;
	}
	if (m <= 0.0)
		m = 1.0;
	return (m);
}

static void	update_zoom_from_overview(t_window *w, t_rect ov_plot,
				int full_xmin, int full_xmax, t_ui_graph_zoom *zoom)
{
	const int	handle_px = 8;
	const int	dbl_ms = 350;
	int		min_w;
	int		zx0;
	int		zx1;
	int		inside;
	uint64_t	now;

	if (!w || !zoom)
		return ;
	if (full_xmax == full_xmin)
		full_xmax = full_xmin + 1;
	if (zoom->t1 <= zoom->t0)
	{
		zoom->t0 = full_xmin;
		zoom->t1 = full_xmax;
	}
	if (zoom->t0 < full_xmin)
		zoom->t0 = full_xmin;
	if (zoom->t1 > full_xmax)
		zoom->t1 = full_xmax;
	if (zoom->t1 <= zoom->t0)
		zoom->t1 = zoom->t0 + 1;
	min_w = 10;
	if (min_w > (full_xmax - full_xmin))
		min_w = 1;

	/* Drag end */
	if (!w->mouse_left_down && zoom->mode != UI_GRAPH_ZOOM_NONE)
		zoom->mode = UI_GRAPH_ZOOM_NONE;

	/* Drag update */
	if (w->mouse_left_down && zoom->mode != UI_GRAPH_ZOOM_NONE)
	{
		if (zoom->mode == UI_GRAPH_ZOOM_MOVE)
		{
			double dt = ((double)(w->mouse_x - zoom->drag_start_x)
				/ (double)ov_plot.w) * (double)(full_xmax - full_xmin);
			int width = zoom->drag_t1 - zoom->drag_t0;
			int nt0 = zoom->drag_t0 + (int)llround(dt);
			int nt1 = nt0 + width;
			if (nt0 < full_xmin)
			{
				nt0 = full_xmin;
				nt1 = nt0 + width;
			}
			if (nt1 > full_xmax)
			{
				nt1 = full_xmax;
				nt0 = nt1 - width;
				if (nt0 < full_xmin)
					nt0 = full_xmin;
			}
			zoom->t0 = nt0;
			zoom->t1 = nt1;
		}
		else if (zoom->mode == UI_GRAPH_ZOOM_RESIZE_L)
		{
			int nt0 = map_px_to_time(ov_plot, full_xmin, full_xmax, w->mouse_x);
			if (nt0 < full_xmin)
				nt0 = full_xmin;
			if (nt0 > zoom->t1 - min_w)
				nt0 = zoom->t1 - min_w;
			zoom->t0 = nt0;
		}
		else if (zoom->mode == UI_GRAPH_ZOOM_RESIZE_R)
		{
			int nt1 = map_px_to_time(ov_plot, full_xmin, full_xmax, w->mouse_x);
			if (nt1 > full_xmax)
				nt1 = full_xmax;
			if (nt1 < zoom->t0 + min_w)
				nt1 = zoom->t0 + min_w;
			zoom->t1 = nt1;
		}
		return ;
	}

	/* Click start */
	if (!w->mouse_left_click)
		return ;
	if (w->mouse_x < ov_plot.x || w->mouse_x > ov_plot.x + ov_plot.w
		|| w->mouse_y < ov_plot.y || w->mouse_y > ov_plot.y + ov_plot.h)
		return ;

	now = ft_time_ms();
	if (zoom->last_click_ms && (now - zoom->last_click_ms) <= (uint64_t)dbl_ms)
	{
		zoom->t0 = full_xmin;
		zoom->t1 = full_xmax;
		zoom->mode = UI_GRAPH_ZOOM_NONE;
		zoom->last_click_ms = 0;
		return ;
	}
	zoom->last_click_ms = now;

	zx0 = map_time_to_px(ov_plot, full_xmin, full_xmax, zoom->t0);
	zx1 = map_time_to_px(ov_plot, full_xmin, full_xmax, zoom->t1);
	if (zx1 < zx0)
	{
		int t = zx0;
		zx0 = zx1;
		zx1 = t;
	}
	inside = (w->mouse_x >= zx0 && w->mouse_x <= zx1);

	/* Handle grab */
	if (abs(w->mouse_x - zx0) <= handle_px)
		zoom->mode = UI_GRAPH_ZOOM_RESIZE_L;
	else if (abs(w->mouse_x - zx1) <= handle_px)
		zoom->mode = UI_GRAPH_ZOOM_RESIZE_R;
	else if (inside)
		zoom->mode = UI_GRAPH_ZOOM_MOVE;
	else
		zoom->mode = UI_GRAPH_ZOOM_NONE;

	if (zoom->mode == UI_GRAPH_ZOOM_MOVE)
	{
		zoom->drag_start_x = w->mouse_x;
		zoom->drag_t0 = zoom->t0;
		zoom->drag_t1 = zoom->t1;
		return ;
	}

	/* Click outside window => re-center window around clicked time */
	if (!inside)
	{
		int width = zoom->t1 - zoom->t0;
		int center = map_px_to_time(ov_plot, full_xmin, full_xmax, w->mouse_x);
		int nt0 = center - (width / 2);
		int nt1 = nt0 + width;
		if (width < min_w)
			width = min_w;
		if (nt0 < full_xmin)
		{
			nt0 = full_xmin;
			nt1 = nt0 + width;
		}
		if (nt1 > full_xmax)
		{
			nt1 = full_xmax;
			nt0 = nt1 - width;
			if (nt0 < full_xmin)
				nt0 = full_xmin;
		}
		zoom->t0 = nt0;
		zoom->t1 = nt1;
	}
}

static void	ui_graph_timeseries_zoom_impl(t_window *w, t_ui_state *ui, t_rect r,
					const char *title,
					const double *values,
					const int *x_seconds,
					const int *badge_counts,
					const t_ui_graph_annot *annots,
					int annots_n,
					int n,
					double vmax_in,
					const char *y_label,
					const char *unit,
					unsigned int line_color,
					t_ui_graph_zoom *zoom,
					uint32_t series_version)
{
	const int	pad_r = 18;
	const int	pad_t = 48;
	const int	x_axis_area = 30;
	const int	ov_gap = 10;
	int		ov_h;
	const int	pad_b = 14;
	t_rect		plot;
	t_rect		ov_plot;
	unsigned int	grid;
	unsigned int	bd;
	unsigned int	grid2;
	unsigned int	cross;
	char		buf[96];
	int		pad_l;
	int		xmin_full;
	int		xmax_full;
	int		xmin;
	int		xmax;
	double		ymax_full;
	double		ymax;
	double		ystep;
	int		y_ticks;
	int		xstep;
	int		i;
	int		gx;
	int		gy;
	int		show_seconds;
	int		text_w;
	const char	*ylab;
	static t_ui_graph_cache	cache_main;
	static t_ui_graph_cache	cache_ov;
	int		hover_src;
	int		hover_poly_i;
	int		thresh2;
	t_point_i	hover_p;
	int		tt_w;
	int		tt_h;
	int		tt_x;
	int		tt_y;
	char		lab_time[32];
	char		lab_val[96];
	int		marker_sz;

	if (!w || !ui || !ui->theme || !values || n <= 0)
		return ;
	ui_draw_panel(w, r, ui->theme->surface2, ui->theme->border);
	if (title)
		ui_draw_text(w, r.x + 12, r.y + 10, title, ui->theme->text);

	/* Full X range */
	xmin_full = 0;
	xmax_full = (n > 0 ? (n - 1) : 0);
	if (x_seconds)
	{
		xmin_full = x_seconds[0];
		xmax_full = x_seconds[n - 1];
		i = 0;
		while (i < n)
		{
			if (x_seconds[i] < xmin_full)
				xmin_full = x_seconds[i];
			if (x_seconds[i] > xmax_full)
				xmax_full = x_seconds[i];
			i++;
		}
	}
	if (xmax_full < xmin_full)
	{
		int t = xmin_full;
		xmin_full = xmax_full;
		xmax_full = t;
	}
	if (xmax_full == xmin_full)
		xmax_full = xmin_full + 1;

	/*
	 * Layout: keep the overview even on compact panels.
	 *
	 * On small screens (or when the right panel is scrolled/clipped), the visible
	 * graph height can dip below the old threshold (190px), which made the
	 * mini overview disappear completely.
	 *
	 * We lower the threshold slightly and use a smaller overview height for
	 * compact graphs so the overview remains usable.
	 */
	if (r.h < 170)
		ov_h = 0;
	else if (r.h < 220)
		ov_h = 50;
	else if (r.h < 260)
		ov_h = 60;
	else
		ov_h = 80;
	if (ov_h > 0)
	{
		/* Left padding needs to fit the largest Y label (use full vmax). */
		ymax_full = (vmax_in <= 0.0) ? max_in_range(values, x_seconds, n,
				xmin_full, xmax_full) : vmax_in;
		snprintf(buf, sizeof(buf), "%.2f%s", ymax_full, unit ? unit : "");
		text_w = ui_measure_text_w(buf, 14);
		pad_l = 14 + text_w + 12;
		if (pad_l < 64)
			pad_l = 64;
		if (pad_l > 110)
			pad_l = 110;

		plot.x = r.x + pad_l;
		plot.y = r.y + pad_t;
		plot.w = r.w - pad_l - pad_r;
		plot.h = r.h - pad_t - pad_b - x_axis_area - ov_gap - ov_h;
		ov_plot.x = plot.x;
		ov_plot.y = plot.y + plot.h + x_axis_area + ov_gap;
		ov_plot.w = plot.w;
		ov_plot.h = ov_h;
		if (plot.w <= 10 || plot.h <= 10 || ov_plot.h <= 10)
			ov_h = 0;
	}
	if (ov_h <= 0)
	{
		/* Old behavior if the widget is too small. */
		ui_graph_timeseries_impl(w, ui, r, title, values, x_seconds,
			badge_counts, annots, annots_n, n, vmax_in, y_label, unit, line_color);
		return ;
	}

	/* Zoom defaults */
	if (!zoom)
	{
		static t_ui_graph_zoom z;
		zoom = &z;
	}
	if (zoom->t0 == 0 && zoom->t1 == 0)
	{
		zoom->t0 = xmin_full;
		zoom->t1 = xmax_full;
	}
	update_zoom_from_overview(w, ov_plot, xmin_full, xmax_full, zoom);
	xmin = zoom->t0;
	xmax = zoom->t1;
	if (xmax <= xmin)
		xmax = xmin + 1;

	/* Y range */
	ymax_full = (vmax_in <= 0.0) ? max_in_range(values, x_seconds, n,
			xmin_full, xmax_full) : vmax_in;
	ymax = max_in_range(values, x_seconds, n, xmin, xmax);
	if (ymax <= 0.0)
		ymax = 1.0;

	grid = ui->theme->border;
	grid2 = ui_color_lerp(ui->theme->border, ui->theme->bg, 180);
	bd = ui->theme->text2;
	cross = ui_color_lerp(ui->theme->text2, ui->theme->bg, 140);

	/* Axis titles */
	ylab = (y_label && y_label[0]) ? y_label : "Valeur";
	if (unit && unit[0])
	{
		const char *u = unit;
		while (*u == ' ')
			u++;
		snprintf(buf, sizeof(buf), "%s (%s)", ylab, u);
	}
	else
		snprintf(buf, sizeof(buf), "%s", ylab);
	ui_draw_text(w, r.x + 12, r.y + 28, buf, bd);

	/* Y grid + labels */
	ystep = nice_step(0.0, ymax, 5);
	y_ticks = (int)ceil(ymax / ystep);
	if (y_ticks < 1)
		y_ticks = 1;
	if (y_ticks > 10)
		y_ticks = 10;
	i = 0;
	while (i <= y_ticks)
	{
		double v = ystep * (double)i;
		int dec;
		if (v > ymax)
			v = ymax;
		gy = plot.y + plot.h - (int)round((v / ymax) * (double)plot.h);
		gy = clamp_int(gy, plot.y, plot.y + plot.h);
		window_draw_line(w, plot.x, gy, plot.x + plot.w, gy,
			(int)(i == 0 ? grid : grid2));
		dec = 0;
		if (ystep < 1.0)
			dec = 2;
		else if (ystep < 10.0)
			dec = 1;
		if (dec == 0)
			snprintf(buf, sizeof(buf), "%.0f", v);
		else if (dec == 1)
			snprintf(buf, sizeof(buf), "%.1f", v);
		else
			snprintf(buf, sizeof(buf), "%.2f", v);
		ui_draw_text(w, r.x + 12, gy - 7, buf, bd);
		i++;
	}

	/* X grid + labels (main plot) */
	xstep = time_step_seconds(xmax - xmin, 6);
	show_seconds = (xstep < 60);
	{
		int x;
		int ticks_n;
		int max_tw;
		double tick_px;
		int skip;
		int tick_i;
		int last_right;
		const int min_gap = 10;

		ticks_n = 0;
		max_tw = 0;
		x = (xmin / xstep) * xstep;
		if (x < xmin)
			x += xstep;
		while (x <= xmax && ticks_n < 2048)
		{
			int tw;
			fmt_time(buf, sizeof(buf), x, show_seconds);
			tw = ui_measure_text_w(buf, 14);
			if (tw > max_tw)
				max_tw = tw;
			ticks_n++;
			x += xstep;
		}
		tick_px = ((double)plot.w * (double)xstep) / (double)(xmax - xmin);
		skip = 1;
		if (tick_px > 1.0)
		{
			int need_px = max_tw + min_gap * 2;
			if ((double)need_px > tick_px)
				skip = (int)ceil((double)need_px / tick_px);
		}
		if (skip < 1)
			skip = 1;

		tick_i = 0;
		last_right = -100000;
		x = (xmin / xstep) * xstep;
		if (x < xmin)
			x += xstep;
		while (x <= xmax)
		{
			double t = (double)(x - xmin) / (double)(xmax - xmin);
			int tw;
			int left;
			int right;

			gx = plot.x + (int)round(t * (double)plot.w);
			gx = clamp_int(gx, plot.x, plot.x + plot.w);
			window_draw_line(w, gx, plot.y, gx, plot.y + plot.h, (int)grid2);
			fmt_time(buf, sizeof(buf), x, show_seconds);
			tw = ui_measure_text_w(buf, 14);
			left = gx - (tw / 2);
			right = left + tw;
			if ((tick_i % skip) == 0 && left >= last_right + min_gap)
			{
				ui_draw_text(w, left, plot.y + plot.h + 8, buf, bd);
				last_right = right;
			}
			tick_i++;
			x += xstep;
		}
	}
	{
		int tw = ui_measure_text_w("Temps", 14);
		ui_draw_text(w, plot.x + (plot.w / 2) - (tw / 2), plot.y + plot.h + 26,
			"Temps", bd);
	}

	/* Main series (cached downsample) */
	ui_graph_cache_compute(&cache_main, values, x_seconds, n,
		xmin, xmax, 0.0, ymax, plot, series_version);
	if (cache_main.poly_n >= 2)
		window_draw_polyline(w, cache_main.buf.poly_pts, cache_main.poly_n,
			(int)line_color);

	/* Markers */
	marker_sz = (cache_main.rep_n <= 120) ? 5 : 3;
	i = 0;
	while (i < cache_main.rep_n)
	{
		int idx = cache_main.buf.rep_idx[i];
		int is_group = (badge_counts && idx >= 0 && idx < n
			&& badge_counts[idx] > 1);
		window_fill_rect(w,
			cache_main.buf.rep_pts[i].x - (marker_sz / 2),
			cache_main.buf.rep_pts[i].y - (marker_sz / 2),
			marker_sz, marker_sz, (int)line_color);
		if (is_group)
		{
			window_fill_rect(w,
				cache_main.buf.rep_pts[i].x - (marker_sz / 2) - 2,
				cache_main.buf.rep_pts[i].y - (marker_sz / 2) - 2,
				marker_sz, marker_sz, (int)line_color);
		}
		i++;
	}

	/* Point annotations */
	if (annots && annots_n > 0)
	{
		int ai = 0;
		while (ai < annots_n)
		{
			int idx = annots[ai].point_index;
			t_point_i p;
			unsigned int accent = line_color;
			if (annots[ai].kind == UI_GRAPH_ANNOT_KILL)
				accent = ui->theme->success;
			else if (annots[ai].kind == UI_GRAPH_ANNOT_LOOT)
				accent = ui->theme->warn;
			else if (annots[ai].kind == UI_GRAPH_ANNOT_VALUE)
				accent = ui_color_lerp(line_color, ui->theme->bg, 170);
			if (!graph_point_for_index(plot, xmin, xmax, ymax,
					values, x_seconds, n, idx, &p))
			{
				ai++;
				continue ;
			}
			draw_annotation(w, ui, plot, &p, annots[ai].text,
				accent, 0, 0, 0);
			ai++;
		}
	}

	/* Hover (nearest point) */
	hover_poly_i = -1;
	hover_src = -1;
	thresh2 = 9 * 9;
	if (w->mouse_x >= plot.x && w->mouse_x <= plot.x + plot.w
		&& w->mouse_y >= plot.y && w->mouse_y <= plot.y + plot.h)
	{
		i = 0;
		while (i < cache_main.poly_n)
		{
			int src = cache_main.buf.poly_idx[i];
			int dx = w->mouse_x - cache_main.buf.poly_pts[i].x;
			int dy = w->mouse_y - cache_main.buf.poly_pts[i].y;
			int d2 = dx * dx + dy * dy;
			if (src >= 0 && src < n && d2 <= thresh2)
			{
				hover_poly_i = i;
				hover_src = src;
				thresh2 = d2;
			}
			i++;
		}
	}
	if (hover_src >= 0)
	{
		double v = values[hover_src];
		const char *name = (y_label && y_label[0]) ? y_label : "Valeur";
		long iv = (long)llround(v);
		int is_int = (fabs(v - (double)iv) < 1e-6);
		int grp_count = 0;

		hover_p = cache_main.buf.poly_pts[hover_poly_i];
		window_draw_line(w, hover_p.x, plot.y, hover_p.x, plot.y + plot.h,
			(int)cross);
		window_draw_line(w, plot.x, hover_p.y, plot.x + plot.w, hover_p.y,
			(int)cross);
		window_fill_rect(w, hover_p.x - 3, hover_p.y - 3, 7, 7, (int)line_color);
		window_fill_rect(w, hover_p.x - 2, hover_p.y - 2, 5, 5,
			(int)ui->theme->bg);

		fmt_time(lab_time, sizeof(lab_time),
			x_seconds ? x_seconds[hover_src] : hover_src, show_seconds);
		snprintf(buf, sizeof(buf), "Temps: %s", lab_time);
		if (is_int && (!unit || !unit[0]))
			snprintf(lab_val, sizeof(lab_val), "%s: %ld", name, iv);
		else
			snprintf(lab_val, sizeof(lab_val), "%s: %.2f%s", name, v,
				(unit ? unit : ""));
		if (badge_counts)
			grp_count = badge_counts[hover_src];

		tt_w = ui_measure_text_w(buf, 14);
		text_w = ui_measure_text_w(lab_val, 14);
		if (text_w > tt_w)
			tt_w = text_w;
		if (grp_count > 1)
		{
			snprintf(buf + 64, sizeof(buf) - 64, "Groupe: x%d", grp_count);
			text_w = ui_measure_text_w(buf + 64, 14);
			if (text_w > tt_w)
				tt_w = text_w;
		}
		tt_w += 16;
		tt_h = 18 * ((grp_count > 1) ? 3 : 2) + 12;
		tt_x = w->mouse_x + 12;
		tt_y = w->mouse_y + 12;
		if (tt_x + tt_w > r.x + r.w - 6)
			tt_x = w->mouse_x - 12 - tt_w;
		if (tt_y + tt_h > r.y + r.h - 6)
			tt_y = w->mouse_y - 12 - tt_h;
		if (tt_x < r.x + 6)
			tt_x = r.x + 6;
		if (tt_y < r.y + 6)
			tt_y = r.y + 6;
		ui_draw_panel(w, (t_rect){tt_x, tt_y, tt_w, tt_h},
			ui->theme->surface, ui->theme->border);
		ui_draw_text(w, tt_x + 8, tt_y + 8, buf, ui->theme->text);
		ui_draw_text(w, tt_x + 8, tt_y + 8 + 18, lab_val, ui->theme->text2);
		if (grp_count > 1)
			ui_draw_text(w, tt_x + 8, tt_y + 8 + 18 * 2, buf + 64,
				ui->theme->text2);
	}

	/* Overview */
	{
		unsigned int ov_line = ui_color_lerp(line_color, ui->theme->bg, 160);
		unsigned int zoom_bd = 0xA6B0BF;
		unsigned int zoom_fill = ui_color_lerp(zoom_bd, ui->theme->bg, 200);
		int zx0 = map_time_to_px(ov_plot, xmin_full, xmax_full, zoom->t0);
		int zx1 = map_time_to_px(ov_plot, xmin_full, xmax_full, zoom->t1);
		if (zx1 < zx0)
		{
			int t = zx0;
			zx0 = zx1;
			zx1 = t;
		}

		ui_draw_panel(w, (t_rect){ov_plot.x - 2, ov_plot.y - 2, ov_plot.w + 4, ov_plot.h + 4},
			ui->theme->surface, ui->theme->border);
		ui_graph_cache_compute(&cache_ov, values, x_seconds, n,
			xmin_full, xmax_full, 0.0, ymax_full, ov_plot, series_version);
		if (cache_ov.poly_n >= 2)
			window_draw_polyline(w, cache_ov.buf.poly_pts, cache_ov.poly_n,
				(int)ov_line);

		/* Zoom window rectangle (semi-transparent approximation). */
		window_fill_rect(w, zx0, ov_plot.y, zx1 - zx0, ov_plot.h, (int)zoom_fill);
		window_draw_line(w, zx0, ov_plot.y, zx1, ov_plot.y, (int)zoom_bd);
		window_draw_line(w, zx0, ov_plot.y + ov_plot.h, zx1, ov_plot.y + ov_plot.h, (int)zoom_bd);
		window_draw_line(w, zx0, ov_plot.y, zx0, ov_plot.y + ov_plot.h, (int)zoom_bd);
		window_draw_line(w, zx1, ov_plot.y, zx1, ov_plot.y + ov_plot.h, (int)zoom_bd);
		/* Handles */
		window_draw_line(w, zx0, ov_plot.y, zx0, ov_plot.y + ov_plot.h, (int)ui->theme->text);
		window_draw_line(w, zx1, ov_plot.y, zx1, ov_plot.y + ov_plot.h, (int)ui->theme->text);

		ui_draw_text(w, ov_plot.x, ov_plot.y - 18,
			"Overview (drag/resize, double-click reset)", bd);
	}
}

void	ui_graph_timeseries_zoom(t_window *w, t_ui_state *ui, t_rect r,
				const char *title,
				const double *values,
				const int *x_seconds,
				int n,
				double vmax,
				const char *y_label,
				const char *unit,
				unsigned int line_color,
				t_ui_graph_zoom *zoom,
				uint32_t series_version)
{
	ui_graph_timeseries_zoom_impl(w, ui, r, title, values, x_seconds, NULL,
		NULL, 0, n, vmax, y_label, unit, line_color, zoom, series_version);
}

void	ui_graph_timeseries_zoom_badges(t_window *w, t_ui_state *ui, t_rect r,
				const char *title,
				const double *values,
				const int *x_seconds,
				const int *badge_counts,
				int n,
				double vmax,
				const char *y_label,
				const char *unit,
				unsigned int line_color,
				t_ui_graph_zoom *zoom,
				uint32_t series_version)
{
	ui_graph_timeseries_zoom_impl(w, ui, r, title, values, x_seconds,
		badge_counts, NULL, 0, n, vmax, y_label, unit, line_color, zoom,
		series_version);
}

void	ui_graph_timeseries_zoom_annotations(t_window *w, t_ui_state *ui, t_rect r,
				const char *title,
				const double *values,
				const int *x_seconds,
				int n,
				double vmax,
				const char *y_label,
				const char *unit,
				unsigned int line_color,
				const t_ui_graph_annot *annots,
				int annots_n,
				t_ui_graph_zoom *zoom,
				uint32_t series_version)
{
	ui_graph_timeseries_zoom_impl(w, ui, r, title, values, x_seconds, NULL,
		annots, annots_n, n, vmax, y_label, unit, line_color, zoom, series_version);
}

void	ui_graph_timeseries_zoom_badges_annotations(t_window *w, t_ui_state *ui, t_rect r,
				const char *title,
				const double *values,
				const int *x_seconds,
				const int *badge_counts,
				int n,
				double vmax,
				const char *y_label,
				const char *unit,
				unsigned int line_color,
				const t_ui_graph_annot *annots,
				int annots_n,
				t_ui_graph_zoom *zoom,
				uint32_t series_version)
{
	ui_graph_timeseries_zoom_impl(w, ui, r, title, values, x_seconds,
		badge_counts, annots, annots_n, n, vmax, y_label, unit, line_color,
		zoom, series_version);
}

void	ui_graph_timeseries(t_window *w, t_ui_state *ui, t_rect r,
				const char *title,
				const double *values,
				const int *x_seconds,
				int n,
				double vmax,
				const char *y_label,
				const char *unit,
				unsigned int line_color)
{
	ui_graph_timeseries_impl(w, ui, r, title, values, x_seconds, NULL, NULL, 0, n,
		vmax, y_label, unit, line_color);
}

void	ui_graph_timeseries_badges(t_window *w, t_ui_state *ui, t_rect r,
				const char *title,
				const double *values,
				const int *x_seconds,
				const int *badge_counts,
				int n,
				double vmax,
				const char *y_label,
				const char *unit,
				unsigned int line_color)
{
	ui_graph_timeseries_impl(w, ui, r, title, values, x_seconds, badge_counts, NULL, 0, n,
		vmax, y_label, unit, line_color);
}


void	ui_graph_timeseries_annotations(t_window *w, t_ui_state *ui, t_rect r,
					const char *title,
					const double *values,
					const int *x_seconds,
					int n,
					double vmax,
					const char *y_label,
					const char *unit,
					unsigned int line_color,
					const t_ui_graph_annot *annots,
					int annots_n)
{
	ui_graph_timeseries_impl(w, ui, r, title, values, x_seconds, NULL,
		annots, annots_n, n, vmax, y_label, unit, line_color);
}

void	ui_graph_timeseries_badges_annotations(t_window *w, t_ui_state *ui, t_rect r,
					const char *title,
					const double *values,
					const int *x_seconds,
					const int *badge_counts,
					int n,
					double vmax,
					const char *y_label,
					const char *unit,
					unsigned int line_color,
					const t_ui_graph_annot *annots,
					int annots_n)
{
	ui_graph_timeseries_impl(w, ui, r, title, values, x_seconds, badge_counts,
		annots, annots_n, n, vmax, y_label, unit, line_color);
}
