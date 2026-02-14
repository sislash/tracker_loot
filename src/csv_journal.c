#include "data_csv.h"
#include "csv.h"
#include "csv_index.h"
#include "fs_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <ctype.h>
#include <time.h>

#if defined(_WIN32) || defined(_WIN64)
# include <io.h>
# include <fcntl.h>
# include <sys/stat.h>
# include <process.h>
# define TM_FSEEK64 _fseeki64
# define TM_FTELL64 _ftelli64
# define TM_FILENO _fileno
# define TM_FSYNC  _commit
# define TM_GETPID _getpid
# define TM_OPEN   _open
# define TM_CLOSE  _close
# define TM_WRITE  _write
# define TM_O_EXCL  _O_EXCL
# define TM_O_CREAT _O_CREAT
# define TM_O_WRONLY _O_WRONLY
# define TM_O_BINARY _O_BINARY
# define TM_PERM (_S_IREAD | _S_IWRITE)
#else
# include <unistd.h>
# include <fcntl.h>
# include <sys/stat.h>

extern int	fseeko(FILE *stream, off_t offset, int whence);
extern off_t	ftello(FILE *stream);

# define TM_FSEEK64 fseeko
# define TM_FTELL64 ftello
# define TM_FILENO fileno
# define TM_FSYNC  fsync
# define TM_GETPID getpid
# define TM_OPEN   open
# define TM_CLOSE  close
# define TM_WRITE  write
# define TM_O_EXCL  O_EXCL
# define TM_O_CREAT O_CREAT
# define TM_O_WRONLY O_WRONLY
# define TM_O_BINARY 0
# define TM_PERM 0644
#endif

#if !defined(_WIN32) && !defined(_WIN64)
int	fileno(FILE *stream);
#endif

#define JOURNAL_SUFFIX ".journal"
#define LOCK_SUFFIX ".lock"

/* Sidecars used by incremental journal indexing. */
#define INDEX_SUFFIX ".idx"
#define INDEX_STATE_SUFFIX ".idxstate"

#define DEFAULT_ROTATE_BYTES (8u * 1024u * 1024u)

static const char	*g_header_fields[4] = {
	"timestamp",
	"event_type",
	"value",
	"hit_count_reset"
};

static void	journal_report_set(CsvJournalReport *r, CsvJournalStatus st,
				char delim, const char *jpath, const char *msg)
{
	if (!r)
		return ;
	memset(r, 0, sizeof(*r));
	r->status = st;
	r->delimiter = delim;
	if (jpath)
		snprintf(r->journal_path, sizeof(r->journal_path), "%s", jpath);
	if (msg)
		snprintf(r->error, sizeof(r->error), "%s", msg);
}

void	csv_journal_options_default(CsvJournalOptions *opt)
{
	if (!opt)
		return ;
	opt->delimiter = ',';
	opt->fsync_on_append = 0;
	opt->use_lock_file = 1;
	opt->update_journal_index = 1;
	opt->journal_index_stride_rows = 1024;
	opt->journal_index_best_effort = 1;
}

static int	tm_fsync_stream(FILE *f)
{
	int	fd;

	if (!f)
		return (0);
	if (fflush(f) != 0)
		return (0);
	fd = TM_FILENO(f);
	if (fd < 0)
		return (0);
	if (TM_FSYNC(fd) != 0)
		return (0);
	return (1);
}

/* Locale-proof: normalize comma decimals to dot. */
static void	fmt_double(char *dst, size_t cap, double v)
{
	size_t	i;

	if (!dst || cap == 0)
		return ;
	snprintf(dst, cap, "%.17g", v);
	for (i = 0; dst[i]; i++)
	{
		if (dst[i] == ',')
			dst[i] = '.';
	}
}

static int	make_path_with_suffix(char *out, size_t outsz,
				const char *base, const char *suffix)
{
	size_t	bl;
	size_t	sl;

	if (!out || outsz == 0 || !base || !suffix)
		return (0);
	bl = strlen(base);
	sl = strlen(suffix);
	if (bl + sl + 1 > outsz)
		return (0);
	memcpy(out, base, bl);
	memcpy(out + bl, suffix, sl + 1);
	return (1);
}

