/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ui_utils.c                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: login <login@student.42.fr>                +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/01/31 00:00:00 by login             #+#    #+#             */
/*   Updated: 2026/01/31 00:00:00 by login            ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */
#ifndef _WIN32
# ifndef _POSIX_C_SOURCE
#  define _POSIX_C_SOURCE 199309L
# endif
#endif


#include "ui_utils.h"
#include "ui_chrome.h"
#include "ui_layout.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "overlay.h"
#include "hunt_csv.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#ifdef _WIN32
# include <conio.h>
# include <windows.h>
#else
# include <sys/select.h>
# include <time.h>
#endif

/* -------------------------------------------------------------------------- */
/* Timing                                                                     */
/* -------------------------------------------------------------------------- */

void	ui_sleep_ms(unsigned ms)
{
    #ifdef _WIN32
    Sleep((DWORD)ms);
    #else
    struct timespec	ts;
    
    ts.tv_sec = (time_t)(ms / 1000);
    ts.tv_nsec = (long)((ms % 1000) * 1000000L);
    nanosleep(&ts, NULL);
    #endif
}

/* -------------------------------------------------------------------------- */
/* Pause                                                                      */
/* -------------------------------------------------------------------------- */

void	ui_wait_enter(void)
{
    printf("Press ENTER to continue...");
    fflush(stdout);
    getchar();
}

/* -------------------------------------------------------------------------- */
/* Cursor / Clear                                                             */
/* -------------------------------------------------------------------------- */

/*
 * Clear the console and reset cursor to top-left.
 * - Windows: WinAPI
 * - Unix: ANSI escape sequences
 */
void	ui_clear_screen(void)
{
    #ifdef _WIN32
    HANDLE						hout;
    CONSOLE_SCREEN_BUFFER_INFO	csbi;
    DWORD						cell_count;
    DWORD						count;
    COORD						home;
    
    hout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hout == INVALID_HANDLE_VALUE)
        return ;
    if (!GetConsoleScreenBufferInfo(hout, &csbi))
        return ;
    cell_count = (DWORD)csbi.dwSize.X * (DWORD)csbi.dwSize.Y;
    count = 0;
    home.X = 0;
    home.Y = 0;
    FillConsoleOutputCharacterA(hout, ' ', cell_count, home, &count);
    FillConsoleOutputAttribute(hout, csbi.wAttributes, cell_count, home, &count);
    SetConsoleCursorPosition(hout, home);
    #else
    fputs("\x1b[2J\x1b[H", stdout);
    fflush(stdout);
    #endif
}

/*
 * Clear only the visible viewport and reset cursor to the viewport top.
 * Useful for dashboards (avoids growing scrollback unnecessarily).
 */
void	ui_clear_viewport(void)
{
    #ifdef _WIN32
    HANDLE						hout;
    CONSOLE_SCREEN_BUFFER_INFO	csbi;
    SHORT						width;
    SHORT						height;
    COORD						top_left;
    DWORD						cells;
    DWORD						count;
    
    hout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hout == INVALID_HANDLE_VALUE)
        return ;
    if (!GetConsoleScreenBufferInfo(hout, &csbi))
        return ;
    width = (SHORT)(csbi.srWindow.Right - csbi.srWindow.Left + 1);
    height = (SHORT)(csbi.srWindow.Bottom - csbi.srWindow.Top + 1);
    top_left.X = csbi.srWindow.Left;
    top_left.Y = csbi.srWindow.Top;
    cells = (DWORD)width * (DWORD)height;
    count = 0;
    FillConsoleOutputCharacterA(hout, ' ', cells, top_left, &count);
    FillConsoleOutputAttribute(hout, csbi.wAttributes, cells, top_left, &count);
    SetConsoleCursorPosition(hout, top_left);
    #else
    fputs("\x1b[2J\x1b[H", stdout);
    fflush(stdout);
    #endif
}

void	ui_cursor_home(void)
{
    #ifdef _WIN32
    HANDLE	hout;
    COORD	home;
    
    hout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hout == INVALID_HANDLE_VALUE)
        return ;
    home.X = 0;
    home.Y = 0;
    SetConsoleCursorPosition(hout, home);
    #else
    fputs("\x1b[H", stdout);
    fflush(stdout);
    #endif
}

/* -------------------------------------------------------------------------- */
/* Dashboard helpers                                                          */
/* -------------------------------------------------------------------------- */

void	print_hr(void)
{
    printf("|"
    "---------------------------------------------------------------------------"
    "|\n");
}

void	print_hrs(void)
{
    printf("|"
    "==========================================================================="
    "|\n");
}

