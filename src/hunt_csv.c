/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   hunt_csv.c                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: tracker_loot                                   +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/02/12                                #+#    #+#             */
/*   Updated: 2026/02/12                                ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

/* localtime_r() availability on some strict C99 builds */
#ifndef _WIN32
# ifndef _POSIX_C_SOURCE
#  define _POSIX_C_SOURCE 200809L
# endif
#endif

#include "hunt_csv.h"

#include "csv.h"
#include "eu_economy.h"
#include "fs_utils.h"
#include "tm_string.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ----------------------------- small parsers ----------------------------- */

static int	parse_int64(const char *s, int64_t *out)
{
	char		*end;
	long long	v;

	if (!out)
		return (0);
	*out = 0;
	if (!s || !*s)
		return (0);
	errno = 0;
	v = strtoll(s, &end, 10);
	if (errno != 0 || end == s)
		return (0);
	while (*end && isspace((unsigned char)*end))
		end++;
	if (*end != '\0')
		return (0);
	*out = (int64_t)v;
	return (1);
}

static int	parse_uint32(const char *s, uint32_t *out)
{
	char			*end;
	unsigned long	v;

	if (!out)
		return (0);
	*out = 0;
	if (!s || !*s)
		return (0);
	errno = 0;
	v = strtoul(s, &end, 10);
	if (errno != 0 || end == s)
		return (0);
	while (*end && isspace((unsigned char)*end))
		end++;
	if (*end != '\0')
		return (0);
	*out = (uint32_t)v;
	return (1);
}

static long	parse_long_default(const char *s, long defv)
{
	char	*end;
	long	v;

	if (!s || !*s)
		return (defv);
	errno = 0;
	v = strtol(s, &end, 10);
	if (errno != 0 || end == s)
		return (defv);
	while (*end && isspace((unsigned char)*end))
		end++;
	if (*end != '\0')
		return (defv);
	return (v);
}

/* ----------------------------- header ------------------------------------ */

void	hunt_csv_ensure_header_v2(FILE *f)
{
	long	endpos;

	if (!f)
		return ;
	if (fseek(f, 0, SEEK_END) != 0)
		return ;
	endpos = ftell(f);
	if (endpos == 0)
	{
		fprintf(f,
			"timestamp_unix,event_type,target_or_item,qty,value_uPED,kill_id,flags,raw\n");
	}
	(void)fseek(f, 0, SEEK_END);
}

int	hunt_csv_repair_trailing_partial_line(FILE *f)
{
	long	sz;
	long	read_start;
	size_t	n;
	char	buf[4096];
	long	i;
	long	cut_pos;
	int	c;

	if (!f)
		return (0);
	fflush(f);
	if (fseek(f, 0, SEEK_END) != 0)
		return (0);
	sz = ftell(f);
	if (sz <= 0)
		return (1);
	/* If last byte is already '\n', nothing to repair */
	if (fseek(f, sz - 1, SEEK_SET) != 0)
		return (0);
	c = fgetc(f);
	if (c == '\n')
	{
		(void)fseek(f, 0, SEEK_END);
		return (1);
	}
	/* Scan last chunk backwards to find last complete line */
	read_start = sz - (long)sizeof(buf);
	if (read_start < 0)
		read_start = 0;
	if (fseek(f, read_start, SEEK_SET) != 0)
		return (0);
	n = fread(buf, 1, (size_t)(sz - read_start) < sizeof(buf)
			? (size_t)(sz - read_start) : sizeof(buf), f);
	if (n == 0)
		return (1);
	cut_pos = -1;
	i = (long)n - 1;
	while (i >= 0)
	{
		if (buf[i] == '\n')
		{
			cut_pos = read_start + i + 1;
			break ;
		}
		i--;
	}
	if (cut_pos < 0)
		cut_pos = 0;
	if (fs_truncate_fp(f, cut_pos) != 0)
		return (0);
	(void)fseek(f, 0, SEEK_END);
	return (1);
}

/* ----------------------------- time helpers ------------------------------ */

int	hunt_csv_ts_text_to_unix(const char *ts_text, int64_t *out_unix)
{
	struct tm	tm;
	int			yr, mo, da, ho, mi, se;
	time_t		t;

	if (!out_unix)
		return (0);
	*out_unix = 0;
	if (!ts_text || strlen(ts_text) < 19)
		return (0);
	memset(&tm, 0, sizeof(tm));
	if (sscanf(ts_text, "%d-%d-%d %d:%d:%d", &yr, &mo, &da, &ho, &mi, &se) != 6)
		return (0);
	tm.tm_year = yr - 1900;
	tm.tm_mon = mo - 1;
	tm.tm_mday = da;
	tm.tm_hour = ho;
	tm.tm_min = mi;
	tm.tm_sec = se;
	tm.tm_isdst = -1;
	t = mktime(&tm);
	if (t == (time_t)-1)
		return (0);
	*out_unix = (int64_t)t;
	return (1);
}

