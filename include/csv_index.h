#ifndef CSV_INDEX_H
# define CSV_INDEX_H

# include <stddef.h>
# include <stdint.h>

/*
 * Lightweight sparse index to accelerate "load since timestamp".
 *
 * Index file path: <csv_path>.idx
 *
 * File format (text, UTF-8):
 *   #csv_index_v1 stride=<N>
 *   row_index,timestamp,byte_offset
 */

typedef enum e_csv_index_status
{
	CSV_INDEX_OK = 0,
	CSV_INDEX_OPEN_FAILED,
	CSV_INDEX_IO_ERROR,
	CSV_INDEX_BAD_FORMAT,
	CSV_INDEX_OOM
} 	CsvIndexStatus;

typedef struct s_csv_index_options
{
	size_t	stride_rows; /* e.g. 1024 */
} 	CsvIndexOptions;

typedef struct s_csv_index_report
{
	CsvIndexStatus	status;
	size_t			stride_rows;
	size_t			entries;
	char			index_path[512];
	char			error[256];
} 	CsvIndexReport;

/* Persistent state to support incremental (append-time) sparse indexing.
 * State file path: <csv_path>.idxstate
 */
typedef struct s_csv_index_state
{
	unsigned long long	data_rows; /* number of data rows (header excluded) */
	unsigned long long	bytes;     /* last known file size in bytes */
	long long			last_ts;   /* last appended/seen timestamp (best-effort) */
} 	CsvIndexState;

void	csv_index_options_default(CsvIndexOptions *opt);
int		csv_index_build_ex(const char *csv_path,
						const CsvIndexOptions *opt,
						CsvIndexReport *out_report);

/* Incremental helpers (best-effort, safe under external locking). */
int		csv_index_state_load_ex(const char *csv_path,
						CsvIndexState *out_state,
						CsvIndexReport *out_report);

int		csv_index_state_store_ex(const char *csv_path,
						const CsvIndexState *state,
						CsvIndexReport *out_report);

/* Rebuilds state by scanning the CSV (used when state is missing/corrupt). */
int		csv_index_state_rebuild_ex(const char *csv_path,
						CsvIndexState *out_state,
						CsvIndexReport *out_report);

/* Appends a checkpoint to <csv_path>.idx if row_index is on stride.
 * Creates or rebuilds the index if missing/mismatched.
 */
int		csv_index_maybe_append_checkpoint_ex(const char *csv_path,
						size_t stride_rows,
						unsigned long long row_index,
						long long timestamp,
						unsigned long long byte_offset,
						CsvIndexReport *out_report);

/*
 * Finds the best checkpoint whose timestamp <= target timestamp.
 * Returns 1 on success, 0 if no index or no checkpoint found.
 */
int		csv_index_lookup_offset_ex(const char *csv_path,
						long long timestamp,
						unsigned long long *out_offset,
						long long *out_checkpoint_ts,
						unsigned long long *out_checkpoint_row,
						CsvIndexReport *out_report);

int		csv_index_remove(const char *csv_path);

#endif
