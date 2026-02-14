/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   window_win.c                                       :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: you <you@student.42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/02/04                                 #+#    #+#           */
/*   Updated: 2026/02/04                                 #+#    #+#           */
/*                                                                            */
/* ************************************************************************** */

#ifdef _WIN32

# include "window.h"

# include <windows.h>
# include <stdlib.h>
# include <string.h>

#ifndef GET_X_LPARAM
# define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#endif
#ifndef GET_Y_LPARAM
# define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#endif

typedef struct s_win_backend
{
    HINSTANCE	hinst;
    HWND		hwnd;
    const char	*class_name;
    /* Overlay windows must not PostQuitMessage() on destroy */
    int			is_overlay;
    BITMAPINFO	bmi;
    /* GDI double-buffer (prevents flicker) */
    HDC		back_dc;
    HBITMAP		back_bmp;
    HBITMAP		back_old_bmp;
    /* Monospace font to keep ASCII art aligned (Windows default is proportional) */
    HFONT		font;
    HFONT		old_font;
}	t_win_backend;

/*
** When the window is resized (fullscreen / maximize / manual resize), we must
** recreate the backbuffer and the software pixel buffer.
*/
static void	ft_resize_buffers(t_window *w, int width, int height)
{
	t_win_backend	*b;
	unsigned int	*new_pixels;
	HDC			hdc;

	if (!w || width <= 0 || height <= 0)
		return ;
	b = (t_win_backend *)w->backend_1;
	if (!b || !b->hwnd)
		return ;
	if (width == w->width && height == w->height)
		return ;
	new_pixels = (unsigned int *)malloc((size_t)width * (size_t)height * 4);
	if (!new_pixels)
		return ;
	if (w->pixels)
		free(w->pixels);
	w->pixels = new_pixels;
	w->width = width;
	w->height = height;
	w->pitch = width;
	memset(&b->bmi, 0, sizeof(b->bmi));
	b->bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	b->bmi.bmiHeader.biWidth = width;
	b->bmi.bmiHeader.biHeight = -height;
	b->bmi.bmiHeader.biPlanes = 1;
	b->bmi.bmiHeader.biBitCount = 32;
	b->bmi.bmiHeader.biCompression = BI_RGB;
	/* Recreate GDI backbuffer bitmap */
	if (b->back_dc)
	{
		if (b->back_old_bmp)
			SelectObject(b->back_dc, b->back_old_bmp);
		if (b->back_bmp)
			DeleteObject(b->back_bmp);
		hdc = GetDC(b->hwnd);
		b->back_bmp = CreateCompatibleBitmap(hdc, width, height);
		ReleaseDC(b->hwnd, hdc);
		b->back_old_bmp = (HBITMAP)SelectObject(b->back_dc, b->back_bmp);
	}
}