static int	lock_acquire(const char *lock_path, int *out_fd)
{
	int		fd;
	char	buf[128];
	int		n;

	if (!lock_path)
		return (0);
	fd = TM_OPEN(lock_path, TM_O_CREAT | TM_O_EXCL | TM_O_WRONLY | TM_O_BINARY, TM_PERM);
	if (fd < 0)
		return (0);
	if (out_fd)
		*out_fd = fd;
	n = snprintf(buf, sizeof(buf), "pid=%ld\n", (long)TM_GETPID());
	if (n > 0)
		(void)TM_WRITE(fd, buf, (unsigned int)n);
	return (1);
}

static void	lock_release(const char *lock_path, int fd)
{
	if (fd >= 0)
		TM_CLOSE(fd);
	if (lock_path)
		(void)remove(lock_path);
}

/* If the journal is missing a trailing newline (e.g. previous crash), fix it. */
static int	ensure_trailing_newline(FILE *f)
{
	long	sz;
	int		c;

	if (!f)
		return (0);
	if (fseek(f, 0, SEEK_END) != 0)
		return (1);
	sz = ftell(f);
	if (sz <= 0)
		return (1);
	if (fseek(f, -1, SEEK_END) != 0)
		return (1);
	c = fgetc(f);
	if (c == '\n')
		return (1);
	if (fseek(f, 0, SEEK_END) != 0)
		return (0);
	if (fwrite("\n", 1, 1, f) != 1)
		return (0);
	return (1);
}

static int	file_is_empty(FILE *f)
{
	long	sz;

	if (!f)
		return (1);
	if (fseek(f, 0, SEEK_END) != 0)
		return (1);
	sz = ftell(f);
	if (sz < 0)
		return (1);
	return (sz == 0);
}

