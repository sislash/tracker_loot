/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   chatlog_path.c                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: login <login@student.42.fr>                +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/01/31 00:00:00 by login             #+#    #+#             */
/*   Updated: 2026/01/31 00:00:00 by login            ###   ###########       */
/*                                                                            */
/* ************************************************************************** */

#include "chatlog_path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
# include <io.h>
# define ACCESS _access
# define F_OK 0
# define PATH_SEP '\\'
#else
# include <unistd.h>
# define ACCESS access
# define F_OK 0
# define PATH_SEP '/'
#endif

static int	copy_str(char *out, size_t outsz, const char *src)
{
	size_t	len;
	
	if (!out || !src || outsz == 0)
		return (0);
	len = strlen(src);
	if (len + 1 > outsz)
		return (0);
	memcpy(out, src, len + 1);
	return (1);
}

static int	try_path(char *out, size_t outsz, const char *path)
{
	if (!out || !path || outsz == 0)
		return (0);
	if (ACCESS(path, F_OK) != 0)
		return (0);
	return (copy_str(out, outsz, path));
}

static int	set_override_path(char *out, size_t outsz)
{
	const char	*override;
	
	override = getenv("ENTROPIA_CHATLOG");
	if (override && override[0])
	{
		if (!copy_str(out, outsz, override))
			return (-1);
		return (0);
	}
	return (1);
}

#ifdef _WIN32

static const char	*get_home_dir(void)
{
	const char	*home;
	
	home = getenv("USERPROFILE");
	if (!home)
		home = getenv("HOMEPATH");
	return (home);
}

static int	build_windows_path(char *out, size_t outsz)
{
	const char	*home;
	char		p1[1024];
	char		p2[1024];
	
	home = get_home_dir();
	if (!home)
		return (1);
	snprintf(p1, sizeof(p1),
			 "%s%cDocuments%cEntropia Universe%cchat.log",
		  home, PATH_SEP, PATH_SEP, PATH_SEP);
	if (try_path(out, outsz, p1))
		return (0);
	snprintf(p2, sizeof(p2),
			 "%s%cOneDrive%cDocuments%cEntropia Universe%cchat.log",
		  home, PATH_SEP, PATH_SEP, PATH_SEP, PATH_SEP);
	if (try_path(out, outsz, p2))
		return (0);
	if (copy_str(out, outsz, p1))
		return (0);
	return (-1);
}

#else

static int	build_unix_path(char *out, size_t outsz)
{
	const char	*home;
	char		p1[1024];
	
	home = getenv("HOME");
	if (!home)
		return (1);
	snprintf(p1, sizeof(p1),
			 "%s%cDocuments%cEntropia Universe%cchat.log",
		  home, PATH_SEP, PATH_SEP, PATH_SEP);
	if (try_path(out, outsz, p1))
		return (0);
	if (copy_str(out, outsz, p1))
		return (0);
	return (-1);
}

#endif

static int	set_fallback_path(char *out, size_t outsz)
{
	if (!copy_str(out, outsz, "chat.log"))
		return (-1);
	return (0);
}

int	chatlog_build_path(char *out, size_t outsz)
{
	int	ret;
	
	if (!out || outsz == 0)
		return (-1);
	out[0] = '\0';
	ret = set_override_path(out, outsz);
	if (ret != 1)
		return (ret);
	#ifdef _WIN32
	ret = build_windows_path(out, outsz);
	#else
	ret = build_unix_path(out, outsz);
	#endif
	if (ret != 1)
		return (ret);
	return (set_fallback_path(out, outsz));
}