static HFONT	ft_create_mono_font(HDC hdc)
{
    int		px_h;
    HFONT	font;
    
    if (!hdc)
        return (NULL);
    /* 16px-ish at current DPI. Negative = character height in pixels. */
    px_h = -MulDiv(8, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    font = CreateFontA(px_h, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
    if (!font)
        font = CreateFontA(px_h, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, "Lucida Console");
    if (!font)
            font = (HFONT)GetStockObject(OEM_FIXED_FONT);
    return (font);
}

static COLORREF	ft_win_color(int rgb)
{
    int	r;
    int	g;
    int	b;
    
    r = (rgb >> 16) & 0xFF;
    g = (rgb >> 8) & 0xFF;
    b = rgb & 0xFF;
    return (RGB(r, g, b));
}

static void	ft_set_key(t_window *w, WPARAM key)
{
    if (key == VK_UP)
        w->key_up = 1;
    else if (key == VK_DOWN)
        w->key_down = 1;
    else if (key == VK_RETURN)
        w->key_enter = 1;
    else if (key == VK_ESCAPE)
        w->key_escape = 1;
    else if (key == 'Z')
        w->key_z = 1;
    else if (key == 'Q')
        w->key_q = 1;
    else if (key == 'S')
        w->key_s = 1;
    else if (key == 'D')
        w->key_d = 1;
	else if (key == 'O')
		w->key_o = 1;
	else if (key == 'H')
		w->key_h = 1;
	else if (key == VK_BACK)
		w->key_backspace = 1;
	else if (key == VK_DELETE)
		w->key_delete = 1;
	else if (key == VK_TAB)
		w->key_tab = 1;
}

static void	ft_unset_key(t_window *w, WPARAM key)
{
    if (key == 'Z')
        w->key_z = 0;
    else if (key == 'Q')
        w->key_q = 0;
    else if (key == 'S')
        w->key_s = 0;
    else if (key == 'D')
        w->key_d = 0;
	else if (key == 'O')
		w->key_o = 0;
}

static LRESULT CALLBACK	ft_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    t_window		*w;
    t_win_backend	*b;
    LRESULT			ht;
    
    w = (t_window *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    b = (w ? (t_win_backend *)w->backend_1 : NULL);
    if (msg == WM_CREATE)
    {
        w = (t_window *)((CREATESTRUCT *)lparam)->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)w);
        return (0);
    }
    if (msg == WM_KEYDOWN && w)
        return (ft_set_key(w, wparam), 0);
    if (msg == WM_KEYUP && w)
        return (ft_unset_key(w, wparam), 0);
    
    /* Overlay must not steal focus from the main window */
    if (msg == WM_MOUSEACTIVATE && b && b->is_overlay)
        return (MA_NOACTIVATE);
    
    /*
    ** Overlay window: allow dragging without breaking UI clicks.
    ** Old behavior returned HTCAPTION for the whole client area -> buttons unclickable.
    ** New behavior: draggable only from a small top zone (like a title bar).
    */
    if (msg == WM_NCHITTEST && b && b->is_overlay)
    {
        POINT	pt;
        int		drag_h;

        ht = DefWindowProc(hwnd, msg, wparam, lparam);
        if (ht != HTCLIENT)
            return (ht);
        /* Convert screen coords to client coords */
        pt.x = GET_X_LPARAM(lparam);
        pt.y = GET_Y_LPARAM(lparam);
        ScreenToClient(hwnd, &pt);
        /* Draggable header height (px) */
        drag_h = 24;
        if (pt.y >= 0 && pt.y < drag_h)
            return (HTCAPTION);
        return (HTCLIENT);
    }
    if (msg == WM_MOUSEMOVE && w)
    {
        w->mouse_x = GET_X_LPARAM(lparam);
        w->mouse_y = GET_Y_LPARAM(lparam);
        return (0);
    }
    if (msg == WM_LBUTTONDOWN && w)
    {
        w->mouse_x = GET_X_LPARAM(lparam);
        w->mouse_y = GET_Y_LPARAM(lparam);
        w->mouse_left_click = 1;
		w->mouse_left_down = 1;
        return (0);
    }
	if (msg == WM_LBUTTONUP && w)
	{
		w->mouse_x = GET_X_LPARAM(lparam);
		w->mouse_y = GET_Y_LPARAM(lparam);
		w->mouse_left_down = 0;
		return (0);
	}
    if (msg == WM_MOUSEWHEEL && w)
    {
        short	delta;
        
        delta = (short)HIWORD(wparam);
        if (delta > 0)
            w->mouse_wheel += 1;
        else if (delta < 0)
            w->mouse_wheel -= 1;
        return (0);
    }
    if (msg == WM_CHAR && w)
    {
        unsigned int	ch;
        
        ch = (unsigned int)wparam;
        if (ch >= 32 && ch < 127)
        {
            if (w->text_len < (int)sizeof(w->text_input) - 1)
            {
                w->text_input[w->text_len++] = (char)ch;
                w->text_input[w->text_len] = '\0';
            }
        }
        return (0);
    }
    if (msg == WM_SIZE && w)
    {
        int	cw;
        int	ch;
        
        cw = (int)LOWORD(lparam);
        ch = (int)HIWORD(lparam);
        if (wparam != SIZE_MINIMIZED)
            ft_resize_buffers(w, cw, ch);
        return (0);
    }
    if (msg == WM_ERASEBKGND)
        return (1);
    if (msg == WM_CLOSE)
        return (DestroyWindow(hwnd), 0);
    if (msg == WM_DESTROY)
    {
        /* Do not kill the whole app when closing the overlay */
        if (w && b && b->is_overlay)
            return (w->running = 0, 0);
        return (PostQuitMessage(0), 0);
    }
    return (DefWindowProc(hwnd, msg, wparam, lparam));
}

static int	ft_register_class(t_win_backend *b)
{
    WNDCLASSA	wc;
    DWORD		err;
    
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = ft_wndproc;
    wc.hInstance = b->hinst;
    wc.lpszClassName = b->class_name;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    if (RegisterClassA(&wc))
        return (0);
    err = GetLastError();
    if (err == ERROR_CLASS_ALREADY_EXISTS)
        return (0);
    return (1);
}

