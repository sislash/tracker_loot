/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   window.h                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: you <you@student.42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/02/04                                 #+#    #+#           */
/*   Updated: 2026/02/04                                 #+#    #+#           */
/*                                                                            */
/* ************************************************************************** */

#ifndef WINDOW_H
# define WINDOW_H

/*
** Small integer point used by higher-level UI primitives (graphs, etc.).
** Kept minimal to stay portable (avoid Win32 POINT / X11 XPoint in headers).
*/
typedef struct s_point_i
{
	int	x;
	int	y;
}	t_point_i;

typedef struct s_window
{
    int				running;
    const char		*title;
    int				width;
    int				height;
    
    int				key_up;
    int				key_down;
    int				key_enter;
    int				key_escape;
    
    int			key_z;
	int			key_q;
	int			key_s;
	int			key_d;

	/* Mouse (frame-based): position + click pulse */
	int			mouse_x;
	int			mouse_y;
	int			mouse_left_click;
	/* Mouse button state (persistent) */
	int			mouse_left_down;

	/* UI drag state (for scrollbars, sliders, etc.) */
	int			ui_active_id;
	int			ui_drag_offset_y;

	/* Mouse wheel (frame-based): +1 up, -1 down (can accumulate) */
	int			mouse_wheel;

	/* Text input (frame-based): collected during window_poll_events() */
	char			text_input[64];
	int			text_len;
	int			key_backspace;
	int			key_delete;
	int			key_tab;
	int			key_o;
	int			key_h;

    int				use_buffer;
    
    void			*backend_1;
    void			*backend_2;
    void			*backend_3;
    
    unsigned int	*pixels;
    int				pitch;
}	t_window;

int		window_init(t_window *w, const char *title, int width, int height);
int		window_init_overlay(t_window *w, const char *title, int width, int height,
						int topmost, int alpha_0_255);
void	window_poll_events(t_window *w);

void	window_clear(t_window *w, int color);
void	window_fill_rect(t_window *w, int x, int y, int width, int height,
                         int color);
void	window_draw_text(t_window *w, int x, int y, const char *text, int color);

/* Simple line primitive (used by graphs, separators, etc.). */
void	window_draw_line(t_window *w, int x0, int y0, int x1, int y1, int color);

/*
** Polyline primitive (optimized for graphs).
** Draws connected segments through all points (n >= 2).
*/
void	window_draw_polyline(t_window *w, const t_point_i *pts, int n, int color);

void	window_present(t_window *w);
void	window_destroy(t_window *w);

#endif