int	csv_journal_append_row_ex(const char *base_csv,
			long long timestamp, const char *event_type,
			double value, int hit_count_reset,
			const CsvJournalOptions *opt_in, CsvJournalReport *out_report)
{
	CsvJournalOptions	opt;
	CsvJournalReport		rep;
	CsvIndexState			st;
	CsvIndexReport		idxrep;
	int					idx_ok;
	unsigned long long	row_start_off;
	unsigned long long	file_sz_after;
	unsigned long long	row_index;
	char				jpath[512];
	char				lpath[560];
	int					lock_fd;
	FILE				*f;
	char				ts[32];
	char				val[64];
	char				rst[16];
	const char			*fields[4];
	char				sep;

	memset(&rep, 0, sizeof(rep));
	lock_fd = -1;
	f = NULL;
	memset(&st, 0, sizeof(st));
	memset(&idxrep, 0, sizeof(idxrep));
	idx_ok = 1;
	row_start_off = 0ULL;
	file_sz_after = 0ULL;
	row_index = 0ULL;

	if (!base_csv)
	{
		journal_report_set(out_report, CSV_JOURNAL_OPEN_FAILED, ',', NULL, "invalid base path");
		errno = EINVAL;
		return (0);
	}
	csv_journal_options_default(&opt);
	if (opt_in)
		opt = *opt_in;
	sep = (opt.delimiter == ';') ? ';' : ',';

	if (!make_path_with_suffix(jpath, sizeof(jpath), base_csv, JOURNAL_SUFFIX))
	{
		journal_report_set(out_report, CSV_JOURNAL_OOM, sep, NULL, "journal path too long");
		errno = ENAMETOOLONG;
		return (0);
	}
	if (!make_path_with_suffix(lpath, sizeof(lpath), jpath, LOCK_SUFFIX))
	{
		journal_report_set(out_report, CSV_JOURNAL_OOM, sep, jpath, "lock path too long");
		errno = ENAMETOOLONG;
		return (0);
	}
	if (fs_mkdir_p_for_file(jpath) != 0)
	{
		journal_report_set(out_report, CSV_JOURNAL_OPEN_FAILED, sep, jpath, "cannot create parent directory");
		return (0);
	}
	if (opt.use_lock_file)
	{
		if (!lock_acquire(lpath, &lock_fd))
		{
			journal_report_set(out_report, CSV_JOURNAL_LOCKED, sep, jpath, "journal is locked");
			return (0);
		}
	}

	f = fopen(jpath, "ab+");
	if (!f)
	{
		if (opt.use_lock_file)
			lock_release(lpath, lock_fd);
		journal_report_set(out_report, CSV_JOURNAL_OPEN_FAILED, sep, jpath, "cannot open journal");
		return (0);
	}

	/* If file exists, ensure last row ends with a newline. */
	if (!file_is_empty(f))
	{
		if (!ensure_trailing_newline(f))
		{
			fclose(f);
			if (opt.use_lock_file)
				lock_release(lpath, lock_fd);
			journal_report_set(out_report, CSV_JOURNAL_IO_ERROR, sep, jpath, "cannot fix trailing newline");
			return (0);
		}
	}

	/* If empty, write header (no checksum footer in journal). */
	if (file_is_empty(f))
		csv_write_row_sep(f, g_header_fields, 4, sep);

	/* Optional: load/rebuild incremental state for sparse journal indexing. */
	if (opt.update_journal_index)
	{
		CsvIndexReport	strep;
		unsigned long long	curr_sz;

		memset(&strep, 0, sizeof(strep));
		if (!csv_index_state_load_ex(jpath, &st, &strep))
		{
			/* Missing/corrupt state: rebuild from CSV once (best-effort). */
			if (!csv_index_state_rebuild_ex(jpath, &st, &strep))
				memset(&st, 0, sizeof(st));
			(void)csv_index_state_store_ex(jpath, &st, NULL);
		}
		/* Detect drift (journal changed but state didn't) and rebuild. */
		if (TM_FSEEK64(f, 0, SEEK_END) != 0)
			curr_sz = st.bytes;
		else
		{
			long long p = (long long)TM_FTELL64(f);
			curr_sz = (p < 0) ? st.bytes : (unsigned long long)p;
		}
		if (st.bytes != 0ULL && curr_sz != st.bytes)
		{
			if (csv_index_state_rebuild_ex(jpath, &st, NULL))
				(void)csv_index_state_store_ex(jpath, &st, NULL);
		}
		row_index = st.data_rows;
	}
	else
		row_index = 0ULL;

	/* Capture start offset for this data row (for optional index checkpoint). */
	if (TM_FSEEK64(f, 0, SEEK_END) != 0)
		row_start_off = 0ULL;
	else
	{
		long long p = (long long)TM_FTELL64(f);
		row_start_off = (p < 0) ? 0ULL : (unsigned long long)p;
	}

	snprintf(ts, sizeof(ts), "%lld", timestamp);
	fmt_double(val, sizeof(val), value);
	snprintf(rst, sizeof(rst), "%d", hit_count_reset ? 1 : 0);
	fields[0] = ts;
	fields[1] = (event_type) ? event_type : "";
	fields[2] = val;
	fields[3] = rst;

	csv_write_row_sep(f, fields, 4, sep);
	if (ferror(f))
	{
		fclose(f);
		if (opt.use_lock_file)
			lock_release(lpath, lock_fd);
		journal_report_set(out_report, CSV_JOURNAL_IO_ERROR, sep, jpath, "write failed");
		return (0);
	}
	if (opt.fsync_on_append && !tm_fsync_stream(f))
	{
		fclose(f);
		if (opt.use_lock_file)
			lock_release(lpath, lock_fd);
		journal_report_set(out_report, CSV_JOURNAL_IO_ERROR, sep, jpath, "fsync failed");
		return (0);
	}
	/* Update incremental state and optional sparse index (best-effort). */
	if (opt.update_journal_index)
	{
		unsigned long long curr_sz;
		long long p;

		idx_ok = 1;
		memset(&idxrep, 0, sizeof(idxrep));
		/* Current file size after append (best-effort). */
		if (TM_FSEEK64(f, 0, SEEK_END) != 0)
			curr_sz = st.bytes;
		else
		{
			p = (long long)TM_FTELL64(f);
			curr_sz = (p < 0) ? st.bytes : (unsigned long long)p;
		}
		file_sz_after = curr_sz;

		/* Maybe append a checkpoint to <journal>.idx. */
		if (!csv_index_maybe_append_checkpoint_ex(jpath,
				opt.journal_index_stride_rows,
				row_index, timestamp, row_start_off, &idxrep))
			idx_ok = 0;
		/* Update state to stay consistent with file content. */
		st.data_rows = row_index + 1ULL;
		st.bytes = file_sz_after;
		st.last_ts = timestamp;
		if (!csv_index_state_store_ex(jpath, &st, NULL))
			idx_ok = 0;

		rep.index_updated = idx_ok ? 1 : 0;
		rep.index_report = idxrep;

		if (!idx_ok && !opt.journal_index_best_effort)
		{
			fclose(f);
			if (opt.use_lock_file)
				lock_release(lpath, lock_fd);
			journal_report_set(out_report, CSV_JOURNAL_IO_ERROR, sep, jpath,
							"journal index/state update failed");
			if (out_report)
			{
				out_report->index_updated = 0;
				out_report->index_report = idxrep;
			}
			return (0);
		}
	}

	if (fclose(f) != 0)
	{
		if (opt.use_lock_file)
			lock_release(lpath, lock_fd);
		journal_report_set(out_report, CSV_JOURNAL_IO_ERROR, sep, jpath, "close failed");
		return (0);
	}
	if (opt.use_lock_file)
		lock_release(lpath, lock_fd);

	rep.status = CSV_JOURNAL_OK;
	rep.delimiter = sep;
	snprintf(rep.journal_path, sizeof(rep.journal_path), "%s", jpath);
	if (out_report)
		*out_report = rep;
	return (1);
}