static void	ft_clear_buf(t_window *w, unsigned int color)
{
    int	i;
    int	n;
    
    if (!w || !w->pixels)
        return ;
    n = w->pitch * w->height;
    i = 0;
    while (i < n)
    {
        w->pixels[i] = color;
        i++;
    }
}

int	window_init(t_window *w, const char *title, int width, int height)
{
    t_win_backend	*b;
    RECT			r;
    
    if (!w || width <= 0 || height <= 0)
        return (1);
    /*
     * * IMPORTANT:
     ** On Windows, the system can emit WM_SIZE very early during CreateWindowEx.
     ** If the caller provided an uninitialized t_window (stack garbage), our
     ** WM_SIZE handler may try to free/resize w->pixels before we set it.
     ** So we zero-init the struct up-front to make it safe.
     */
    memset(w, 0, sizeof(*w));
    b = (t_win_backend *)malloc(sizeof(*b));
    if (!b)
        return (1);
    memset(b, 0, sizeof(*b));
    b->hinst = GetModuleHandleA(NULL);
    b->class_name = "ft_window_class";
    /* Make backend available early (WM_SIZE can happen during creation) */
    w->backend_1 = (void *)b;
    w->backend_2 = NULL;
    w->backend_3 = NULL;
    if (ft_register_class(b) != 0)
    {
        w->backend_1 = NULL;
        return (free(b), 1);
    }
    r.left = 0;
    r.top = 0;
    r.right = width;
    r.bottom = height;
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    b->hwnd = CreateWindowExA(0, b->class_name, title, WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                              NULL, NULL, b->hinst, w);
    if (!b->hwnd)
    {
        w->backend_1 = NULL;
        return (free(b), 1);
    }
    ShowWindow(b->hwnd, SW_SHOW);
    UpdateWindow(b->hwnd);
    
    /* Create a compatible backbuffer to eliminate flicker when redrawing */
    {
        HDC	hdc;
        
        hdc = GetDC(b->hwnd);
        b->back_dc = CreateCompatibleDC(hdc);
        b->back_bmp = CreateCompatibleBitmap(hdc, width, height);
        b->back_old_bmp = (HBITMAP)SelectObject(b->back_dc, b->back_bmp);
        /* Force a monospace font on Windows so ASCII art aligns like on Linux */
        b->font = ft_create_mono_font(hdc);
        if (b->font)
            b->old_font = (HFONT)SelectObject(b->back_dc, b->font);
        ReleaseDC(b->hwnd, hdc);
    }
    
    w->pixels = (unsigned int *)malloc((size_t)width * (size_t)height * 4);
    if (!w->pixels)
        return (DestroyWindow(b->hwnd), free(b), 1);
    w->pitch = width;
    
    memset(&b->bmi, 0, sizeof(b->bmi));
    b->bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    b->bmi.bmiHeader.biWidth = width;
    b->bmi.bmiHeader.biHeight = -height;
    b->bmi.bmiHeader.biPlanes = 1;
    b->bmi.bmiHeader.biBitCount = 32;
    b->bmi.bmiHeader.biCompression = BI_RGB;
    
    w->running = 1;
    w->title = title;
    w->width = width;
    w->height = height;
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
    
    ft_clear_buf(w, 0xFFFFFFFFu);
    return (0);
}

