/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   globals_engine.c                                   :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: login <login@student.42.fr>                +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/01/31 00:00:00 by login             #+#    #+#             */
/*   Updated: 2026/01/31 00:00:00 by login            ###   ###########       */
/*                                                                            */
/* ************************************************************************** */

#include "globals_engine.h"
#include "globals_parser.h"
#include "csv.h"
#include "fs_utils.h"
#include "ui_utils.h"

#include <stdio.h>

static int	append_event(FILE *out, const t_globals_event *ev)
{
    if (!out || !ev)
        return (-1);
    csv_write_row6(out, ev->ts, ev->type, ev->name, "", ev->value, ev->raw);
    fflush(out);
    return (0);
}

static void	process_line(FILE *out, const char *line)
{
    t_globals_event	ev;
    
    if (globals_parse_line(line, &ev))
        append_event(out, &ev);
}

int	globals_run_replay(const char *chatlog_path, const char *csv_path,
				atomic_int *stop_flag)
{
    FILE	*in;
    FILE	*out;
    char	buf[2048];
    
    in = fs_fopen_shared_read(chatlog_path);
    if (!in)
        return (-1);
    out = fopen(csv_path, "ab");
    if (!out)
    {
        fclose(in);
        return (-1);
    }
    csv_ensure_header6(out);
	while (!stop_flag || atomic_load(stop_flag) == 0)
    {
        if (!fgets(buf, sizeof(buf), in))
            break ;
        process_line(out, buf);
    }
    fclose(out);
    fclose(in);
    return (0);
}

int	globals_run_live(const char *chatlog_path, const char *csv_path,
				atomic_int *stop_flag)
{
    FILE	*in;
    FILE	*out;
    char	buf[2048];
    long	last_pos;
    
    in = fs_fopen_shared_read(chatlog_path);
    if (!in)
        return (-1);
    out = fopen(csv_path, "ab");
    if (!out)
    {
        fclose(in);
        return (-1);
    }
    csv_ensure_header6(out);
    fseek(in, 0, SEEK_END);
    last_pos = ftell(in);
	while (!stop_flag || atomic_load(stop_flag) == 0)
    {
        if (fgets(buf, sizeof(buf), in))
        {
            process_line(out, buf);
            last_pos = ftell(in);
        }
        else
        {
            long	sz;
            
            clearerr(in);
            sz = fs_file_size(chatlog_path);
            if (sz >= 0 && sz < last_pos)
            {
                fclose(in);
                in = fs_fopen_shared_read(chatlog_path);
                if (!in)
                {
                    ui_sleep_ms(250);
                    continue ;
                }
                fseek(in, 0, SEEK_END);
                last_pos = ftell(in);
            }
            else if (sz >= 0 && sz > last_pos)
                fseek(in, last_pos, SEEK_SET);
            else
            {
                ui_sleep_ms(200);
                fseek(in, last_pos, SEEK_SET);
            }
        }
    }
    fclose(out);
    fclose(in);
    return (0);
}