int	csv_journal_append_data_row_ex(const char *base_csv,
			const t_data_row *row,
			const CsvJournalOptions *opt, CsvJournalReport *out_report)
{
	if (!row)
	{
		journal_report_set(out_report, CSV_JOURNAL_IO_ERROR, ',', NULL, "invalid row");
		errno = EINVAL;
		return (0);
	}
	return (csv_journal_append_row_ex(base_csv, row->timestamp, row->event_type,
			row->value, row->hit_count_reset, opt, out_report));
}

static DataStruct	*data_struct_new_empty(void)
{
	DataStruct	*d;

	d = (DataStruct *)calloc(1, sizeof(*d));
	return (d);
}

static int	data_struct_reserve(DataStruct *d, size_t need_cap)
{
	t_data_row	*nr;
	size_t		new_cap;

	if (!d)
		return (0);
	if (need_cap <= d->capacity)
		return (1);
	new_cap = (d->capacity == 0) ? 64 : d->capacity;
	while (new_cap < need_cap)
	{
		if (new_cap > (SIZE_MAX / 2))
			return (0);
		new_cap *= 2;
	}
	nr = (t_data_row *)realloc(d->rows, new_cap * sizeof(*nr));
	if (!nr)
		return (0);
	d->rows = nr;
	d->capacity = new_cap;
	return (1);
}

/* Move rows from src into dst (transfers event_type ownership). */
static int	data_struct_move_append(DataStruct *dst, DataStruct *src)
{
	size_t	i;

	if (!dst || !src || src->count == 0)
		return (1);
	if (!data_struct_reserve(dst, dst->count + src->count))
		return (0);
	for (i = 0; i < src->count; i++)
	{
		dst->rows[dst->count + i] = src->rows[i];
		src->rows[i].event_type = NULL;
	}
	dst->count += src->count;
	return (1);
}

static void	csv_load_report_ok(CsvLoadReport *r)
{
	if (!r)
		return ;
	memset(r, 0, sizeof(*r));
	r->status = CSV_LOAD_OK;
	r->delimiter = ',';
}

DataStruct	*load_from_csv_journal_ex(const char *base_csv,
				const CsvLoadOptions *opt, CsvJournalLoadReport *out_report)
{
	char				jpath[512];
	DataStruct			*base;
	DataStruct			*journal;
	CsvLoadReport		base_rep;
	CsvLoadReport		j_rep;

	csv_load_report_ok(&base_rep);
	csv_load_report_ok(&j_rep);

	if (!base_csv)
		return (NULL);
	if (!make_path_with_suffix(jpath, sizeof(jpath), base_csv, JOURNAL_SUFFIX))
		return (NULL);

	/* Load snapshot if present, else start empty. */
	if (fs_file_exists(base_csv))
		base = load_from_csv_ex(base_csv, opt, &base_rep);
	else
		base = data_struct_new_empty();
	if (!base)
	{
		if (out_report)
		{
			out_report->base = base_rep;
			out_report->journal = j_rep;
			out_report->journal_present = 0;
		}
		return (NULL);
	}

	/* Apply journal if present. */
	if (!fs_file_exists(jpath))
	{
		if (out_report)
		{
			out_report->base = base_rep;
			out_report->journal = j_rep;
			out_report->journal_present = 0;
		}
		return (base);
	}
	journal = load_from_csv_ex(jpath, opt, &j_rep);
	if (!journal)
	{
		data_struct_free(base);
		if (out_report)
		{
			out_report->base = base_rep;
			out_report->journal = j_rep;
			out_report->journal_present = 1;
		}
		return (NULL);
	}
	if (!data_struct_move_append(base, journal))
	{
		data_struct_free(journal);
		data_struct_free(base);
		return (NULL);
	}
	data_struct_free(journal);

	if (out_report)
	{
		out_report->base = base_rep;
		out_report->journal = j_rep;
		out_report->journal_present = 1;
	}
	return (base);
}

