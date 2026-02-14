/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   session_export.c                                   :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: login <login@student.42.fr>                +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/01/31 00:00:00 by login             #+#    #+#             */
/*   Updated: 2026/01/31 00:00:00 by login             ###   ########.fr      */
/*                                                                            */
/* ************************************************************************** */

#define _CRT_SECURE_NO_WARNINGS

#include "session_export.h"
#include "csv.h"
#include "core_paths.h"
#include "hunt_csv.h"
#include "mob_selected.h"
#include "tm_money.h"

#include <stdio.h>
#include <string.h>

typedef struct s_session_bufs
{
    char	kills[64];
    char	shots[64];
    char	loot[64];
    char	exp[64];
    char	net[64];
    char	ret[64];
    char	start_off[64];
    char	end_off[64];
}	t_session_bufs;

static void	str_copy(char *dst, size_t dstsz, const char *src)
{
    if (!dst || dstsz == 0)
        return ;
    if (!src)
    {
        dst[0] = '\0';
        return ;
    }
    snprintf(dst, dstsz, "%s", src);
}

static void	init_out_ranges(char *out_start, char *out_end)
{
    if (out_start)
        out_start[0] = '\0';
    if (out_end)
        out_end[0] = '\0';
}

static int	looks_like_hunt_csv_header(const char *line)
{
	if (!line)
		return (0);
	if (strstr(line, "timestamp")
		&& (strstr(line, "event_type") || strstr(line, ",type,")))
		return (1);
	return (0);
}

static int	format_ts_from_hunt_row(char *line, char *out, size_t outsz)
{
	t_hunt_csv_row_view	row;
	char				ts[64];

	if (!line || !out || outsz == 0)
		return (0);
	if (!hunt_csv_parse_row_inplace(line, &row))
		return (0);
	if (row.ts_unix <= 0)
		return (0);
	hunt_csv_format_ts_local(ts, sizeof(ts), row.ts_unix);
	if (ts[0] == '\0')
		snprintf(ts, sizeof(ts), "%lld", (long long)row.ts_unix);
	str_copy(out, outsz, ts);
	return (1);
}


static void	process_range_file_ex(FILE *f, long start_offset, long end_offset,
							  char *out_start, size_t out_start_sz,
							  char *out_end, size_t out_end_sz)
{
	char	line[1024];
	char	work[1024];
	long	data_idx;
	int		first;
	int		first_data_seen;

	data_idx = 0;
	first = 1;
	first_data_seen = 0;
	while (fgets(line, sizeof(line), f))
	{
		if (first)
		{
			first = 0;
			if (looks_like_hunt_csv_header(line))
				continue ;
		}
		if (data_idx < start_offset)
		{
			data_idx++;
			continue ;
		}
		if (end_offset >= 0 && data_idx >= end_offset)
			break ;
		str_copy(work, sizeof(work), line);
		if (format_ts_from_hunt_row(work, out_end, out_end_sz))
		{
			if (!first_data_seen)
			{
				first_data_seen = 1;
				str_copy(out_start, out_start_sz, out_end);
			}
		}
		data_idx++;
	}
}

int	session_extract_range_timestamps_ex(const char *hunt_csv_path,
							 long start_offset, long end_offset,
							 char *out_start, size_t out_start_sz,
							 char *out_end, size_t out_end_sz)
{
    FILE	*f;
    
    init_out_ranges(out_start, out_end);
	if (!hunt_csv_path)
        return (0);
	if (start_offset < 0)
		start_offset = 0;
	if (end_offset >= 0 && end_offset < start_offset)
		end_offset = start_offset;
	f = fopen(hunt_csv_path, "rb");
	if (!f)
	{
		return (0);
	}
	process_range_file_ex(f, start_offset, end_offset, out_start, out_start_sz,
					   out_end, out_end_sz);
	fclose(f);
	return (1);
}

int	session_extract_range_timestamps(const char *hunt_csv_path, long start_offset,
							 char *out_start, size_t out_start_sz,
							 char *out_end, size_t out_end_sz)
{
	return (session_extract_range_timestamps_ex(hunt_csv_path, start_offset, -1,
			out_start, out_start_sz, out_end, out_end_sz));
}