void	print_menu_line(const char *text)
{
    char	line[STATUS_WIDTH + 1];
    int		len;
    
    if (!text)
        text = "";
    len = snprintf(line, sizeof(line), "%s", text);
    if (len < 0)
    {
        memset(line, ' ', STATUS_WIDTH);
        line[STATUS_WIDTH] = '\0';
        printf("|%s|\n", line);
        return ;
    }
    if (len < STATUS_WIDTH)
    {
        memset(line + len, ' ', STATUS_WIDTH - len);
        line[STATUS_WIDTH] = '\0';
    }
    else
        line[STATUS_WIDTH] = '\0';
    printf("|%s|\n", line);
}

void	print_status_line(const char *label, const char *value)
{
    char	line[STATUS_WIDTH + 1];
    int		len;
    
    if (!label)
        label = "";
    if (!value)
        value = "";
    len = snprintf(line, sizeof(line), "%s: %s", label, value);
    if (len < 0)
    {
        memset(line, ' ', STATUS_WIDTH);
        line[STATUS_WIDTH] = '\0';
        printf("|%s|\n", line);
        return ;
    }
    if (len < STATUS_WIDTH)
    {
        memset(line + len, ' ', STATUS_WIDTH - len);
        line[STATUS_WIDTH] = '\0';
    }
    else
        line[STATUS_WIDTH] = '\0';
    printf("|%s|\n", line);
}

void	print_status_linef(const char *label, const char *fmt, ...)
{
    char	value[256];
    va_list	ap;
    
    if (!fmt)
        fmt = "";
    va_start(ap, fmt);
    vsnprintf(value, sizeof(value), fmt, ap);
    va_end(ap);
    print_status_line(label, value);
}

/* -------------------------------------------------------------------------- */
/* Input                                                                      */
/* -------------------------------------------------------------------------- */

int	ui_user_wants_quit(void)
{
    #ifdef _WIN32
    int	c;
    
    if (_kbhit())
    {
        c = _getch();
        if (c == 'q' || c == 'Q')
            return (1);
    }
    return (0);
    #else
    struct timeval	tv;
    fd_set			fds;
    int				c;
    
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    FD_ZERO(&fds);
    FD_SET(0, &fds);
    if (select(1, &fds, NULL, NULL, &tv) > 0)
    {
        c = getchar();
        if (c == 'q' || c == 'Q')
            return (1);
    }
    return (0);
    #endif
}

void	ui_flush_stdin(void)
{
    int	c;
    
    c = getchar();
    while (c != '\n' && c != EOF)
        c = getchar();
}

/* -------------------------------------------------------------------------- */
/* Window/UI helpers (shared across multiple screens)                         */
/* -------------------------------------------------------------------------- */

#include "window.h"
#include "menu_principale.h"
#include "core_paths.h"
#include "csv.h"

void	ui_draw_lines(t_window *w, int x, int y,
			const char **lines, int n, int line_h, unsigned color)
{
	ui_draw_lines_clipped(w, x, y, lines, n, line_h, color, 0x7FFFFFFF);
}

void	ui_draw_lines_clipped(t_window *w, int x, int y,
			const char **lines, int n, int line_h, unsigned color,
			int max_y)
{
	int	i;
	int	yy;

	if (!w || !lines || n <= 0)
		return;
	if (line_h <= 0)
		line_h = 12;
	i = 0;
	while (i < n)
	{
		yy = y + i * line_h;
		if (yy >= max_y)
			break;
		if (lines[i] && lines[i][0])
			window_draw_text(w, x, yy, lines[i], color);
		i++;
	}
}

void	ui_screen_message(t_window *w, const char *title,
			const char **lines, int n)
{
	t_ui_state	ui;
	t_ui_layout	ly;
	t_rect		content;
	const char	*items[1];
	int			selected;
	int			clicked;
	int			max_y;
	t_rect		list_r;

	if (!w)
		return;
	ui.theme = &g_theme_dark;
	items[0] = "Retour";
	selected = 0;
	while (w->running)
	{
		window_poll_events(w);
		overlay_tick_auto_hunt();
		if (w->key_escape || w->key_enter)
			break;
		ui_calc_layout(w, &ly);
		content = ui_draw_chrome(w, &ui, title, NULL,
			"Enter/Echap retour");
		list_r = (t_rect){ly.sidebar.x, ly.sidebar.y + UI_PAD,
			ly.sidebar.w, ly.sidebar.h - UI_PAD * 2};
		clicked = ui_list(w, &ui, list_r, items, 1, &selected, 40, 0);
		if (clicked == 0)
			break;
		/* content */
		ui_draw_panel(w, content, ui.theme->bg, ui.theme->border);
		max_y = content.y + content.h - UI_PAD;
		if (max_y < 0)
			max_y = 0x7FFFFFFF;
		if (lines && n > 0)
			ui_draw_lines_clipped(w, content.x + UI_PAD, content.y + UI_PAD,
				lines, n, 14, ui.theme->text2, max_y);
		window_present(w);
		ui_sleep_ms(16);
	}
}

