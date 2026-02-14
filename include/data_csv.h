#ifndef DATA_CSV_H
# define DATA_CSV_H

# include <stddef.h>
# include <stdint.h>

/* Optional: sparse index for fast timestamp seeks (load_since_indexed). */
# include "csv_index.h"

typedef struct s_data_row
{
	long long	timestamp;
	char		*event_type;      /* UTF-8, heap allocated */
	double		value;
	int			hit_count_reset;  /* 0/1 */
}	t_data_row;

typedef struct s_data_struct
{
	t_data_row	*rows;
	size_t		count;
	size_t		capacity;
}	DataStruct;

/* -------------------- CSV load configuration/reporting -------------------- */

typedef enum e_csv_error_policy
{
	CSV_ERROR_SKIP = 0,
	CSV_ERROR_STOP = 1
}	CsvErrorPolicy;

typedef enum e_csv_load_status
{
	CSV_LOAD_OK = 0,
	CSV_LOAD_OPEN_FAILED,
	CSV_LOAD_IO_ERROR,
	CSV_LOAD_CHECKSUM_MISMATCH,
	CSV_LOAD_TOO_BIG,
	CSV_LOAD_TOO_MANY_ROWS,
	CSV_LOAD_BAD_FORMAT,
	CSV_LOAD_OOM
}	CsvLoadStatus;

typedef struct s_csv_load_options
{
	CsvErrorPolicy	policy;            /* skip malformed lines or stop on first error */
	int				log_stderr;        /* 1 => limited warnings to stderr */
	size_t			max_file_bytes;    /* 0 => unlimited (not recommended) */
	size_t			max_rows;          /* 0 => unlimited (not recommended) */
	size_t			max_error_samples; /* max per-line warnings (when log_stderr=1) */
	size_t			max_line_bytes;    /* 0 => default internal limit */
	int				verify_crc32;      /* 1 => enforce #crc32 footer when present */
}	CsvLoadOptions;

typedef struct s_csv_load_report
{
	CsvLoadStatus	status;
	size_t			first_error_line;  /* 0 if none */
	size_t			last_error_line;   /* 0 if none */
	size_t			loaded_rows;
	size_t			skipped_lines;
	size_t			bytes_read;
	char			first_error[256];  /* reason + preview (first error only) */
	char			delimiter;         /* detected delimiter: ',' or ';' */
	int				has_crc32;         /* footer was present */
	uint32_t		expected_crc32;
	uint32_t		computed_crc32;
	int				crc32_ok;          /* 1 if verified and matches */
}	CsvLoadReport;

void				csv_load_options_default(CsvLoadOptions *opt);
DataStruct		*load_from_csv_ex(const char *filename,
						const CsvLoadOptions *opt, CsvLoadReport *out_report);
DataStruct		*load_from_csv_since_ex(const char *filename,
					long long min_timestamp,
					const CsvLoadOptions *opt, CsvLoadReport *out_report);

DataStruct		*load_from_csv_since_indexed_ex(const char *filename,
					long long min_timestamp,
					const CsvLoadOptions *opt, CsvLoadReport *out_report);

const CsvLoadReport	*csv_last_report(void);

/* -------------------- CSV write configuration/reporting ------------------- */

typedef enum e_csv_write_status
{
	CSV_WRITE_OK = 0,
	CSV_WRITE_OPEN_FAILED,
	CSV_WRITE_IO_ERROR,
	CSV_WRITE_RENAME_FAILED,
	CSV_WRITE_OOM
}	CsvWriteStatus;

typedef struct s_csv_write_options
{
	char			delimiter;          /* ',' (default) or ';' */
	int				atomic_write;       /* 1 => write to tmp + rename (recommended) */
	int				fsync_on_close;     /* 1 => fflush + fsync/_commit (best effort) */
	int				write_crc32_footer; /* 1 => add final line: #crc32=XXXXXXXX */
}	CsvWriteOptions;

typedef struct s_csv_write_report
{
	CsvWriteStatus	status;
	char			delimiter;
	int				has_crc32;
	uint32_t		crc32;
	char			error[256];
}	CsvWriteReport;

void				csv_write_options_default(CsvWriteOptions *opt);
int				save_to_csv_ex(const char *filename, DataStruct *data,
						const CsvWriteOptions *opt, CsvWriteReport *out_report);

/* Streaming writer (large data / real-time logging) */

typedef struct s_csv_writer	CsvWriter;

CsvWriter		*csv_writer_open_ex(const char *filename,
						const CsvWriteOptions *opt, CsvWriteReport *out_report);
int				csv_writer_write_row(CsvWriter *w, long long timestamp,
						const char *event_type, double value, int hit_count_reset);
int				csv_writer_write_data_row(CsvWriter *w, const t_data_row *row);
int	csv_writer_close(CsvWriter *w);

/* -------------------- Journal append (fast incremental logging) ------------ */
/*
 * Journal strategy:
 * - Snapshot file: <base>.csv (your normal file, can have #crc32 footer)
 * - Journal file : <base>.csv.journal  (append-only, no footer; may be compacted later)
 * On load, you can read snapshot then apply journal rows.
 */

