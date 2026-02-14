/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ui_key.c                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: login <login@student.42.fr>                +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/01/25                                #+#    #+#             */
/*   Updated: 2026/01/31                                #+#    #+#             */
/*                                                                            */
/* ************************************************************************** */

#include "ui_key.h"

#ifdef _WIN32
#include <conio.h>

int	ui_key_available(void)
{
    return (_kbhit() != 0);
}

int	ui_key_getch(void)
{
    return (_getch());
}
#else
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

static int	set_raw_mode(struct termios *old)
{
    struct termios	newt;
    
    if (tcgetattr(STDIN_FILENO, old) != 0)
        return (-1);
    newt = *old;
    newt.c_lflag &= (unsigned int)~(ICANON | ECHO);
    newt.c_cc[VMIN] = 0;
    newt.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &newt) != 0)
        return (-1);
    return (0);
}

static void	restore_mode(const struct termios *old)
{
    tcsetattr(STDIN_FILENO, TCSANOW, old);
}

int	ui_key_available(void)
{
    fd_set			set;
    struct timeval	tv;
    struct termios	old;
    int				ret;
    
    if (set_raw_mode(&old) != 0)
        return (0);
    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set);
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    ret = select(STDIN_FILENO + 1, &set, NULL, NULL, &tv);
    restore_mode(&old);
    if (ret > 0 && FD_ISSET(STDIN_FILENO, &set))
        return (1);
    return (0);
}

int	ui_key_getch(void)
{
    unsigned char	ch;
    ssize_t			n;
    struct termios	old;
    
    if (set_raw_mode(&old) != 0)
        return (-1);
    n = read(STDIN_FILENO, &ch, 1);
    restore_mode(&old);
    if (n == 1)
        return ((int)ch);
    return (-1);
}
#endif