static void	ensure_sessions_header(FILE *f)
{
    long	endpos;
    
    if (!f)
        return ;
    fseek(f, 0, SEEK_END);
    endpos = ftell(f);
    if (endpos == 0)
    {
        fprintf(f, "session_start,session_end,weapon,kills,shots,");
		fprintf(f, "loot_ped,expense_ped,net_ped,return_pct,start_offset,end_offset,mob\n");
    }
}

static double	safe_return_pct(tm_money_t loot, tm_money_t expense)
{
	if (expense <= 0)
        return (0.0);
	return (((double)loot / (double)expense) * 100.0);
}

static void	build_session_bufs_ex(const t_hunt_stats *s, t_session_bufs *b,
							 long start_offset, long end_offset)
{
    snprintf(b->kills, sizeof(b->kills), "%ld", (long)s->kills);
    snprintf(b->shots, sizeof(b->shots), "%ld", (long)s->shots);
	tm_money_format_ped4(b->loot, sizeof(b->loot), s->loot_ped);
	tm_money_format_ped4(b->exp, sizeof(b->exp), s->expense_used);
	tm_money_format_ped4(b->net, sizeof(b->net), s->net_ped);
    snprintf(b->ret, sizeof(b->ret), "%.2f",
             safe_return_pct(s->loot_ped, s->expense_used));
	if (start_offset >= 0)
		snprintf(b->start_off, sizeof(b->start_off), "%ld", start_offset);
	else
		b->start_off[0] = '\0';
	if (end_offset >= 0)
		snprintf(b->end_off, sizeof(b->end_off), "%ld", end_offset);
	else
		b->end_off[0] = '\0';
}

static void	write_fields_list(FILE *f, const char **fields, int count)
{
    int	i;
    
    i = 0;
    while (i < count)
    {
        csv_write_field(f, fields[i]);
        if (i + 1 < count)
            fputc(',', f);
        else
            fputc('\n', f);
        i++;
    }
}

static void	write_sessions_row_ex(FILE *f, const t_hunt_stats *s,
							   const char *start_ts, const char *end_ts,
							   long start_offset, long end_offset,
							   const char *mob_name)
{
    t_session_bufs	b;
    const char		*wname;
	const char		*fields[12];
    
	build_session_bufs_ex(s, &b, start_offset, end_offset);
    wname = (s->weapon_name[0] ? s->weapon_name : "");
    fields[0] = start_ts;
    fields[1] = end_ts;
    fields[2] = wname;
    fields[3] = b.kills;
    fields[4] = b.shots;
    fields[5] = b.loot;
    fields[6] = b.exp;
    fields[7] = b.net;
    fields[8] = b.ret;
	fields[9] = b.start_off;
	fields[10] = b.end_off;
	fields[11] = (mob_name ? mob_name : "");
	write_fields_list(f, fields, 12);
}

int	session_export_stats_csv_ex(const char *out_csv_path, const t_hunt_stats *s,
						 const char *session_start_ts,
						 const char *session_end_ts,
						 long start_offset, long end_offset)
{
    FILE		*f;
    const char	*start_ts;
    const char	*end_ts;
	char		mob[128];
    
    if (!out_csv_path || !s)
        return (0);
    f = fopen(out_csv_path, "ab");
    if (!f)
        return (0);
    ensure_sessions_header(f);
    start_ts = (session_start_ts ? session_start_ts : "");
    end_ts = (session_end_ts ? session_end_ts : "");
	mob[0] = '\0';
	(void)mob_selected_load(tm_path_mob_selected(), mob, sizeof(mob));
	mob_selected_sanitize(mob);
	write_sessions_row_ex(f, s, start_ts, end_ts, start_offset, end_offset, mob);
    fclose(f);
    return (1);
}

int	session_export_stats_csv(const char *out_csv_path, const t_hunt_stats *s,
						 const char *session_start_ts, const char *session_end_ts)
{
	return (session_export_stats_csv_ex(out_csv_path, s, session_start_ts,
			session_end_ts, -1, -1));
}