DataStruct	*load_from_csv_journal_since_ex(const char *base_csv,
				long long min_timestamp,
				const CsvLoadOptions *opt,
				CsvJournalLoadReport *out_report)
{
	char				jpath[512];
	DataStruct			*base;
	DataStruct			*journal;
	CsvLoadReport		base_rep;
	CsvLoadReport		j_rep;

	csv_load_report_ok(&base_rep);
	csv_load_report_ok(&j_rep);

	if (!base_csv)
		return (NULL);
	if (!make_path_with_suffix(jpath, sizeof(jpath), base_csv, JOURNAL_SUFFIX))
		return (NULL);

	/* Load snapshot if present, else start empty. */
	if (fs_file_exists(base_csv))
		base = load_from_csv_since_ex(base_csv, min_timestamp, opt, &base_rep);
	else
		base = data_struct_new_empty();
	if (!base)
	{
		if (out_report)
		{
			out_report->base = base_rep;
			out_report->journal = j_rep;
			out_report->journal_present = 0;
		}
		return (NULL);
	}

	/* Apply journal if present. */
	if (!fs_file_exists(jpath))
	{
		if (out_report)
		{
			out_report->base = base_rep;
			out_report->journal = j_rep;
			out_report->journal_present = 0;
		}
		return (base);
	}
	journal = load_from_csv_since_indexed_ex(jpath, min_timestamp, opt, &j_rep);
	if (!journal)
	{
		data_struct_free(base);
		if (out_report)
		{
			out_report->base = base_rep;
			out_report->journal = j_rep;
			out_report->journal_present = 1;
		}
		return (NULL);
	}
	if (!data_struct_move_append(base, journal))
	{
		data_struct_free(journal);
		data_struct_free(base);
		return (NULL);
	}
	data_struct_free(journal);

	if (out_report)
	{
		out_report->base = base_rep;
		out_report->journal = j_rep;
		out_report->journal_present = 1;
	}
	return (base);
}

DataStruct	*load_from_csv_journal_since_indexed_ex(const char *base_csv,
				long long min_timestamp,
				const CsvLoadOptions *opt,
				CsvJournalLoadReport *out_report)
{
	char				jpath[512];
	DataStruct			*base;
	DataStruct			*journal;
	CsvLoadReport		base_rep;
	CsvLoadReport		j_rep;

	csv_load_report_ok(&base_rep);
	csv_load_report_ok(&j_rep);

	if (!base_csv)
		return (NULL);
	if (!make_path_with_suffix(jpath, sizeof(jpath), base_csv, JOURNAL_SUFFIX))
		return (NULL);

	/* Load snapshot (indexed) if present, else start empty. */
	if (fs_file_exists(base_csv))
		base = load_from_csv_since_indexed_ex(base_csv, min_timestamp, opt, &base_rep);
	else
		base = data_struct_new_empty();
	if (!base)
	{
		if (out_report)
		{
			out_report->base = base_rep;
			out_report->journal = j_rep;
			out_report->journal_present = 0;
		}
		return (NULL);
	}

	/* Apply journal if present (journal is typically small; linear scan). */
	if (!fs_file_exists(jpath))
	{
		if (out_report)
		{
			out_report->base = base_rep;
			out_report->journal = j_rep;
			out_report->journal_present = 0;
		}
		return (base);
	}
	journal = load_from_csv_since_ex(jpath, min_timestamp, opt, &j_rep);
	if (!journal)
	{
		data_struct_free(base);
		if (out_report)
		{
			out_report->base = base_rep;
			out_report->journal = j_rep;
			out_report->journal_present = 1;
		}
		return (NULL);
	}
	if (!data_struct_move_append(base, journal))
	{
		data_struct_free(journal);
		data_struct_free(base);
		return (NULL);
	}
	data_struct_free(journal);

	if (out_report)
	{
		out_report->base = base_rep;
		out_report->journal = j_rep;
		out_report->journal_present = 1;
	}
	return (base);
}

