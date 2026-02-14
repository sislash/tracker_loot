/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   session.c                                          :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: login <login@student.42.fr>                +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/01/31 00:00:00 by login             #+#    #+#             */
/*   Updated: 2026/01/31 00:00:00 by login            ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "session.h"

#include "csv_index.h"
#include "fs_utils.h"

#include <stdio.h>
#include <string.h>
#include <limits.h>

int	session_load_range(const char *path, long *out_start, long *out_end)
{
	FILE	*f;
	long	s;
	long	e;

	if (out_start)
		*out_start = 0;
	if (out_end)
		*out_end = -1;
	if (!path || !out_start || !out_end)
		return (0);
	f = fopen(path, "rb");
	if (!f)
		return (0);
	s = 0;
	e = -1;
	if (fscanf(f, "%ld %ld", &s, &e) < 1)
	{
		fclose(f);
		return (0);
	}
	fclose(f);
	if (s < 0)
		s = 0;
	/* end can be -1 (EOF) or >= start */
	if (e >= 0 && e < s)
		e = s;
	*out_start = s;
	*out_end = e;
	return (1);
}

int	session_save_range(const char *path, long start, long end)
{
	FILE	*f;

	if (!path)
		return (-1);
	if (start < 0)
		start = 0;
	if (end >= 0 && end < start)
		end = start;
	f = fopen(path, "wb");
	if (!f)
		return (-1);
	fprintf(f, "%ld %ld\n", start, end);
	fclose(f);
	return (0);
}

int	session_clear_range(const char *path)
{
	if (!path)
		return (-1);
	/* remove() is portable (Windows/Linux). */
	remove(path);
	return (0);
}

long	session_load_offset(const char *path)
{
	FILE	*f;
	long	v;
	
	if (!path)
		return (0);
	f = fopen(path, "rb");
	if (!f)
		return (0);
	v = 0;
	fscanf(f, "%ld", &v);
	fclose(f);
	return (v);
}

int	session_save_offset(const char *path, long offset)
{
	FILE	*f;
	
	if (!path)
		return (-1);
	f = fopen(path, "wb");
	if (!f)
		return (-1);
	fprintf(f, "%ld", offset);
	fclose(f);
	return (0);
}

long	session_count_data_lines(const char *csv_path)
{
	CsvIndexState	st;
	CsvIndexReport	rep;
	long			sz;
	FILE	*f;
	char	buf[4096];
	long	lines;
	int	first;
	
	if (!csv_path)
		return (0);
	/* Fast path: use sparse index state if available (O(1)). */
	sz = fs_file_size(csv_path);
	if (csv_index_state_load_ex(csv_path, &st, &rep))
	{
		if (sz < 0 || (unsigned long long)sz == st.bytes)
		{
			if (st.data_rows > (unsigned long long)LONG_MAX)
				return (LONG_MAX);
			return ((long)st.data_rows);
		}
	}
	/* Try to rebuild the state once (still cheaper than repeated scans). */
	if (csv_index_state_rebuild_ex(csv_path, &st, &rep))
	{
		(void)csv_index_state_store_ex(csv_path, &st, NULL);
		if (st.data_rows > (unsigned long long)LONG_MAX)
			return (LONG_MAX);
		return ((long)st.data_rows);
	}
	f = fopen(csv_path, "rb");
	if (!f)
		return (0);
	lines = 0;
	first = 1;
	while (fgets(buf, (int)sizeof(buf), f))
	{
		if (first)
		{
			first = 0;
			if (strstr(buf, "timestamp")
				&& (strstr(buf, "event_type") || strstr(buf, ",type,")))
				continue;
		}
		lines++;
	}
	fclose(f);
	if (lines < 0)
		lines = 0;
	return (lines);
}
