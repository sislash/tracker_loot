/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   window_linux.c                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: you <you@student.42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/02/04                                 #+#    #+#           */
/*   Updated: 2026/02/04                                 #+#    #+#           */
/*                                                                            */
/* ************************************************************************** */

#ifndef _WIN32

# include "window.h"
# include <X11/Xlib.h>
# include <X11/Xutil.h>
# include <X11/Xatom.h>
# include <X11/keysym.h>
# include <stdint.h>
# include <stdlib.h>
# include <string.h>

typedef struct s_x11_backend
{
    Display         *d;
    int             screen;
    Window          win;
    GC              gc;
    XFontStruct     *font;
    Atom            wm_delete;

    /* X11 double-buffer (Pixmap) to prevent flicker */
    Pixmap          back;

    /* Visual info (for fast TrueColor pixel computation) */
    Visual          *vis;
    int             depth;
    Colormap        cmap;

    /* Small color cache: avoids XAllocColor() on every draw */
    struct {
        int             rgb;
        unsigned long   pixel;
    }               color_cache[256];
    int             color_cache_count;
}   t_x11_backend;

static int  ft_mask_shift(unsigned long mask)
{
    int shift;

    shift = 0;
    if (mask == 0)
        return (0);
    while ((mask & 1UL) == 0UL)
    {
        mask >>= 1;
        shift++;
    }
    return (shift);
}

static unsigned long ft_x11_color(t_x11_backend *b, int rgb)
{
    int             i;
    unsigned long   r;
    unsigned long   g;
    unsigned long   bl;
    unsigned long   pix;
    XColor          c;

    if (!b || !b->d)
        return (0);

    /* Cache lookup (fast path) */
    i = 0;
    while (i < b->color_cache_count)
    {
        if (b->color_cache[i].rgb == rgb)
            return (b->color_cache[i].pixel);
        i++;
    }

    /* TrueColor/DirectColor: compute pixel directly using visual masks */
    if (b->vis && (b->vis->class == TrueColor || b->vis->class == DirectColor))
    {
        r = (unsigned long)((rgb >> 16) & 0xFF);
        g = (unsigned long)((rgb >> 8) & 0xFF);
        bl = (unsigned long)(rgb & 0xFF);

        pix = ((r << ft_mask_shift(b->vis->red_mask)) & b->vis->red_mask)
            | ((g << ft_mask_shift(b->vis->green_mask)) & b->vis->green_mask)
            | ((bl << ft_mask_shift(b->vis->blue_mask)) & b->vis->blue_mask);
    }
    else
    {
        /* Fallback: allocate from colormap (can be expensive) */
        c.red = (unsigned short)(((rgb >> 16) & 0xFF) * 257);
        c.green = (unsigned short)(((rgb >> 8) & 0xFF) * 257);
        c.blue = (unsigned short)((rgb & 0xFF) * 257);
        c.flags = DoRed | DoGreen | DoBlue;
        if (XAllocColor(b->d, b->cmap, &c) == 0)
            pix = BlackPixel(b->d, b->screen);
        else
            pix = c.pixel;
    }

    /* Cache insert (simple FIFO/overwrite) */
    if (b->color_cache_count < (int)(sizeof(b->color_cache) / sizeof(b->color_cache[0])))
    {
        b->color_cache[b->color_cache_count].rgb = rgb;
        b->color_cache[b->color_cache_count].pixel = pix;
        b->color_cache_count++;
    }
    else
    {
        /* Overwrite a deterministic slot to keep the cache bounded */
        i = (rgb ^ (rgb >> 8) ^ (rgb >> 16)) & 255;
        b->color_cache[i].rgb = rgb;
        b->color_cache[i].pixel = pix;
    }
    return (pix);
}


/*
** Resize handler: recreate Pixmap backbuffer + XImage + pixel buffer so the
** whole window remains drawable in fullscreen / maximized modes.
*/
/*
** Resize handler: recreate Pixmap backbuffer so the whole window remains
** drawable in fullscreen / maximized modes.
*/
static void ft_resize_buffers(t_window *w, int width, int height)
{
    t_x11_backend   *b;
    Pixmap          new_back;

    if (!w || width <= 0 || height <= 0)
        return ;
    b = (t_x11_backend *)w->backend_1;
    if (!b || !b->d || !b->win)
        return ;
    if (width == w->width && height == w->height)
        return ;

    new_back = XCreatePixmap(b->d, b->win, (unsigned int)width,
                             (unsigned int)height, (unsigned int)b->depth);
    if (!new_back)
        return ;
    if (b->back)
        XFreePixmap(b->d, b->back);
    b->back = new_back;

    w->width = width;
    w->height = height;
}