DataStruct	*load_from_csv_journal(const char *base_csv)
{
	CsvLoadOptions	opt;

	csv_load_options_default(&opt);
	return (load_from_csv_journal_ex(base_csv, &opt, NULL));
}

/* -------------------------------------------------------------------------- */
/* Journal rotation / compaction                                              */
/* -------------------------------------------------------------------------- */

static void	rotate_report_set(CsvJournalRotateReport *r,
				CsvJournalRotateStatus st, const char *jpath, const char *msg)
{
	if (!r)
		return ;
	memset(r, 0, sizeof(*r));
	r->status = st;
	if (jpath)
		snprintf(r->journal_path, sizeof(r->journal_path), "%s", jpath);
	if (msg)
		snprintf(r->error, sizeof(r->error), "%s", msg);
}

void	csv_journal_rotate_options_default(CsvJournalRotateOptions *opt)
{
	if (!opt)
		return ;
	opt->max_journal_bytes = DEFAULT_ROTATE_BYTES;
	opt->max_journal_rows = 0;
	opt->archive_old_journal = 1;
	opt->rebuild_index = 1;
	opt->index_stride_rows = 1024;
}

static size_t	approx_count_rows_newlines(const char *path, size_t limit)
{
	FILE		*f;
	char		buf[65536];
	size_t		n;
	size_t		i;
	size_t		lines;

	lines = 0;
	f = fopen(path, "rb");
	if (!f)
		return (0);
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
	{
		i = 0;
		while (i < n)
		{
			if (buf[i] == '\n')
			{
				lines++;
				if (limit != 0 && lines > limit + 1)
				{
					fclose(f);
					return (limit + 1);
				}
			}
			i++;
		}
	}
	fclose(f);
	if (lines == 0)
		return (0);
	/* First line is the header in our journal. */
	if (lines > 0)
		lines--;
	return (lines);
}

static int	build_archive_path(char *out, size_t outsz, const char *jpath)
{
	time_t	t;
	long	pid;
	int		n;

	if (!out || outsz == 0 || !jpath)
		return (0);
	t = time(NULL);
	pid = (long)TM_GETPID();
	n = snprintf(out, outsz, "%s.%ld.%ld.bak", jpath, (long)t, pid);
	return (n > 0 && (size_t)n < outsz);
}

static void	remove_sidecar_best_effort(const char *path)
{
	if (path)
		(void)remove(path);
}

static void	rename_sidecar_best_effort(const char *src, const char *dst)
{
	if (!src || !dst)
		return ;
	if (!fs_file_exists(src))
		return ;
	(void)rename(src, dst);
}

static int	remove_or_archive_journal(const char *jpath,
				int archive_old, char *out_archive, size_t outsz)
{
	char	apath[512];
	char	idx_src[560];
	char	st_src[560];
	char	idx_dst[560];
	char	st_dst[560];

	if (!jpath)
		return (0);
	(void)make_path_with_suffix(idx_src, sizeof(idx_src), jpath, INDEX_SUFFIX);
	(void)make_path_with_suffix(st_src, sizeof(st_src), jpath, INDEX_STATE_SUFFIX);
	if (!archive_old)
	{
		(void)remove(jpath);
		remove_sidecar_best_effort(idx_src);
		remove_sidecar_best_effort(st_src);
		if (out_archive && outsz)
			out_archive[0] = '\0';
		return (1);
	}
	if (!build_archive_path(apath, sizeof(apath), jpath))
		return (0);
	/* Make sure parent dir exists (same dir as journal in most cases). */
	(void)fs_mkdir_p_for_file(apath);
	if (rename(jpath, apath) != 0)
		return (0);
	/* Best-effort: keep sidecars next to the archived journal. */
	if (make_path_with_suffix(idx_dst, sizeof(idx_dst), apath, INDEX_SUFFIX))
		rename_sidecar_best_effort(idx_src, idx_dst);
	if (make_path_with_suffix(st_dst, sizeof(st_dst), apath, INDEX_STATE_SUFFIX))
		rename_sidecar_best_effort(st_src, st_dst);
	if (out_archive && outsz)
		snprintf(out_archive, outsz, "%s", apath);
	return (1);
}