typedef enum e_csv_journal_status
{
	CSV_JOURNAL_OK = 0,
	CSV_JOURNAL_LOCKED,
	CSV_JOURNAL_OPEN_FAILED,
	CSV_JOURNAL_IO_ERROR,
	CSV_JOURNAL_OOM
}	CsvJournalStatus;

typedef struct s_csv_journal_options
{
	char	delimiter;			/* , (default) or ; */
	int		fsync_on_append;	/* 1 => fflush + fsync/ _commit */
	int		use_lock_file;		/* 1 => create <journal>.lock with O_EXCL */
	/* Optional: maintain a sparse index for the journal itself (<journal>.idx)
	 * incrementally at append time.
	 *
	 * - update_journal_index: enable/disable.
	 * - journal_index_stride_rows: checkpoint stride (e.g. 1024).
	 * - journal_index_best_effort: if 1 (default), journal appends still succeed
	 *   even if the index/state update fails.
	 */
	int		update_journal_index;
	size_t	journal_index_stride_rows;
	int		journal_index_best_effort;
}	CsvJournalOptions;

typedef struct s_csv_journal_report
{
	CsvJournalStatus	status;
	char				delimiter;
	char				journal_path[512];
	int				index_updated;
	CsvIndexReport		index_report;
	char				error[256];
}	CsvJournalReport;

typedef struct s_csv_journal_load_report
{
	CsvLoadReport	base;
	CsvLoadReport	journal;
	int				journal_present;
}	CsvJournalLoadReport;

void		csv_journal_options_default(CsvJournalOptions *opt);
int		csv_journal_append_row_ex(const char *base_csv,
				long long timestamp, const char *event_type,
				double value, int hit_count_reset,
				const CsvJournalOptions *opt, CsvJournalReport *out_report);
int		csv_journal_append_data_row_ex(const char *base_csv,
				const t_data_row *row,
				const CsvJournalOptions *opt, CsvJournalReport *out_report);

DataStruct	*load_from_csv_journal_ex(const char *base_csv,
				const CsvLoadOptions *opt,
				CsvJournalLoadReport *out_report);
DataStruct	*load_from_csv_journal_since_ex(const char *base_csv,
				long long min_timestamp,
				const CsvLoadOptions *opt,
				CsvJournalLoadReport *out_report);
DataStruct	*load_from_csv_journal_since_indexed_ex(const char *base_csv,
				long long min_timestamp,
				const CsvLoadOptions *opt,
				CsvJournalLoadReport *out_report);

DataStruct	*load_from_csv_journal(const char *base_csv);

/* -------------------- Journal rotation / compaction ------------------------ */

typedef enum e_csv_journal_rotate_status
{
	CSV_JOURNAL_ROTATE_OK = 0,
	CSV_JOURNAL_ROTATE_NOOP,
	CSV_JOURNAL_ROTATE_LOCKED,
	CSV_JOURNAL_ROTATE_OPEN_FAILED,
	CSV_JOURNAL_ROTATE_IO_ERROR,
	CSV_JOURNAL_ROTATE_OOM
} 	CsvJournalRotateStatus;

typedef enum e_csv_journal_rotate_reason
{
	CSV_JOURNAL_ROTATE_NONE = 0,
	CSV_JOURNAL_ROTATE_BY_BYTES,
	CSV_JOURNAL_ROTATE_BY_ROWS,
	CSV_JOURNAL_ROTATE_FORCE
} 	CsvJournalRotateReason;

typedef struct s_csv_journal_rotate_options
{
	size_t	max_journal_bytes;	/* 0 => disable byte trigger */
	size_t	max_journal_rows;	/* 0 => disable row trigger (approx count) */
	int		archive_old_journal;	/* 1 => rename journal to <journal>.<ts>.<pid>.bak */
	int		rebuild_index;			/* 1 => rebuild <base>.idx after rotation/compaction */
	size_t	index_stride_rows;		/* stride for index checkpoints (e.g. 1024) */
} 	CsvJournalRotateOptions;

typedef struct s_csv_journal_rotate_report
{
	CsvJournalRotateStatus	status;
	int					rotated;
	CsvJournalRotateReason	reason;
	long					journal_size;
	size_t				approx_journal_rows;
	char					journal_path[512];
	char					archive_path[512];
	CsvWriteReport			snapshot_write;
	int				index_rebuilt;
	CsvIndexReport			index_report;
	char					error[256];
} 	CsvJournalRotateReport;

void	csv_journal_rotate_options_default(CsvJournalRotateOptions *opt);

int		csv_journal_rotate_if_needed_ex(const char *base_csv,
			const CsvJournalRotateOptions *rot_opt,
			const CsvLoadOptions *load_opt,
			const CsvWriteOptions *snapshot_write_opt,
			const CsvJournalOptions *journal_opt,
			CsvJournalRotateReport *out_report);

int		csv_journal_compact_ex(const char *base_csv,
			const CsvLoadOptions *load_opt,
			const CsvWriteOptions *snapshot_write_opt,
			const CsvJournalOptions *journal_opt,
			CsvJournalRotateReport *out_report);

/* Legacy APIs */
int				save_to_csv(const char *filename, DataStruct *data);
DataStruct		*load_from_csv(const char *filename);
void				data_struct_free(DataStruct *data);

#endif