int	window_init(t_window *w, const char *title, int width, int height)
{
    t_x11_backend	*b;

    if (!w || width <= 0 || height <= 0)
        return (1);
    /* Make sure the caller didn't pass an uninitialized struct */
    memset(w, 0, sizeof(*w));
    b = (t_x11_backend *)malloc(sizeof(*b));
    if (!b)
        return (1);
    memset(b, 0, sizeof(*b));
    b->d = XOpenDisplay(NULL);
    if (!b->d)
        return (free(b), 1);
    b->screen = DefaultScreen(b->d);
    b->vis = DefaultVisual(b->d, b->screen);
    b->depth = DefaultDepth(b->d, b->screen);
    b->cmap = DefaultColormap(b->d, b->screen);

    b->win = XCreateSimpleWindow(b->d, RootWindow(b->d, b->screen), 0, 0,
                                 (unsigned int)width, (unsigned int)height, 1,
                                 BlackPixel(b->d, b->screen),
                                 WhitePixel(b->d, b->screen));
    XStoreName(b->d, b->win, title ? title : "window");
    XSelectInput(b->d, b->win, ExposureMask | KeyPressMask | KeyReleaseMask
        | ButtonPressMask | ButtonReleaseMask | PointerMotionMask
        | StructureNotifyMask);
    b->wm_delete = XInternAtom(b->d, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(b->d, b->win, &b->wm_delete, 1);
    XMapWindow(b->d, b->win);
    b->gc = XCreateGC(b->d, b->win, 0, NULL);

    /* Force a monospace font on X11 to keep ASCII separators aligned */
    b->font = XLoadQueryFont(b->d, "9x15");
    if (!b->font)
        b->font = XLoadQueryFont(b->d, "fixed");
    if (b->font)
        XSetFont(b->d, b->gc, b->font->fid);

    /* Pixmap backbuffer to eliminate flicker when redrawing */
    b->back = XCreatePixmap(b->d, b->win, (unsigned int)width,
                            (unsigned int)height, (unsigned int)b->depth);

    w->running = 1;
    w->title = title;
    w->width = width;
    w->height = height;
    w->pitch = 0;
    w->pixels = NULL;

    w->key_up = 0;
    w->key_down = 0;
    w->key_enter = 0;
    w->key_escape = 0;
    w->key_z = 0;
    w->key_q = 0;
    w->key_s = 0;
    w->key_d = 0;

    w->mouse_x = 0;
    w->mouse_y = 0;
    w->mouse_left_click = 0;
    w->use_buffer = 1;

    w->backend_1 = (void *)b;
    w->backend_2 = NULL;
    w->backend_3 = NULL;
    return (0);
}

int	window_init_overlay(t_window *w, const char *title, int width, int height,
					int topmost, int alpha_0_255)
{
	int			ret;
	t_x11_backend	*b;
	unsigned long	opacity;
	Atom			opacity_atom;
	Atom			state_atom;
	Atom			above_atom;
	XSizeHints		hints;

	/* Create a normal X11 window first */
	ret = window_init(w, title, width, height);
	if (ret != 0)
		return (ret);
	/* Best-effort hints: require a compositing WM to be effective */
	b = (t_x11_backend *)w->backend_1;
	if (!b || !b->d || !b->win)
		return (0);

	/* Keep overlay layout stable: prevent resizing (min=max). */
	memset(&hints, 0, sizeof(hints));
	hints.flags = PMinSize | PMaxSize;
	hints.min_width = width;
	hints.min_height = height;
	hints.max_width = width;
	hints.max_height = height;
	XSetWMNormalHints(b->d, b->win, &hints);

	/* Opacity: _NET_WM_WINDOW_OPACITY (0..0xffffffff) */
	if (alpha_0_255 < 0)
		alpha_0_255 = 0;
	if (alpha_0_255 > 255)
		alpha_0_255 = 255;
	opacity = (unsigned long)((double)alpha_0_255 / 255.0 * 4294967295.0);
	opacity_atom = XInternAtom(b->d, "_NET_WM_WINDOW_OPACITY", False);
	if (opacity_atom != None)
	{
		XChangeProperty(b->d, b->win, opacity_atom, XA_CARDINAL, 32,
			PropModeReplace, (unsigned char *)&opacity, 1);
	}

	/* Topmost: _NET_WM_STATE_ABOVE */
	if (topmost)
	{
		state_atom = XInternAtom(b->d, "_NET_WM_STATE", False);
		above_atom = XInternAtom(b->d, "_NET_WM_STATE_ABOVE", False);
		if (state_atom != None && above_atom != None)
		{
			XChangeProperty(b->d, b->win, state_atom, XA_ATOM, 32,
				PropModeReplace, (unsigned char *)&above_atom, 1);
		}
	}
	XFlush(b->d);
	return (0);
}

void	window_poll_events(t_window *w)
{
    t_x11_backend	*b;
    XEvent			ev;
    KeySym			ks;
    
    if (!w)
        return ;
    w->key_up = 0;
    w->key_down = 0;
    w->key_enter = 0;
    w->key_escape = 0;
    w->mouse_left_click = 0;
    w->mouse_wheel = 0;
	w->text_len = 0;
	w->text_input[0] = '\0';
	w->key_backspace = 0;
	w->key_delete = 0;
	w->key_tab = 0;
	w->key_o = 0;
	w->key_h = 0;
    b = (t_x11_backend *)w->backend_1;
    if (!b || !b->d)
        return ;
    while (XPending(b->d) > 0)
    {
        XNextEvent(b->d, &ev);
        if (ev.type == DestroyNotify)
            w->running = 0;
        else if (ev.type == ClientMessage)
        {
            if ((Atom)ev.xclient.data.l[0] == b->wm_delete)
                w->running = 0;
        }
        else if (ev.type == KeyPress)
        {
			char	buf[32];
			int		n;
			KeySym	ks2;

			/* Get both keysym and printable chars */
			n = XLookupString(&ev.xkey, buf, (int)sizeof(buf) - 1, &ks2, NULL);
			if (n > 0)
			{
				buf[n] = '\0';
				/* Append printable chars for ui_input_text */
				for (int i = 0; i < n; i++)
				{
					unsigned char c = (unsigned char)buf[i];
					if (c >= 32 && c < 127 && w->text_len < (int)sizeof(w->text_input) - 1)
						w->text_input[w->text_len++] = (char)c;
				}
				w->text_input[w->text_len] = '\0';
			}
			ks = ks2;
            if (ks == XK_Up)
                w->key_up = 1;
            else if (ks == XK_Down)
                w->key_down = 1;
            else if (ks == XK_Return || ks == XK_KP_Enter)
                w->key_enter = 1;
            else if (ks == XK_Escape)
                w->key_escape = 1;
            else if (ks == XK_z || ks == XK_Z)
                w->key_z = 1;
            else if (ks == XK_q || ks == XK_Q)
                w->key_q = 1;
            else if (ks == XK_s || ks == XK_S)
                w->key_s = 1;
            else if (ks == XK_d || ks == XK_D)
                w->key_d = 1;
			else if (ks == XK_o || ks == XK_O)
				w->key_o = 1;
			else if (ks == XK_h || ks == XK_H)
				w->key_h = 1;
			else if (ks == XK_BackSpace)
				w->key_backspace = 1;
			else if (ks == XK_Delete)
				w->key_delete = 1;
			else if (ks == XK_Tab)
				w->key_tab = 1;
        }
        else if (ev.type == KeyRelease)
        {
            ks = XLookupKeysym(&ev.xkey, 0);
            if (ks == XK_z || ks == XK_Z)
                w->key_z = 0;
            else if (ks == XK_q || ks == XK_Q)
                w->key_q = 0;
            else if (ks == XK_s || ks == XK_S)
                w->key_s = 0;
            else if (ks == XK_d || ks == XK_D)
                w->key_d = 0;
        }
        else if (ev.type == MotionNotify)
        {
            w->mouse_x = ev.xmotion.x;
            w->mouse_y = ev.xmotion.y;
        }
		else if (ev.type == ConfigureNotify)
		{
			ft_resize_buffers(w, ev.xconfigure.width, ev.xconfigure.height);
		}
        else if (ev.type == ButtonPress)
        {
            w->mouse_x = ev.xbutton.x;
            w->mouse_y = ev.xbutton.y;
            if (ev.xbutton.button == Button1)
			{
				w->mouse_left_click = 1;
				w->mouse_left_down = 1;
			}
			else if (ev.xbutton.button == Button4 || ev.xbutton.button == 6)
                w->mouse_wheel += 1;
			else if (ev.xbutton.button == Button5 || ev.xbutton.button == 7)
                w->mouse_wheel -= 1;
        }
		else if (ev.type == ButtonRelease)
		{
			if (ev.xbutton.button == Button1)
				w->mouse_left_down = 0;
		}
    }
}

void	window_clear(t_window *w, int color)
{
    t_x11_backend	*b;
    unsigned long	pix;
    Drawable		dst;
    
    if (!w)
        return ;
    b = (t_x11_backend *)w->backend_1;
    if (!b || !b->d || !b->gc)
        return ;
    dst = (w->use_buffer && b->back) ? b->back : b->win;
    pix = ft_x11_color(b, color);
    XSetForeground(b->d, b->gc, pix);
    XFillRectangle(b->d, dst, b->gc, 0, 0,
                   (unsigned int)w->width, (unsigned int)w->height);
}

void	window_fill_rect(t_window *w, int x, int y, int width, int height,
                         int color)
{
    t_x11_backend	*b;
    unsigned long	pix;
    Drawable		dst;
    
    if (!w || width <= 0 || height <= 0)
        return ;
    b = (t_x11_backend *)w->backend_1;
    if (!b || !b->d || !b->gc)
        return ;
    dst = (w->use_buffer && b->back) ? b->back : b->win;
    pix = ft_x11_color(b, color);
    XSetForeground(b->d, b->gc, pix);
    XFillRectangle(b->d, dst, b->gc, x, y,
                   (unsigned int)width, (unsigned int)height);
}

void	window_draw_text(t_window *w, int x, int y, const char *text, int color)
{
    t_x11_backend	*b;
    unsigned long	pix;
    Drawable		dst;
    
    if (!w || !text)
        return ;
    b = (t_x11_backend *)w->backend_1;
    if (!b || !b->d || !b->gc)
        return ;
    dst = (w->use_buffer && b->back) ? b->back : b->win;
    pix = ft_x11_color(b, color);
    XSetForeground(b->d, b->gc, pix);
    XDrawString(b->d, dst, b->gc, x, y + 16, text, (int)strlen(text));
}

void	window_draw_line(t_window *w, int x0, int y0, int x1, int y1, int color)
{
	t_x11_backend	*b;
	unsigned long	pix;
	Drawable		dst;

	if (!w)
		return ;
	b = (t_x11_backend *)w->backend_1;
	if (!b || !b->d || !b->gc)
		return ;
	dst = (w->use_buffer && b->back) ? b->back : b->win;
	pix = ft_x11_color(b, color);
	XSetForeground(b->d, b->gc, pix);
	XDrawLine(b->d, dst, b->gc, x0, y0, x1, y1);
}

void	window_draw_polyline(t_window *w, const t_point_i *pts, int n, int color)
{
	t_x11_backend	*b;
	unsigned long	pix;
	Drawable		dst;
	XPoint			xp[1024];
	int				i;

	if (!w || !pts || n < 2)
		return ;
	b = (t_x11_backend *)w->backend_1;
	if (!b || !b->d || !b->gc)
		return ;
	if (n > (int)(sizeof(xp) / sizeof(xp[0])))
		n = (int)(sizeof(xp) / sizeof(xp[0]));
	dst = (w->use_buffer && b->back) ? b->back : b->win;
	pix = ft_x11_color(b, color);
	XSetForeground(b->d, b->gc, pix);
	i = 0;
	while (i < n)
	{
		xp[i].x = (short)pts[i].x;
		xp[i].y = (short)pts[i].y;
		i++;
	}
	XDrawLines(b->d, dst, b->gc, xp, n, CoordModeOrigin);
}

void	window_present(t_window *w)
{
    t_x11_backend	*b;

    if (!w)
        return ;
    b = (t_x11_backend *)w->backend_1;
    if (!b || !b->d)
        return ;
    if (w->use_buffer && b->back)
    {
        XCopyArea(b->d, b->back, b->win, b->gc, 0, 0,
                  (unsigned int)w->width, (unsigned int)w->height, 0, 0);
    }
    XFlush(b->d);
}

void	window_destroy(t_window *w)
{
    t_x11_backend	*b;

    if (!w)
        return ;
    b = (t_x11_backend *)w->backend_1;
    if (b)
    {
        if (b->back)
            XFreePixmap(b->d, b->back);
        if (b->font)
            XFreeFont(b->d, b->font);
        if (b->gc)
            XFreeGC(b->d, b->gc);
        if (b->win)
            XDestroyWindow(b->d, b->win);
        if (b->d)
            XCloseDisplay(b->d);
        free(b);
    }
    w->backend_1 = NULL;
    w->backend_2 = NULL;
    w->backend_3 = NULL;
    w->pixels = NULL;
    w->pitch = 0;
}

#endif