static int	journal_rotate_internal(const char *base_csv,
				CsvJournalRotateReason reason,
				const CsvJournalRotateOptions *rot_opt_in,
				const CsvLoadOptions *load_opt,
				const CsvWriteOptions *snapshot_write_opt,
				const CsvJournalOptions *journal_opt,
				CsvJournalRotateReport *out_report)
{
	CsvJournalRotateOptions	rot_opt;
	CsvJournalOptions		jopt;
	CsvWriteOptions			wopt;
	CsvWriteReport			wrep;
	CsvJournalLoadReport	lrep;
	CsvIndexOptions		iopt;
	CsvIndexReport		idxrep;
	int				idx_ok;
	DataStruct				*merged;
	char					jpath[512];
	char					lpath[560];
	int					lock_fd;

	lock_fd = -1;
	memset(&wrep, 0, sizeof(wrep));
	memset(&lrep, 0, sizeof(lrep));
	memset(&idxrep, 0, sizeof(idxrep));
	idx_ok = 0;
	merged = NULL;

	rot_opt = (rot_opt_in) ? *rot_opt_in
		: (CsvJournalRotateOptions){DEFAULT_ROTATE_BYTES, 0, 1, 1, 1024};
	csv_journal_options_default(&jopt);
	if (journal_opt)
		jopt = *journal_opt;

	if (!base_csv)
	{
		rotate_report_set(out_report, CSV_JOURNAL_ROTATE_OPEN_FAILED, NULL, "invalid base path");
		errno = EINVAL;
		return (0);
	}
	if (!make_path_with_suffix(jpath, sizeof(jpath), base_csv, JOURNAL_SUFFIX))
	{
		rotate_report_set(out_report, CSV_JOURNAL_ROTATE_OOM, NULL, "journal path too long");
		errno = ENAMETOOLONG;
		return (0);
	}
	if (!fs_file_exists(jpath))
	{
		if (out_report)
		{
			rotate_report_set(out_report, CSV_JOURNAL_ROTATE_NOOP, jpath, NULL);
			out_report->rotated = 0;
			out_report->reason = CSV_JOURNAL_ROTATE_NONE;
		}
		return (1);
	}
	if (!make_path_with_suffix(lpath, sizeof(lpath), jpath, LOCK_SUFFIX))
	{
		rotate_report_set(out_report, CSV_JOURNAL_ROTATE_OOM, jpath, "lock path too long");
		errno = ENAMETOOLONG;
		return (0);
	}
	if (jopt.use_lock_file)
	{
		if (!lock_acquire(lpath, &lock_fd))
		{
			if (errno == EEXIST)
				rotate_report_set(out_report, CSV_JOURNAL_ROTATE_LOCKED, jpath, "journal is locked");
			else
				rotate_report_set(out_report, CSV_JOURNAL_ROTATE_OPEN_FAILED, jpath, "cannot create lock");
			return (0);
		}
	}

	/* Merge snapshot + journal under lock (so we don't miss appends). */
	merged = load_from_csv_journal_ex(base_csv, load_opt, &lrep);
	if (!merged)
	{
		if (jopt.use_lock_file)
			lock_release(lpath, lock_fd);
		rotate_report_set(out_report, CSV_JOURNAL_ROTATE_IO_ERROR, jpath, "load failed");
		return (0);
	}

	/* Snapshot write options: default + reuse delimiter when possible. */
	if (snapshot_write_opt)
		wopt = *snapshot_write_opt;
	else
		csv_write_options_default(&wopt);
	if (!snapshot_write_opt)
	{
		if (lrep.base.delimiter == ';' || lrep.journal.delimiter == ';')
			wopt.delimiter = ';';
	}
	if (!save_to_csv_ex(base_csv, merged, &wopt, &wrep))
	{
		data_struct_free(merged);
		if (jopt.use_lock_file)
			lock_release(lpath, lock_fd);
		if (out_report)
		{
			rotate_report_set(out_report, CSV_JOURNAL_ROTATE_IO_ERROR, jpath, "snapshot write failed");
			out_report->snapshot_write = wrep;
			out_report->rotated = 0;
			out_report->reason = reason;
		}
		return (0);
	}

	/* Archive or clear the journal now that snapshot contains all rows. */
	if (!remove_or_archive_journal(jpath, rot_opt.archive_old_journal,
			(out_report) ? out_report->archive_path : NULL,
			(out_report) ? sizeof(out_report->archive_path) : 0))
	{
		data_struct_free(merged);
		if (jopt.use_lock_file)
			lock_release(lpath, lock_fd);
		rotate_report_set(out_report, CSV_JOURNAL_ROTATE_IO_ERROR, jpath, "cannot archive/clear journal");
		if (out_report)
			out_report->snapshot_write = wrep;
		return (0);
	}

	/* Rebuild sparse index for fast seeks (best-effort; does not fail rotation). */
	if (rot_opt.rebuild_index)
	{
		csv_index_options_default(&iopt);
		if (rot_opt.index_stride_rows != 0)
			iopt.stride_rows = rot_opt.index_stride_rows;
		idx_ok = csv_index_build_ex(base_csv, &iopt, &idxrep);
		if (out_report)
		{
			out_report->index_rebuilt = idx_ok ? 1 : 0;
			out_report->index_report = idxrep;
		}
	}

	data_struct_free(merged);
	if (jopt.use_lock_file)
		lock_release(lpath, lock_fd);

	if (out_report)
	{
		rotate_report_set(out_report, CSV_JOURNAL_ROTATE_OK, jpath, NULL);
		out_report->rotated = 1;
		out_report->reason = reason;
		out_report->snapshot_write = wrep;
		if (!rot_opt.rebuild_index)
			out_report->index_rebuilt = 0;
	}
	return (1);
}

