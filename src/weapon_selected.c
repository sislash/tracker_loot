/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   weapon_selected.c                                  :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: login <login@student.42.fr>                +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/01/31 00:00:00 by login             #+#    #+#             */
/*   Updated: 2026/01/31 00:00:00 by login            ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "weapon_selected.h"

#include <stdio.h>
#include <string.h>

static void	weapon_trim_eol(char *s)
{
	size_t	len;
	
	if (!s)
		return ;
	len = strlen(s);
	while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r'))
		s[--len] = '\0';
}

int	weapon_selected_load(const char *path, char *out, size_t outsz)
{
	FILE	*f;
	
	if (!out || outsz == 0 || !path)
		return (-1);
	out[0] = '\0';
	f = fopen(path, "rb");
	if (!f)
		return (-1);
	if (!fgets(out, (int)outsz, f))
	{
		fclose(f);
		return (-1);
	}
	fclose(f);
	weapon_trim_eol(out);
	return (0);
}

int	weapon_selected_save(const char *path, const char *name)
{
	FILE	*f;
	
	if (!path || !name)
		return (-1);
	f = fopen(path, "wb");
	if (!f)
		return (-1);
	fprintf(f, "%s\n", name);
	fclose(f);
	return (0);
}
