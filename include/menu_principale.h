/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   menu.h                                             :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: you <you@student.42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/02/04 00:00:00 by you               #+#    #+#             */
/*   Updated: 2026/02/04 00:00:00 by you              ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef MENU_PRINCIPALE_H
# define MENU_PRINCIPALE_H

# include "window.h"

typedef struct s_menu
{
    const char	**items;
    int			count;
    int			selected;

	/* Last render position/metrics (used for mouse click selection) */
	int			render_x;
	int			render_y;
	int			item_w;
	int			item_h;
}	t_menu;

void	menu_init(t_menu *m, const char **items, int count);
int		menu_update(t_menu *m, t_window *w);
void	menu_render(t_menu *m, t_window *w, int x, int y);
void    menu_render_screen(t_menu *m, t_window *w, int x, int y);
void	stop_all_parsers(t_window *w, int x, int y);


#endif