void	ui_ensure_globals_csv(void)
{
	FILE	*f;

	f = fopen(tm_path_globals_csv(), "ab");
	if (!f)
		return;
	csv_ensure_header6(f);
	fclose(f);
}

void	ui_ensure_hunt_csv(void)
{
	FILE	*f;

	f = fopen(tm_path_hunt_csv(), "ab");
	if (!f)
		return ;
	hunt_csv_ensure_header_v2(f);
	fclose(f);
}

static int	ui_screen_confirm_yes_menu(t_window *w, const char *title,
					const char **lines, int n)
{
	t_ui_state	ui;
	t_ui_layout	ly;
	t_rect		content;
	const char	*items[2];
	int			selected;
	int			clicked;
	int			max_y;
	t_rect		list_r;

	if (!w)
		return (0);
	ui.theme = &g_theme_dark;
	items[0] = "Confirmer (YES)";
	items[1] = "Annuler";
	selected = 1;
	while (w->running)
	{
		window_poll_events(w);
		overlay_tick_auto_hunt();
		if (w->key_escape)
			return (0);
		if (w->key_enter)
			return (selected == 0);
		ui_calc_layout(w, &ly);
		content = ui_draw_chrome(w, &ui, title, NULL,
			"Enter valider   Echap annuler");
		list_r = (t_rect){ly.sidebar.x, ly.sidebar.y + UI_PAD,
			ly.sidebar.w, ly.sidebar.h - UI_PAD * 2};
		clicked = ui_list(w, &ui, list_r, items, 2, &selected, 40, 0);
		if (clicked == 0)
			return (1);
		if (clicked == 1)
			return (0);
		ui_draw_panel(w, content, ui.theme->bg, ui.theme->border);
		max_y = content.y + content.h - UI_PAD - 40;
		if (lines && n > 0)
			ui_draw_lines_clipped(w, content.x + UI_PAD, content.y + UI_PAD,
				lines, n, 14, ui.theme->text2, max_y);
		ui_draw_text(w, content.x + UI_PAD, content.y + content.h - 36,
			"Confirmer (YES) vide le CSV.", ui.theme->warn);
		window_present(w);
		ui_sleep_ms(16);
	}
	return (0);
}

int	ui_action_clear_globals_csv(t_window *w)
{
	FILE		*f;
	const char	*info[3];
	const char	*ok[1];
	const char	*err[1];

	if (!w)
		return (-1);
	info[0] = "ATTENTION : tu vas vider le fichier :";
	info[1] = tm_path_globals_csv();
	info[2] = "";
	if (!ui_screen_confirm_yes_menu(w, "VIDER CSV GLOBALS", info, 3))
	{
		const char	*msg[] = { "Annule." };
		ui_screen_message(w, "VIDER CSV GLOBALS", msg, 1);
		return (0);
	}
	f = fopen(tm_path_globals_csv(), "wb");
	if (!f)
	{
		err[0] = "[ERREUR] Impossible d'ouvrir le CSV en ecriture.";
		ui_screen_message(w, "VIDER CSV GLOBALS", err, 1);
		return (-1);
	}
	csv_ensure_header6(f);
	fclose(f);
	ui_ensure_globals_csv();
	ok[0] = "OK : CSV globals vide.";
	ui_screen_message(w, "VIDER CSV GLOBALS", ok, 1);
	return (0);
}

int	ui_action_clear_hunt_csv(t_window *w)
{
	FILE		*f;
	const char	*info[3];
	const char	*ok[1];
	const char	*err[1];

	if (!w)
		return (-1);
	info[0] = "ATTENTION : tu vas vider le fichier :";
	info[1] = tm_path_hunt_csv();
	info[2] = "";
	if (!ui_screen_confirm_yes_menu(w, "VIDER CSV CHASSE", info, 3))
	{
		const char	*msg[] = { "Annule." };
		ui_screen_message(w, "VIDER CSV CHASSE", msg, 1);
		return (0);
	}
	f = fopen(tm_path_hunt_csv(), "wb");
	if (!f)
	{
		err[0] = "[ERREUR] Impossible d'ouvrir le CSV en ecriture.";
		ui_screen_message(w, "VIDER CSV CHASSE", err, 1);
		return (-1);
	}
	hunt_csv_ensure_header_v2(f);
	fclose(f);
	ui_ensure_hunt_csv();
	ok[0] = "OK : CSV chasse vide.";
	ui_screen_message(w, "VIDER CSV CHASSE", ok, 1);
	return (0);
}
