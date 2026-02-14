/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   fs_utils.c                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: login <login@student.42.fr>                +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/01/31 00:00:00 by login             #+#    #+#             */
/*   Updated: 2026/01/31 00:00:00 by login            ###   ###########       */
/*                                                                            */
/* ************************************************************************** */

/* POSIX APIs (fileno, ftruncate) under strict C99 builds */
#ifndef _WIN32
# ifndef _POSIX_C_SOURCE
#  define _POSIX_C_SOURCE 200809L
# endif
#endif

#include "fs_utils.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
# include <io.h>
#else
# include <unistd.h>
# include <sys/types.h>
#endif

#ifdef _WIN32
# include <direct.h>
# include <share.h>
# include <sys/stat.h>
# define TM_MKDIR(path) _mkdir(path)
#else
# include <sys/stat.h>
# include <sys/types.h>
# define TM_MKDIR(path) mkdir(path, 0755)
#endif

static int	mkdir_ok(const char *path)
{
	if (TM_MKDIR(path) != 0 && errno != EEXIST)
		return (-1);
	return (0);
}

static int	cut_to_parent(char *tmp)
{
	size_t	i;
	
	i = strlen(tmp);
	while (i > 0)
	{
		i--;
		if (tmp[i] == '/' || tmp[i] == '\\')
		{
			tmp[i] = '\0';
			return (1);
		}
	}
	return (0);
}

static int	mkdir_recursive(char *tmp)
{
	size_t	i;
	char	saved;
	
	i = 1;
	while (tmp[i])
	{
		if (tmp[i] == '/' || tmp[i] == '\\')
		{
			saved = tmp[i];
			tmp[i] = '\0';
			if (mkdir_ok(tmp) != 0)
				return (-1);
			tmp[i] = saved;
		}
		i++;
	}
	return (mkdir_ok(tmp));
}

#ifdef _WIN32
FILE	*fs_fopen_shared_read(const char *path)
{
	return (_fsopen(path, "rb", _SH_DENYNO));
}
#else
FILE	*fs_fopen_shared_read(const char *path)
{
	return (fopen(path, "rb"));
}
#endif

/*
 * * Cree recursivement les dossiers parents d'un fichier.
 ** Exemple: "logs/hunt_log.csv" => cree "logs/"
 */
int	fs_mkdir_p_for_file(const char *filepath)
{
	char	tmp[1024];
	
	if (!filepath || !*filepath)
		return (-1);
	if (strlen(filepath) >= sizeof(tmp))
		return (-1);
	strcpy(tmp, filepath);
	if (!cut_to_parent(tmp))
		return (0);
	return (mkdir_recursive(tmp));
}

#ifdef _WIN32
static int	is_dir(const char *p)
{
	struct _stat	st;
	
	if (_stat(p, &st) != 0)
		return (0);
	return ((st.st_mode & _S_IFDIR) != 0);
}
#else
static int	is_dir(const char *p)
{
	struct stat	st;
	
	if (stat(p, &st) != 0)
		return (0);
	return (S_ISDIR(st.st_mode));
}
#endif

int	fs_ensure_dir(const char *dir)
{
	if (!dir || !*dir)
		return (-1);
	if (TM_MKDIR(dir) == 0)
		return (0);
	if (errno == EEXIST && is_dir(dir))
		return (0);
	return (-1);
}

int	fs_file_exists(const char *path)
{
	FILE	*f;
	
	if (!path)
		return (0);
	f = fopen(path, "rb");
	if (!f)
		return (0);
	fclose(f);
	return (1);
}

long	fs_file_size(const char *path)
{
	FILE	*f;
	long	sz;
	
	if (!path)
		return (-1);
	f = fopen(path, "rb");
	if (!f)
		return (-1);
	if (fseek(f, 0, SEEK_END) != 0)
	{
		fclose(f);
		return (-1);
	}
	sz = ftell(f);
	fclose(f);
	return (sz);
}

int	fs_truncate_fp(FILE *f, long new_size)
{
	if (!f)
		return (-1);
	if (new_size < 0)
		new_size = 0;
	fflush(f);
#ifdef _WIN32
	{
		int fd = _fileno(f);
		if (fd < 0)
			return (-1);
		if (_chsize_s(fd, (size_t)new_size) != 0)
			return (-1);
		return (0);
	}
#else
	{
		int fd = fileno(f);
		if (fd < 0)
			return (-1);
		if (ftruncate(fd, (off_t)new_size) != 0)
			return (-1);
		return (0);
	}
#endif
}


/* -------------------------------------------------------------------------- */
/* Paths helpers (exe dir, join, chdir)                                       */
/* -------------------------------------------------------------------------- */

#ifdef _WIN32
# include <windows.h>
# define TM_CHDIR _chdir
# define PATH_SEP '\\'
#else
# include <unistd.h>
# include <limits.h>
# define TM_CHDIR chdir
# define PATH_SEP '/'
#endif

int fs_chdir(const char *path)
{
	if (!path || !*path)
		return (-1);
	return (TM_CHDIR(path));
}

int fs_path_parent(char *out, size_t outsz, const char *path)
{
	size_t	len;
	size_t	i;

	if (!out || outsz == 0 || !path || !*path)
		return (-1);
	len = strlen(path);
	if (len + 1 > outsz)
		return (-1);
	memcpy(out, path, len + 1);
	i = len;
	while (i > 0)
	{
		i--;
		if (out[i] == '/' || out[i] == '\\')
		{
			out[i] = '\0';
			return (0);
		}
	}
	out[0] = '\0';
	return (-1);
}

int fs_path_join(char *out, size_t outsz, const char *a, const char *b)
{
	size_t	la;
	size_t	lb;
	int		need_sep;

	if (!out || outsz == 0)
		return (-1);
	out[0] = '\0';
	if (!a || !*a)
		a = "";
	if (!b || !*b)
		b = "";
	la = strlen(a);
	lb = strlen(b);
	need_sep = 0;
	if (la > 0 && a[la - 1] != '/' && a[la - 1] != '\\' && lb > 0
		&& b[0] != '/' && b[0] != '\\')
		need_sep = 1;
	if (la + lb + (need_sep ? 1 : 0) + 1 > outsz)
		return (-1);
	memcpy(out, a, la);
	if (need_sep)
		out[la++] = PATH_SEP;
	memcpy(out + la, b, lb);
	out[la + lb] = '\0';
	return (0);
}

int fs_get_exe_dir(char *out, size_t outsz, const char *argv0)
{
	char	full[2048];
	int		rc;

	if (!out || outsz == 0)
		return (-1);
	out[0] = '\0';
	full[0] = '\0';

#ifdef _WIN32
	{
		DWORD	n = GetModuleFileNameA(NULL, full, (DWORD)(sizeof(full) - 1));
		if (n > 0 && n < sizeof(full))
			full[n] = '\0';
	}
#else
	{
		/* Not relying on /proc: argv0 fallback below is enough here. */
	}
#endif
	if (full[0] == '\0' && argv0 && argv0[0])
	{
		/* Fallback: argv0 (may be relative). */
		strncpy(full, argv0, sizeof(full) - 1);
		full[sizeof(full) - 1] = '\0';
	}

	rc = fs_path_parent(out, outsz, full);
	return (rc);
}
