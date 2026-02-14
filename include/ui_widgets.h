#ifndef UI_WIDGETS_H
#define UI_WIDGETS_H

#include "window.h"
#include "ui_layout.h"
#include "ui_theme.h"

typedef enum e_ui_btn_style
{
	UI_BTN_PRIMARY,
	UI_BTN_SECONDARY,
	UI_BTN_GHOST
} 	t_ui_btn_style;

typedef struct s_ui_state
{
	const t_theme	*theme;
} 	t_ui_state;

/* Drawing helpers */
void	ui_draw_panel(t_window *w, t_rect r, unsigned int bg,
		unsigned int border);
void	ui_draw_text(t_window *w, int x, int y, const char *text,
		unsigned int color);

/*
 * Very small text measurement helper (approximate).
 * With the current X11 core fonts, we don't have real glyph metrics exposed.
 * This is "good enough" to size the sidebar to the longest label.
 */
int		ui_measure_text_w(const char *text, int font_px);

/*
 * Simple vertical scrolling helper for a scrollable view.
 * - scroll_y is in pixels, clamped to [0, max_scroll].
 * - Uses mouse wheel (w->mouse_wheel) by default.
 */
void	ui_scroll_update(t_window *w, int *scroll_y,
		int content_h, int view_h, int step_px);

/* Compute a sidebar width that fits the longest label + padding. */
int		ui_sidebar_width_for_labels(const char **items, int count,
		int font_px, int pad_px);

/* Scrollable list (vertical). scroll_y in pixels. Returns clicked index or -1. */
int		ui_list_scroll(t_window *w, t_ui_state *ui, t_rect r,
		const char **items, int count, int *selected,
		int item_h, int show_icons, int *scroll_y);

/* Widgets */
int		ui_button(t_window *w, t_ui_state *ui, t_rect r,
		const char *label, t_ui_btn_style style, int enabled);

/* Single-line input (buffer is edited when focused). Returns 1 if changed. */
int		ui_input_text(t_window *w, t_ui_state *ui, t_rect r,
			char *buffer, int bufsz, int *focus_id, int my_id);

/* Scrollable text lines helper for right-side panels. */
void	ui_text_lines_scroll(t_window *w, t_ui_state *ui, t_rect r,
			const char **lines, int n, int line_h,
			unsigned int color, int *scroll_y);

/* Simple list (vertical). Returns clicked index or -1. */
int		ui_list(t_window *w, t_ui_state *ui, t_rect r,
		const char **items, int count, int *selected,
		int item_h, int show_icons);

/* Card for dashboard stats */
void	ui_card(t_window *w, t_ui_state *ui, t_rect r,
		const char *title, const char *value, unsigned int value_color);

/* Small layout helpers (for pro forms) */
void	ui_draw_hline(t_window *w, int x, int y, int width, unsigned int color);
void	ui_section_header(t_window *w, t_ui_state *ui, t_rect r, const char *title);

#endif