int	window_init_overlay(t_window *w, const char *title, int width, int height,
					int topmost, int alpha_0_255)
{
	t_win_backend	*b;
	RECT			r;
	DWORD			exstyle;
	DWORD			style;

	if (!w || width <= 0 || height <= 0)
		return (1);
	memset(w, 0, sizeof(*w));
	b = (t_win_backend *)malloc(sizeof(*b));
	if (!b)
		return (1);
	memset(b, 0, sizeof(*b));
	b->hinst = GetModuleHandleA(NULL);
	b->class_name = "ft_overlay_class";
	b->is_overlay = 1;
	w->backend_1 = (void *)b;
	if (ft_register_class(b) != 0)
	{
		w->backend_1 = NULL;
		return (free(b), 1);
	}
	r.left = 0;
	r.top = 0;
	r.right = width;
	r.bottom = height;
	/* Borderless popup */
	style = WS_POPUP;
	exstyle = WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE;
	if (topmost)
		exstyle |= WS_EX_TOPMOST;
	AdjustWindowRectEx(&r, style, FALSE, exstyle);
	b->hwnd = CreateWindowExA(exstyle, b->class_name, title, style,
					  CW_USEDEFAULT, CW_USEDEFAULT,
					  r.right - r.left, r.bottom - r.top,
					  NULL, NULL, b->hinst, w);
	if (!b->hwnd)
	{
		w->backend_1 = NULL;
		return (free(b), 1);
	}
	/* semi-transparent */
	if (alpha_0_255 < 0)
		alpha_0_255 = 0;
	if (alpha_0_255 > 255)
		alpha_0_255 = 255;
	SetLayeredWindowAttributes(b->hwnd, 0, (BYTE)alpha_0_255, LWA_ALPHA);
	/* Show without activating (do not steal focus from main window) */
	ShowWindow(b->hwnd, SW_SHOWNOACTIVATE);
	SetWindowPos(b->hwnd, topmost ? HWND_TOPMOST : HWND_TOP,
			0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	UpdateWindow(b->hwnd);
	{
		HDC	hdc;
		hdc = GetDC(b->hwnd);
		b->back_dc = CreateCompatibleDC(hdc);
		b->back_bmp = CreateCompatibleBitmap(hdc, width, height);
		b->back_old_bmp = (HBITMAP)SelectObject(b->back_dc, b->back_bmp);
		b->font = ft_create_mono_font(hdc);
		if (b->font)
			b->old_font = (HFONT)SelectObject(b->back_dc, b->font);
		ReleaseDC(b->hwnd, hdc);
	}
	w->pixels = (unsigned int *)malloc((size_t)width * (size_t)height * 4);
	if (!w->pixels)
		return (DestroyWindow(b->hwnd), free(b), 1);
	w->pitch = width;
	memset(&b->bmi, 0, sizeof(b->bmi));
	b->bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	b->bmi.bmiHeader.biWidth = width;
	b->bmi.bmiHeader.biHeight = -height;
	b->bmi.bmiHeader.biPlanes = 1;
	b->bmi.bmiHeader.biBitCount = 32;
	b->bmi.bmiHeader.biCompression = BI_RGB;
	w->running = 1;
	w->title = title;
	w->width = width;
	w->height = height;
	w->use_buffer = 1;
	ft_clear_buf(w, 0x00000000u);
	return (0);
}


void	window_poll_events(t_window *w)
{
	MSG				msg;
	t_win_backend	*b;
	HWND			target;

	if (!w)
		return ;
	/* Reset per-frame inputs */
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

	b = (t_win_backend *)w->backend_1;
	target = (b ? b->hwnd : NULL);

	/*
	** IMPORTANT (multi-window):
	** The app can call window_poll_events() for multiple windows each frame
	** (main + overlay). If we drain the whole thread queue with hwnd=NULL,
	** the first poll will also dispatch the other window's mouse events, and
	** the second poll will then reset its input flags, making clicks feel like
	** they "don't register" unless you insist.
	**
	** Fix: only pump messages for the requested window handle. Still consume
	** WM_QUIT (thread message) without draining other windows' input.
	*/
	while (PeekMessage(&msg, NULL, WM_QUIT, WM_QUIT, PM_REMOVE))
		w->running = 0;

	if (!target)
		return ;

	while (PeekMessage(&msg, target, 0, 0, PM_REMOVE))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
}

void	window_clear(t_window *w, int color)
{
    t_win_backend	*b;
    HDC			hdc;
    RECT		rc;
    HBRUSH		brush;
    
    if (!w)
        return ;
    b = (t_win_backend *)w->backend_1;
    if (!b || !b->hwnd)
        return ;
    GetClientRect(b->hwnd, &rc);
    brush = CreateSolidBrush(ft_win_color(color));
    if (w->use_buffer && b->back_dc)
        FillRect(b->back_dc, &rc, brush);
    else
    {
        hdc = GetDC(b->hwnd);
        FillRect(hdc, &rc, brush);
        ReleaseDC(b->hwnd, hdc);
    }
    DeleteObject(brush);
}

void	window_fill_rect(t_window *w, int x, int y, int width, int height,
                         int color)
{
    t_win_backend	*b;
    HDC			hdc;
    RECT		rc;
    HBRUSH		brush;
    
    if (!w || width <= 0 || height <= 0)
        return ;
    b = (t_win_backend *)w->backend_1;
    if (!b || !b->hwnd)
        return ;
    rc.left = x;
    rc.top = y;
    rc.right = x + width;
    rc.bottom = y + height;
    brush = CreateSolidBrush(ft_win_color(color));
    if (w->use_buffer && b->back_dc)
        FillRect(b->back_dc, &rc, brush);
    else
    {
        hdc = GetDC(b->hwnd);
        FillRect(hdc, &rc, brush);
        ReleaseDC(b->hwnd, hdc);
    }
    DeleteObject(brush);
}

void	window_draw_text(t_window *w, int x, int y, const char *text, int color)
{
    t_win_backend	*b;
    HDC			hdc;
    HFONT			old;
    
    if (!w || !text)
        return ;
    b = (t_win_backend *)w->backend_1;
    if (!b || !b->hwnd)
        return ;
    if (w->use_buffer && b->back_dc)
        hdc = b->back_dc;
    else
        hdc = GetDC(b->hwnd);
    old = NULL;
    /* Ensure monospace font for consistent character widths */
    if (b->font && hdc && hdc != b->back_dc)
        old = (HFONT)SelectObject(hdc, b->font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, ft_win_color(color));
    TextOutA(hdc, x, y, text, (int)lstrlenA(text));
    if (old)
        SelectObject(hdc, old);
    if (!(w->use_buffer && b->back_dc))
        ReleaseDC(b->hwnd, hdc);
}

void	window_draw_line(t_window *w, int x0, int y0, int x1, int y1, int color)
{
	t_win_backend	*b;
	HDC			hdc;
	HPEN			old_pen;

	if (!w)
		return ;
	b = (t_win_backend *)w->backend_1;
	if (!b || !b->hwnd)
		return ;
	if (w->use_buffer && b->back_dc)
		hdc = b->back_dc;
	else
		hdc = GetDC(b->hwnd);
	old_pen = (HPEN)SelectObject(hdc, GetStockObject(DC_PEN));
	SetDCPenColor(hdc, ft_win_color(color));
	MoveToEx(hdc, x0, y0, NULL);
	LineTo(hdc, x1, y1);
	if (old_pen)
		SelectObject(hdc, old_pen);
	if (!(w->use_buffer && b->back_dc))
		ReleaseDC(b->hwnd, hdc);
}

void	window_draw_polyline(t_window *w, const t_point_i *pts, int n, int color)
{
	t_win_backend	*b;
	HDC			hdc;
	HPEN			old_pen;
	POINT			wp[1024];
	int				i;

	if (!w || !pts || n < 2)
		return ;
	b = (t_win_backend *)w->backend_1;
	if (!b || !b->hwnd)
		return ;
	if (n > (int)(sizeof(wp) / sizeof(wp[0])))
		n = (int)(sizeof(wp) / sizeof(wp[0]));
	if (w->use_buffer && b->back_dc)
		hdc = b->back_dc;
	else
		hdc = GetDC(b->hwnd);
	old_pen = (HPEN)SelectObject(hdc, GetStockObject(DC_PEN));
	SetDCPenColor(hdc, ft_win_color(color));
	i = 0;
	while (i < n)
	{
		wp[i].x = pts[i].x;
		wp[i].y = pts[i].y;
		i++;
	}
	Polyline(hdc, wp, n);
	if (old_pen)
		SelectObject(hdc, old_pen);
	if (!(w->use_buffer && b->back_dc))
		ReleaseDC(b->hwnd, hdc);
}

void	window_present(t_window *w)
{
    t_win_backend	*b;
    HDC			hdc;
    
    if (!w)
        return ;
    b = (t_win_backend *)w->backend_1;
    if (!b || !b->hwnd)
        return ;
    hdc = GetDC(b->hwnd);
    if (w->use_buffer && b->back_dc && b->back_bmp)
        BitBlt(hdc, 0, 0, w->width, w->height, b->back_dc, 0, 0, SRCCOPY);
    else if (w->use_buffer && w->pixels)
        StretchDIBits(hdc, 0, 0, w->width, w->height, 0, 0, w->width, w->height,
                      w->pixels, &b->bmi, DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(b->hwnd, hdc);
}

void	window_destroy(t_window *w)
{
    t_win_backend	*b;
    
    if (!w)
        return ;
    b = (t_win_backend *)w->backend_1;
    if (w->pixels)
        free(w->pixels);
    if (b)
    {
        if (b->back_dc)
        {
            /* Restore/delete font selected into back_dc */
            if (b->old_font)
                SelectObject(b->back_dc, b->old_font);
            if (b->font && b->font != (HFONT)GetStockObject(OEM_FIXED_FONT))
                DeleteObject(b->font);
            if (b->back_old_bmp)
                SelectObject(b->back_dc, b->back_old_bmp);
            if (b->back_bmp)
                DeleteObject(b->back_bmp);
            DeleteDC(b->back_dc);
        }
        if (b->hwnd)
            DestroyWindow(b->hwnd);
        UnregisterClassA(b->class_name, b->hinst);
        free(b);
    }
    w->backend_1 = NULL;
    w->backend_2 = NULL;
    w->backend_3 = NULL;
    w->pixels = NULL;
    w->pitch = 0;
}

#endif