int	csv_journal_compact_ex(const char *base_csv,
			const CsvLoadOptions *load_opt,
			const CsvWriteOptions *snapshot_write_opt,
			const CsvJournalOptions *journal_opt,
			CsvJournalRotateReport *out_report)
{
	return (journal_rotate_internal(base_csv, CSV_JOURNAL_ROTATE_FORCE,
			NULL, load_opt, snapshot_write_opt, journal_opt, out_report));
}

int	csv_journal_rotate_if_needed_ex(const char *base_csv,
			const CsvJournalRotateOptions *rot_opt_in,
			const CsvLoadOptions *load_opt,
			const CsvWriteOptions *snapshot_write_opt,
			const CsvJournalOptions *journal_opt,
			CsvJournalRotateReport *out_report)
{
	CsvJournalRotateOptions	rot_opt;
	char					jpath[512];
	long					sz;
	size_t				rows;
	CsvJournalRotateReason	reason;

	rot_opt = (rot_opt_in) ? *rot_opt_in
		: (CsvJournalRotateOptions){DEFAULT_ROTATE_BYTES, 0, 1, 1, 1024};
	if (!base_csv)
	{
		rotate_report_set(out_report, CSV_JOURNAL_ROTATE_OPEN_FAILED, NULL, "invalid base path");
		errno = EINVAL;
		return (0);
	}
	if (!make_path_with_suffix(jpath, sizeof(jpath), base_csv, JOURNAL_SUFFIX))
	{
		rotate_report_set(out_report, CSV_JOURNAL_ROTATE_OOM, NULL, "journal path too long");
		errno = ENAMETOOLONG;
		return (0);
	}
	if (!fs_file_exists(jpath))
	{
		if (out_report)
		{
			rotate_report_set(out_report, CSV_JOURNAL_ROTATE_NOOP, jpath, NULL);
			out_report->rotated = 0;
			out_report->reason = CSV_JOURNAL_ROTATE_NONE;
		}
		return (1);
	}
	sz = fs_file_size(jpath);
	if (sz < 0)
	{
		rotate_report_set(out_report, CSV_JOURNAL_ROTATE_OPEN_FAILED, jpath, "cannot stat journal");
		return (0);
	}
	rows = 0;
	reason = CSV_JOURNAL_ROTATE_NONE;
	if (rot_opt.max_journal_bytes != 0 && (size_t)sz >= rot_opt.max_journal_bytes)
		reason = CSV_JOURNAL_ROTATE_BY_BYTES;
	else if (rot_opt.max_journal_rows != 0)
	{
		rows = approx_count_rows_newlines(jpath, rot_opt.max_journal_rows);
		if (rows >= rot_opt.max_journal_rows)
			reason = CSV_JOURNAL_ROTATE_BY_ROWS;
	}
	if (out_report)
	{
		rotate_report_set(out_report, CSV_JOURNAL_ROTATE_NOOP, jpath, NULL);
		out_report->journal_size = sz;
		out_report->approx_journal_rows = rows;
		out_report->rotated = 0;
		out_report->reason = reason;
	}
	if (reason == CSV_JOURNAL_ROTATE_NONE)
		return (1);
	return (journal_rotate_internal(base_csv, reason, &rot_opt,
			load_opt, snapshot_write_opt, journal_opt, out_report));
}
