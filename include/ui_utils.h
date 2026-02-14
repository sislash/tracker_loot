#ifndef UI_UTILS_H
#define UI_UTILS_H

# include <stdarg.h>

# define STATUS_WIDTH 75

/* -------------------------------------------------------------------------- */
/* Window/UI helpers (shared by multiple "screens")                           */
/* -------------------------------------------------------------------------- */

typedef struct s_window	t_window;

void	ui_draw_lines(t_window *w, int x, int y,
			const char **lines, int n, int line_h, unsigned color);
void	ui_draw_lines_clipped(t_window *w, int x, int y,
			const char **lines, int n, int line_h, unsigned color,
			int max_y);
void	ui_screen_message(t_window *w, const char *title,
			const char **lines, int n);

/* Globals CSV helpers used by multiple UI screens */
void	ui_ensure_globals_csv(void);
int		ui_action_clear_globals_csv(t_window *w);

/* Hunt CSV helpers */
void	ui_ensure_hunt_csv(void);
int		ui_action_clear_hunt_csv(t_window *w);

void ui_sleep_ms(unsigned ms);
void ui_wait_enter(void);
void ui_clear_screen(void);
void ui_clear_viewport(void);
void print_menu_line(const char *text);
void print_hr(void);
void print_hrs(void);
void print_status_line(const char *label, const char *value);
void print_status_linef(const char *label, const char *fmt, ...);
void ui_cursor_home(void);
int  ui_user_wants_quit(void);
void ui_flush_stdin(void);

#endif
