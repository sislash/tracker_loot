#ifndef SESSION_EXPORT_H
# define SESSION_EXPORT_H

# include "tracker_stats.h"

/*
** Append one line to sessions_stats.csv. v2 adds (start_offset,end_offset)
** at the end for "load session" features.
**
** Offsets are in data-line indices (0-based, header ignored), range is [start,end).
*/
int	session_export_stats_csv_ex(const char *out_csv_path,
                             const t_hunt_stats *s,
                             const char *session_start_ts,
                             const char *session_end_ts,
                             long start_offset, long end_offset);

/* Backward-compatible wrapper (no offsets). */
int	session_export_stats_csv(const char *out_csv_path,
                             const t_hunt_stats *s,
                             const char *session_start_ts,
                             const char *session_end_ts);

int	session_extract_range_timestamps_ex(const char *hunt_csv_path,
                                     long start_offset, long end_offset,
                                     char *out_start, size_t out_start_sz,
                                     char *out_end, size_t out_end_sz);

/* Backward-compatible wrapper: [start_offset, EOF). */
int	session_extract_range_timestamps(const char *hunt_csv_path, long start_offset,
                                     char *out_start, size_t out_start_sz,
                                     char *out_end, size_t out_end_sz);

#endif
