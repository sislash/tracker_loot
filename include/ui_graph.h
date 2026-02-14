#ifndef UI_GRAPH_H
# define UI_GRAPH_H

# include "ui_widgets.h"

# include <stdint.h>


typedef enum e_ui_graph_annot_kind
{
	UI_GRAPH_ANNOT_KILL = 1,
	UI_GRAPH_ANNOT_LOOT = 2,
	/* Generic per-point value label (discreet). */
	UI_GRAPH_ANNOT_VALUE = 3
} 	t_ui_graph_annot_kind;

typedef struct s_ui_graph_annot
{
	int				point_index;
	t_ui_graph_annot_kind	kind;
	const char			*text;
} 	t_ui_graph_annot;

/* -------------------------------------------------------------------------- */
/*  Overview + Zoom state                                                     */
/* -------------------------------------------------------------------------- */

typedef enum e_ui_graph_zoom_mode
{
	UI_GRAPH_ZOOM_NONE = 0,
	UI_GRAPH_ZOOM_MOVE = 1,
	UI_GRAPH_ZOOM_RESIZE_L = 2,
	UI_GRAPH_ZOOM_RESIZE_R = 3
} 	t_ui_graph_zoom_mode;

typedef struct s_ui_graph_zoom
{
	/* X window in "x_seconds" space (seconds since session start) */
	int		t0;
	int		t1;

	/* Drag interaction (overview) */
	t_ui_graph_zoom_mode	mode;
	int		drag_start_x;
	int		drag_t0;
	int		drag_t1;

	/* Double-click reset */
	uint64_t	last_click_ms;
} 	t_ui_graph_zoom;

void	ui_graph_zoom_reset(t_ui_graph_zoom *z);

/*
 * Minimal graph helper (no deps): draws a single metric time-series.
 * X axis is provided as seconds since session start.
 */
void	ui_graph_timeseries(t_window *w, t_ui_state *ui, t_rect r,
				const char *title,
				const double *values,
				const int *x_seconds,
				int n,
				double vmax,
				const char *y_label,
				const char *unit,
				unsigned int line_color);

/*
 * Same as ui_graph_timeseries(), but can display a small "group" badge per
 * point (ex: loot packet aggregation). If badge_counts[i] > 1, the graph draws
 * a visual marker and a "×N" label near the point.
 */
void	ui_graph_timeseries_badges(t_window *w, t_ui_state *ui, t_rect r,
				const char *title,
				const double *values,
				const int *x_seconds,
				const int *badge_counts,
				int n,
				double vmax,
				const char *y_label,
				const char *unit,
				unsigned int line_color);


/*
 * Same as ui_graph_timeseries(), but can display point annotations.
 * annots[i].point_index refers to the index in values/x_seconds.
 */
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
				int annots_n);

/*
 * Same as ui_graph_timeseries_badges(), with annotations.
 */
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
				int annots_n);

/* -------------------------------------------------------------------------- */
/*  Zoomable variants (Overview + main zoom window)                           */
/* -------------------------------------------------------------------------- */

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
				uint32_t series_version);

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
				uint32_t series_version);

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
				uint32_t series_version);

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
				uint32_t series_version);

#endif