void	hunt_csv_format_ts_local(char *dst, size_t cap, int64_t ts_unix)
{
	time_t		t;
	struct tm	tm;

	if (!dst || cap == 0)
		return ;
	dst[0] = '\0';
	if (ts_unix <= 0)
		return ;
	t = (time_t)ts_unix;
#ifdef _WIN32
	if (localtime_s(&tm, &t) != 0)
		return ;
#else
	if (localtime_r(&t, &tm) == NULL)
		return ;
#endif
	snprintf(dst, cap, "%04d-%02d-%02d %02d:%02d:%02d",
		tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		tm.tm_hour, tm.tm_min, tm.tm_sec);
}

/* ----------------------------- row parsing -------------------------------- */

static int	parse_v2_inplace(char *line, t_hunt_csv_row_view *out)
{
	char		*cols[8];
	int64_t		ts;
	int64_t		val;
	int64_t		kid;
	uint32_t	flags;

	if (!csv_split_n_strict(line, cols, 8))
		return (0);
	if (!parse_int64(cols[0], &ts))
		return (0);
	out->ts_unix = ts;
	out->type = cols[1] ? cols[1] : "";
	out->name = cols[2] ? cols[2] : "";
	out->qty = parse_long_default(cols[3], 0);
	val = 0;
	out->has_value = parse_int64(cols[4], &val);
	out->value_uPED = (tm_money_t)val;
	kid = 0;
	parse_int64(cols[5], &kid);
	out->kill_id = kid;
	flags = 0;
	parse_uint32(cols[6], &flags);
	out->flags = flags;
	out->raw = cols[7] ? cols[7] : "";
	/* SWEAT safety: if value missing but qty present, compute it. */
	if (strcmp(out->type, "SWEAT") == 0 && (!out->has_value || out->value_uPED == 0))
	{
		if (out->qty > 0)
		{
			out->value_uPED = (tm_money_t)out->qty
				* (tm_money_t)EU_SWEAT_uPED_PER_BOTTLE;
			out->has_value = 1;
		}
	}
	return (1);
}

int	hunt_csv_parse_row_inplace(char *line, t_hunt_csv_row_view *out)
{
	if (!line || !out)
		return (0);
	memset(out, 0, sizeof(*out));
	/* Trim CRLF */
	tm_chomp_crlf(line);
	return (parse_v2_inplace(line, out));
}

/* ----------------------------- tail scan ---------------------------------- */

int64_t	hunt_csv_tail_max_kill_id(const char *path)
{
	FILE				*f;
	long				sz;
	long				start;
	char				line[8192];
	t_hunt_csv_row_view	row;
	int64_t				max_id;

	if (!path)
		return (0);
	f = fopen(path, "rb");
	if (!f)
		return (0);
	if (fseek(f, 0, SEEK_END) != 0)
	{
		fclose(f);
		return (0);
	}
	sz = ftell(f);
	if (sz < 0)
	{
		fclose(f);
		return (0);
	}
	start = sz - (long)(128 * 1024);
	if (start < 0)
		start = 0;
	fseek(f, start, SEEK_SET);
	/* If we started in the middle of a line, drop first partial line */
	if (start > 0)
		(void)fgets(line, sizeof(line), f);
	max_id = 0;
	while (fgets(line, sizeof(line), f))
	{
		if (!hunt_csv_parse_row_inplace(line, &row))
			continue ;
		if (row.kill_id > max_id)
			max_id = row.kill_id;
	}
	fclose(f);
	return (max_id);
}

/* ----------------------------- row writing -------------------------------- */

int	hunt_csv_write_v2(FILE *f, int64_t ts_unix,
			const char *type, const char *name,
			long qty, tm_money_t value_uPED,
			int64_t kill_id, uint32_t flags,
			const char *raw)
{
	char	s_ts[32];
	char	s_qty[32];
	char	s_val[32];
	char	s_kid[32];
	char	s_flags[32];

	if (!f)
		return (-1);
	snprintf(s_ts, sizeof(s_ts), "%lld", (long long)ts_unix);
	snprintf(s_qty, sizeof(s_qty), "%ld", qty);
	snprintf(s_val, sizeof(s_val), "%lld", (long long)value_uPED);
	snprintf(s_kid, sizeof(s_kid), "%lld", (long long)kill_id);
	snprintf(s_flags, sizeof(s_flags), "%u", (unsigned)flags);

	csv_write_field(f, s_ts);
	fputc(',', f);
	csv_write_field(f, type ? type : "");
	fputc(',', f);
	csv_write_field(f, name ? name : "");
	fputc(',', f);
	csv_write_field(f, s_qty);
	fputc(',', f);
	csv_write_field(f, s_val);
	fputc(',', f);
	csv_write_field(f, s_kid);
	fputc(',', f);
	csv_write_field(f, s_flags);
	fputc(',', f);
	csv_write_field(f, raw ? raw : "");
	fputc('\n', f);
	return (0);
}
