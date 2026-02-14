/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   globals_parser.h                                   :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: entropia-tracker                              +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/01/25                                #+#    #+#             */
/*   Updated: 2026/01/25                                #+#    #+#             */
/*                                                                            */
/* ************************************************************************** */

#ifndef GLOBALS_PARSER_H
# define GLOBALS_PARSER_H

# include <stddef.h>

typedef struct s_globals_event
{
    char	ts[32];      /* "YYYY-MM-DD HH:MM:SS" */
    char	type[32];    /* GLOB_MOB / HOF_MOB / ATH_MOB / GLOB_CRAFT / ... */
    char	name[128];   /* mob name OR item name */
    char	value[32];   /* "96" (PED) */
    char	raw[1024];   /* original line */
}	t_globals_event;

/*
 * * Parse une ligne du chat.log.
 ** Retour:
 **  1 si un event globals/craft/rare item a été reconnu
 **  0 sinon
 */
int	globals_parse_line(const char *line, t_globals_event *ev);

#endif
