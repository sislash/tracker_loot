/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   mob_selected.c                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: login <login@student.42.fr>                +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/02/12 00:00:00 by login             #+#    #+#             */
/*   Updated: 2026/02/12 00:00:00 by you              ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "mob_selected.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

static void	trim_eol(char *s)
{
	size_t	len;

	if (!s)
		return ;
	len = strlen(s);
	while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r'))
		s[--len] = '\0';
}

static void	trim_spaces(char *s)
{
	size_t	len;
	size_t	i;
	size_t	start;

	if (!s)
		return ;
	len = strlen(s);
	start = 0;
	while (start < len && (s[start] == ' ' || s[start] == '\t'))
		start++;
	if (start > 0)
	{
		i = 0;
		while (s[start])
			s[i++] = s[start++];
		s[i] = '\0';
		len = strlen(s);
	}
	while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t'))
		s[--len] = '\0';
}

void	mob_selected_sanitize(char *s)
{
	size_t	i;
	size_t	j;

	if (!s)
		return ;
	trim_eol(s);
	/* Remove control chars + protect CSV: replace commas by spaces. */
	i = 0;
	j = 0;
	while (s[i])
	{
		unsigned char c = (unsigned char)s[i++];
		if (c == ',')
			c = ' ';
		if (c < 32 || c == 127)
			continue ;
		s[j++] = (char)c;
		if (j + 1 >= 128)
			break ;
	}
	s[j] = '\0';
	trim_spaces(s);
}

int	mob_selected_load(const char *path, char *out, size_t outsz)
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
	mob_selected_sanitize(out);
	return (0);
}

int	mob_selected_save(const char *path, const char *name)
{
	FILE	*f;
	char	buf[128];

	if (!path || !name)
		return (-1);
	snprintf(buf, sizeof(buf), "%s", name);
	mob_selected_sanitize(buf);
	f = fopen(path, "wb");
	if (!f)
		return (-1);
	fprintf(f, "%s\n", buf);
	fclose(f);
	return (0);
}

int	mob_selected_clear(const char *path)
{
	if (!path)
		return (-1);
	return (mob_selected_save(path, ""));
}
