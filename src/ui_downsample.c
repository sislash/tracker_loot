#include "ui_downsample.h"

#include <math.h>
#include <stdlib.h>

typedef struct s_ui_bucket
{
	int	valid;
	int	has_first;
	int	has_last;

	int	y_first;
	int	y_last;
	int	y_min;
	int	y_max;

	int	idx_first;
	int	idx_last;
	int	idx_min;
	int	idx_max;
} 	t_ui_bucket;

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

int	ui_downsample_minmax_pixels(
		const double *values,
		const int *x_seconds,
		int n,
		int xmin,
		int xmax,
		double vmin,
		double vmax,
		t_rect plot,
		t_point_i *poly_pts,
		int *poly_idx,
		int poly_cap,
		t_point_i *rep_pts,
		int *rep_idx,
		int rep_cap,
		int *out_rep_n)
{
	static t_ui_bucket	*buckets;
	static int			buckets_cap;
	int					px_w;
	int					px_h;
	double					dt;
	double					dv;
	int					i;
	int					poly_n;
	int					rep_n;

	if (out_rep_n)
		*out_rep_n = 0;
	if (!values || n <= 0 || !poly_pts || !poly_idx || poly_cap <= 0)
		return (0);
	px_w = plot.w + 1;
	px_h = plot.h + 1;
	if (px_w <= 2 || px_h <= 2)
		return (0);
	if (xmin == xmax)
		xmax = xmin + 1;
	dt = (double)(xmax - xmin);
	dv = (double)(vmax - vmin);
	if (dt <= 0.0 || dv <= 0.0)
		return (0);

	if (buckets_cap < px_w)
	{
		buckets = (t_ui_bucket *)realloc(buckets, sizeof(*buckets) * (size_t)px_w);
		buckets_cap = px_w;
	}
	i = 0;
	while (i < px_w)
	{
		buckets[i].valid = 0;
		buckets[i].has_first = 0;
		buckets[i].has_last = 0;
		i++;
	}

	i = 0;
	while (i < n)
	{
		double	v;
		double	xv;
		int		col;
		int		py;
		t_ui_bucket	*b;

		v = values[i];
		if (tm_isnan(v))
		{
			i++;
			continue ;
		}
		if (v < vmin)
			v = vmin;
		if (v > vmax)
			v = vmax;
		if (x_seconds)
			xv = (double)x_seconds[i];
		else
			xv = (double)i;
		/*
		** Critical for zoom correctness:
		** points outside [xmin..xmax] must be ignored (not clamped to edges).
		*/
		if (xv < (double)xmin || xv > (double)xmax)
		{
			i++;
			continue ;
		}
		col = (int)floor(((xv - (double)xmin) / dt) * (double)(px_w - 1) + 0.5);
		col = clamp_int(col, 0, px_w - 1);
		py = plot.y + plot.h - (int)llround(((v - vmin) / dv) * (double)plot.h);
		py = clamp_int(py, plot.y, plot.y + plot.h);

		b = &buckets[col];
		if (!b->valid)
		{
			b->valid = 1;
			b->y_first = py;
			b->y_last = py;
			b->y_min = py;
			b->y_max = py;
			b->idx_first = i;
			b->idx_last = i;
			b->idx_min = i;
			b->idx_max = i;
			b->has_first = 1;
			b->has_last = 1;
		}
		else
		{
			if (py < b->y_min)
			{
				b->y_min = py;
				b->idx_min = i;
			}
			if (py > b->y_max)
			{
				b->y_max = py;
				b->idx_max = i;
			}
			b->y_last = py;
			b->idx_last = i;
			b->has_last = 1;
		}
		i++;
	}

	poly_n = 0;
	rep_n = 0;
	i = 0;
	while (i < px_w)
	{
		t_ui_bucket	*b;
		int			x;

		b = &buckets[i];
		if (!b->valid)
		{
			i++;
			continue ;
		}
		x = plot.x + i;

		/* Representative: last point (for markers) */
		if (rep_pts && rep_idx && rep_n < rep_cap)
		{
			rep_pts[rep_n] = (t_point_i){x, b->y_last};
			rep_idx[rep_n] = b->idx_last;
			rep_n++;
		}

		/* Polyline: first -> min -> max -> last (avoid duplicates) */
		if (poly_n < poly_cap)
		{
			poly_pts[poly_n] = (t_point_i){x, b->y_first};
			poly_idx[poly_n] = b->idx_first;
			poly_n++;
		}
		if (b->y_min != b->y_first && poly_n < poly_cap)
		{
			poly_pts[poly_n] = (t_point_i){x, b->y_min};
			poly_idx[poly_n] = b->idx_min;
			poly_n++;
		}
		if (b->y_max != b->y_min && poly_n < poly_cap)
		{
			poly_pts[poly_n] = (t_point_i){x, b->y_max};
			poly_idx[poly_n] = b->idx_max;
			poly_n++;
		}
		if (b->y_last != b->y_max && poly_n < poly_cap)
		{
			poly_pts[poly_n] = (t_point_i){x, b->y_last};
			poly_idx[poly_n] = b->idx_last;
			poly_n++;
		}
		i++;
	}

	if (out_rep_n)
		*out_rep_n = rep_n;
	return (poly_n);
}
