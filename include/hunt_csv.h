/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   hunt_csv.h                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: tracker_loot                                   +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/02/12                                #+#    #+#             */
/*   Updated: 2026/02/12                                ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef HUNT_CSV_H
# define HUNT_CSV_H

/*
** Hunt CSV (V2 strict) = "coeur de verite" pour la chasse.
**
** Format (8 colonnes):
**   timestamp_unix,event_type,target_or_item,qty,value_uPED,kill_id,flags,raw
**
** - timestamp_unix : int64 seconds (local time converted via mktime)
** - value_uPED     : int64 fixed point (see tm_money.h) 1 PED = 10000 uPED
** - kill_id        : int64 (incremental id for each KILL)
** - flags          : uint32 bitfield
**     bit0: has_value
**     bit1: has_kill_id
*/

# include <stdint.h>
# include <stddef.h>
# include <stdio.h>

# include "tm_money.h"

typedef struct s_hunt_csv_row_view
{
	int64_t		ts_unix;      /* seconds */
	const char	*type;
	const char	*name;
	long		qty;
	tm_money_t	value_uPED;
	int			has_value;
	int64_t		kill_id;
	uint32_t	flags;
	const char	*raw;
}   t_hunt_csv_row_view;

/* Ensure V2 header if file is empty. Keeps file position at end. */
void        hunt_csv_ensure_header_v2(FILE *f);

/*
 * Crash-safety: if the CSV ended with a partial line (no trailing '\n'),
 * truncate it back to the last complete row.
 * Returns 1 on success (or nothing to do), 0 on error.
 */
int		hunt_csv_repair_trailing_partial_line(FILE *f);

/* Parse one CSV line (in-place). Returns 1 on success, 0 otherwise. */
int         hunt_csv_parse_row_inplace(char *line, t_hunt_csv_row_view *out);

/* Timestamp conversions */
int         hunt_csv_ts_text_to_unix(const char *ts_text, int64_t *out_unix);
void        hunt_csv_format_ts_local(char *dst, size_t cap, int64_t ts_unix);

/* Best-effort scan of last chunk to resume kill_id. */
int64_t     hunt_csv_tail_max_kill_id(const char *path);

/* Write one V2 row. Returns 0 on success, -1 on error. */
int         hunt_csv_write_v2(FILE *f, int64_t ts_unix,
					const char *type, const char *name,
					long qty, tm_money_t value_uPED,
					int64_t kill_id, uint32_t flags,
					const char *raw);

#endif
