/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   markup_ini.h                                       :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: entropia-tracker                              +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/01/27                                #+#    #+#             */
/*   Updated: 2026/01/27                                #+#    #+#             */
/*                                                                            */
/* ************************************************************************** */

#ifndef MARKUP_INI_H
# define MARKUP_INI_H

# include "markup.h"

/*
 * * Parse markup.ini et remplit db.
 ** Format:
 **   [Item Name]
 **   type = percent | tt_plus
 **   value = 1.025  (percent)  ou  0.10 (tt_plus)
 */
int	markup_ini_parse_file(t_markup_db *db, const char *path);

#endif
