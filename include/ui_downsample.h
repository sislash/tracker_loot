/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ui_downsample.h                                    :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: tracker_loot                                   +#+  +:+       +#+    */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/02/13                                 #+#    #+#           */
/*   Updated: 2026/02/13                                 #+#    #+#           */
/*                                                                            */
/* ************************************************************************** */

#ifndef UI_DOWNSAMPLE_H
# define UI_DOWNSAMPLE_H

# include "ui_layout.h" /* t_rect */
# include "window.h"    /* t_point_i */

/*
** Pixel-column min/max downsampling (deterministic, O(n)).
**
** Goal: keep peaks/valleys visually accurate on long sessions by preserving
** extrema per screen column.
**
** - poly_pts/poly_idx: output polyline points (first -> min -> max -> last)
** - rep_pts/rep_idx: one representative point per column (the last point)
**   useful for markers and sparse hover fallback.
**
** Returns: number of points written in poly_pts (polyline).
*/
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
		int *out_rep_n);

#endif
