#ifndef UI_LAYOUT_H
#define UI_LAYOUT_H

#include "window.h"

typedef struct s_rect
{
	int x;
	int y;
	int w;
	int h;
} 	t_rect;

typedef struct s_ui_layout
{
	t_rect	root;
	t_rect	topbar;
	t_rect	footer;
	t_rect	sidebar;
	t_rect	content;
	int		sidebar_w;
	int		top_h;
	int		footer_h;
} 	t_ui_layout;

/* Layout constants (tuned for 1024x768 and scales down reasonably). */
#define UI_TOP_H		84
#define UI_FOOTER_H		32
#define UI_SIDEBAR_W	280
#define UI_SIDEBAR_MIN	220
#define UI_SIDEBAR_MAX	340

/* Generic padding and font sizes (used by widgets/screens). */
#define UI_PAD			16
#define UI_LINE_H		14

void	ui_calc_layout(t_window *w, t_ui_layout *out);

/*
 * Like ui_calc_layout(), but lets the caller suggest a sidebar width.
 * The final width is clamped to sane bounds and to the current window size.
 */
void	ui_calc_layout_ex(t_window *w, t_ui_layout *out, int desired_sidebar_w);

#endif
