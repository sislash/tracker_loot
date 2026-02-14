#include "csv_index.h"
#include "fs_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>

#if defined(_WIN32) || defined(_WIN64)
# include <io.h>
# include <process.h>
# define TM_GETPID _getpid
# define TM_FSEEK64 _fseeki64
# define TM_FTELL64 _ftelli64
#else
# include <unistd.h>
# include <sys/types.h>

/* Some libcs hide fseeko/ftello unless feature macros are set.
 * Declare them explicitly to keep the build C99 + -Werror friendly.
 */
extern int	fseeko(FILE *stream, off_t offset, int whence);
extern off_t	ftello(FILE *stream);

# define TM_GETPID getpid
# define TM_FSEEK64 fseeko
# define TM_FTELL64 ftello
#endif

#define INDEX_SUFFIX ".idx"
#define INDEX_MAGIC  "#csv_index_v1"

#define INDEX_STATE_SUFFIX ".idxstate"
#define INDEX_STATE_MAGIC  "#csv_index_state_v1"

/* Forward declarations (needed because we add incremental helpers above). */
static int	make_index_path(char *out, size_t outsz, const char *csv_path);
static char	*make_tmp_path(const char *final_path);
static int	replace_file_atomic(const char *tmp_path, const char *final_path);

/* Forward declarations for strict parsers/extractors used by incremental helpers. */
static int	parse_u64_strict(const char *s, unsigned long long *out);
static int	parse_ll_strict_local(const char *s, long long *out);
static int	extract_ts_fast(const char *line, long long *out_ts);

/* Small, safe dynamic line reader (local copy). */
enum { RL_OK = 0, RL_OOM = 1, RL_TOO_LONG = 2 };

static int	read_line_dyn(FILE *f, char **line, size_t *cap, int *io_err,
				size_t *out_len, size_t max_line_bytes)
{
	size_t	len;
	int		c;

	if (io_err)
		*io_err = RL_OK;
	if (out_len)
		*out_len = 0;
	if (!f || !line || !cap)
		return (0);
	if (max_line_bytes == 0)
		max_line_bytes = (1u * 1024u * 1024u);
	if (!*line || *cap == 0)
	{
		*cap = 1024;
		if (*cap > max_line_bytes)
			*cap = max_line_bytes;
		*line = (char *)malloc(*cap);
		if (!*line)
		{
			if (io_err)
				*io_err = RL_OOM;
			return (0);
		}
	}
	len = 0;
	while ((c = fgetc(f)) != EOF)
	{
		if (len + 1 >= *cap)
		{
			size_t	ncap;
			char	*nl;

			if (*cap >= max_line_bytes)
			{
				if (io_err)
					*io_err = RL_TOO_LONG;
				return (0);
			}
			if (*cap > (SIZE_MAX / 2))
			{
				if (io_err)
					*io_err = RL_OOM;
				return (0);
			}
			ncap = (*cap) * 2;
			if (ncap > max_line_bytes)
				ncap = max_line_bytes;
			nl = (char *)realloc(*line, ncap);
			if (!nl)
			{
				if (io_err)
					*io_err = RL_OOM;
				return (0);
			}
			*line = nl;
			*cap = ncap;
		}
		(*line)[len++] = (char)c;
		if (c == '\n')
			break ;
	}
	if (len == 0 && c == EOF)
		return (0);
	(*line)[len] = '\0';
	if (out_len)
		*out_len = len;
	return (1);
}

static void	rep_set(CsvIndexReport *r, CsvIndexStatus st,
				const char *ipath, const char *msg)
{
	if (!r)
		return ;
	memset(r, 0, sizeof(*r));
	r->status = st;
	if (ipath)
		snprintf(r->index_path, sizeof(r->index_path), "%s", ipath);
	if (msg)
		snprintf(r->error, sizeof(r->error), "%s", msg);
}

static int	make_state_path(char *out, size_t outsz, const char *csv_path)
{
	size_t	bl;
	size_t	sl;

	if (!out || outsz == 0 || !csv_path)
		return (0);
	bl = strlen(csv_path);
	sl = strlen(INDEX_STATE_SUFFIX);
	if (bl + sl + 1 > outsz)
		return (0);
	memcpy(out, csv_path, bl);
	memcpy(out + bl, INDEX_STATE_SUFFIX, sl + 1);
	return (1);
}

static int	parse_kv_u64(const char *line, const char *key, unsigned long long *out)
{
	const char	*p;
	char		buf[64];
	size_t		i;

	if (!line || !key || !out)
		return (0);
	p = strstr(line, key);
	if (!p)
		return (0);
	p += strlen(key);
	i = 0;
	while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
	{
		if (i + 1 >= sizeof(buf))
			return (0);
		buf[i++] = *p++;
	}
	buf[i] = '\0';
	return (parse_u64_strict(buf, out));
}

static int	parse_kv_ll(const char *line, const char *key, long long *out)
{
	const char	*p;
	char		buf[64];
	size_t		i;

	if (!line || !key || !out)
		return (0);
	p = strstr(line, key);
	if (!p)
		return (0);
	p += strlen(key);
	i = 0;
	while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
	{
		if (i + 1 >= sizeof(buf))
			return (0);
		buf[i++] = *p++;
	}
	buf[i] = '\0';
	return (parse_ll_strict_local(buf, out));
}

int	csv_index_state_load_ex(const char *csv_path,
						CsvIndexState *out_state,
						CsvIndexReport *out_report)
{
	char	spath[512];
	FILE	*f;
	char	line[256];
	unsigned long long	rows;
	unsigned long long	bytes;
	long long	last_ts;

	if (out_state)
		memset(out_state, 0, sizeof(*out_state));
	if (!csv_path)
	{
		rep_set(out_report, CSV_INDEX_OPEN_FAILED, NULL, "invalid csv path");
		errno = EINVAL;
		return (0);
	}
	if (!make_state_path(spath, sizeof(spath), csv_path))
	{
		rep_set(out_report, CSV_INDEX_OOM, NULL, "state path too long");
		return (0);
	}
	f = fopen(spath, "rb");
	if (!f)
	{
		rep_set(out_report, CSV_INDEX_OPEN_FAILED, spath, "state not found");
		return (0);
	}
	if (!fgets(line, (int)sizeof(line), f))
	{
		fclose(f);
		rep_set(out_report, CSV_INDEX_BAD_FORMAT, spath, "empty state");
		return (0);
	}
	fclose(f);
	if (strncmp(line, INDEX_STATE_MAGIC, strlen(INDEX_STATE_MAGIC)) != 0)
	{
		rep_set(out_report, CSV_INDEX_BAD_FORMAT, spath, "bad state magic");
		return (0);
	}
	rows = 0;
	bytes = 0;
	last_ts = 0;
	if (!parse_kv_u64(line, "rows=", &rows)
		|| !parse_kv_u64(line, "bytes=", &bytes)
		|| !parse_kv_ll(line, "last_ts=", &last_ts))
	{
		rep_set(out_report, CSV_INDEX_BAD_FORMAT, spath, "bad state format");
		return (0);
	}
	if (out_state)
	{
		out_state->data_rows = rows;
		out_state->bytes = bytes;
		out_state->last_ts = last_ts;
	}
	rep_set(out_report, CSV_INDEX_OK, spath, NULL);
	return (1);
}

int	csv_index_state_store_ex(const char *csv_path,
						const CsvIndexState *state,
						CsvIndexReport *out_report)
{
	char	spath[512];
	char	*tmp_path;
	FILE	*out;

	tmp_path = NULL;
	out = NULL;
	if (!csv_path || !state)
	{
		rep_set(out_report, CSV_INDEX_OPEN_FAILED, NULL, "invalid args");
		errno = EINVAL;
		return (0);
	}
	if (!make_state_path(spath, sizeof(spath), csv_path))
	{
		rep_set(out_report, CSV_INDEX_OOM, NULL, "state path too long");
		return (0);
	}
	if (fs_mkdir_p_for_file(spath) != 0)
	{
		rep_set(out_report, CSV_INDEX_OPEN_FAILED, spath, "cannot create parent directory");
		return (0);
	}
	tmp_path = make_tmp_path(spath);
	if (!tmp_path)
	{
		rep_set(out_report, CSV_INDEX_OOM, spath, "out of memory (tmp path)");
		return (0);
	}
	out = fopen(tmp_path, "wb");
	if (!out)
	{
		rep_set(out_report, CSV_INDEX_OPEN_FAILED, spath, "cannot open state tmp");
		free(tmp_path);
		return (0);
	}
	fprintf(out, "%s rows=%llu bytes=%llu last_ts=%lld\n",
		INDEX_STATE_MAGIC,
		(unsigned long long)state->data_rows,
		(unsigned long long)state->bytes,
		(long long)state->last_ts);
	if (ferror(out) || fclose(out) != 0)
	{
		(void)remove(tmp_path);
		free(tmp_path);
		rep_set(out_report, CSV_INDEX_IO_ERROR, spath, "cannot write state");
		return (0);
	}
	out = NULL;
	if (!replace_file_atomic(tmp_path, spath))
	{
		(void)remove(tmp_path);
		free(tmp_path);
		rep_set(out_report, CSV_INDEX_IO_ERROR, spath, "cannot replace state file");
		return (0);
	}
	free(tmp_path);
	rep_set(out_report, CSV_INDEX_OK, spath, NULL);
	return (1);
}

int	csv_index_state_rebuild_ex(const char *csv_path,
						CsvIndexState *out_state,
						CsvIndexReport *out_report)
{
	FILE	*in;
	char	*line;
	size_t	cap;
	size_t	len;
	int	io_err;
	size_t	data_row;
	int	saw_first_data;
	long long	last_ts;
	long	sz;

	if (out_state)
		memset(out_state, 0, sizeof(*out_state));
	if (!csv_path)
	{
		rep_set(out_report, CSV_INDEX_OPEN_FAILED, NULL, "invalid csv path");
		errno = EINVAL;
		return (0);
	}
	in = fopen(csv_path, "rb");
	if (!in)
	{
		rep_set(out_report, CSV_INDEX_OPEN_FAILED, NULL, "cannot open csv");
		return (0);
	}
	line = NULL;
	cap = 0;
	len = 0;
	io_err = 0;
	data_row = 0;
	saw_first_data = 0;
	last_ts = 0;
	while (read_line_dyn(in, &line, &cap, &io_err, &len, 0))
	{
		while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
			line[--len] = '\0';
		if (line[0] == '\0')
			continue;
		if (line[0] == '#')
			continue;
		if (!saw_first_data)
		{
			const char *p = line;
			while (*p && isspace((unsigned char)*p))
				p++;
			if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF)
				p += 3;
			if (strncmp(p, "timestamp", 9) == 0)
				continue;
		}
		{
			long long ts;
			if (!extract_ts_fast(line, &ts))
				continue;
			saw_first_data = 1;
			last_ts = ts;
			data_row++;
		}
	}
	free(line);
	if (io_err != RL_OK || ferror(in))
	{
		fclose(in);
		rep_set(out_report, CSV_INDEX_IO_ERROR, NULL, "I/O error while rebuilding state");
		return (0);
	}
	fclose(in);
	sz = fs_file_size(csv_path);
	if (sz < 0)
		sz = 0;
	if (out_state)
	{
		out_state->data_rows = (unsigned long long)data_row;
		out_state->bytes = (unsigned long long)sz;
		out_state->last_ts = last_ts;
	}
	rep_set(out_report, CSV_INDEX_OK, NULL, NULL);
	return (1);
}

static int	read_index_stride(FILE *f, size_t *out_stride)
{
	char	line[128];
	char	*pos;
	unsigned long long	v;

	if (!f || !out_stride)
		return (0);
	*out_stride = 0;
	if (!fgets(line, (int)sizeof(line), f))
		return (0);
	if (strncmp(line, INDEX_MAGIC, strlen(INDEX_MAGIC)) != 0)
		return (0);
	pos = strstr(line, "stride=");
	if (!pos)
		return (0);
	pos += strlen("stride=");
	if (!parse_u64_strict(pos, &v))
		return (0);
	if (v == 0)
		return (0);
	*out_stride = (size_t)v;
	return (1);
}

int	csv_index_maybe_append_checkpoint_ex(const char *csv_path,
						size_t stride_rows,
						unsigned long long row_index,
						long long timestamp,
						unsigned long long byte_offset,
						CsvIndexReport *out_report)
{
	char	ipath[512];
	FILE	*f;
	size_t	found_stride;
	CsvIndexOptions	iopt;
	CsvIndexReport	rep;

	memset(&rep, 0, sizeof(rep));
	if (!csv_path)
	{
		rep_set(out_report, CSV_INDEX_OPEN_FAILED, NULL, "invalid csv path");
		errno = EINVAL;
		return (0);
	}
	if (stride_rows == 0)
		stride_rows = 1024;
	if ((row_index % (unsigned long long)stride_rows) != 0ULL)
	{
		rep_set(out_report, CSV_INDEX_OK, NULL, NULL);
		return (1);
	}
	if (!make_index_path(ipath, sizeof(ipath), csv_path))
	{
		rep_set(out_report, CSV_INDEX_OOM, NULL, "index path too long");
		return (0);
	}
	if (fs_mkdir_p_for_file(ipath) != 0)
	{
		rep_set(out_report, CSV_INDEX_OPEN_FAILED, ipath, "cannot create parent directory");
		return (0);
	}
	/* If index is missing or mismatched, rebuild (best-effort). */
	f = fopen(ipath, "rb");
	if (f)
	{
		found_stride = 0;
		if (!read_index_stride(f, &found_stride) || found_stride != stride_rows)
		{
			fclose(f);
			csv_index_options_default(&iopt);
			iopt.stride_rows = stride_rows;
			if (!csv_index_build_ex(csv_path, &iopt, &rep))
			{
				rep_set(out_report, rep.status, ipath, rep.error);
				return (0);
			}
			rep_set(out_report, CSV_INDEX_OK, ipath, NULL);
			return (1);
		}
		fclose(f);
	}
	else
	{
		/* Create new index file with header. */
		f = fopen(ipath, "wb");
		if (!f)
		{
			rep_set(out_report, CSV_INDEX_OPEN_FAILED, ipath, "cannot create index");
			return (0);
		}
		fprintf(f, "%s stride=%zu\n", INDEX_MAGIC, stride_rows);
		fprintf(f, "row_index,timestamp,byte_offset\n");
		if (ferror(f) || fclose(f) != 0)
		{
			rep_set(out_report, CSV_INDEX_IO_ERROR, ipath, "cannot write index header");
			return (0);
		}
	}
	/* Append checkpoint. */
	f = fopen(ipath, "ab");
	if (!f)
	{
		rep_set(out_report, CSV_INDEX_OPEN_FAILED, ipath, "cannot open index for append");
		return (0);
	}
	fprintf(f, "%llu,%lld,%llu\n", row_index, timestamp, byte_offset);
	if (ferror(f) || fclose(f) != 0)
	{
		rep_set(out_report, CSV_INDEX_IO_ERROR, ipath, "cannot append checkpoint");
		return (0);
	}
	rep_set(out_report, CSV_INDEX_OK, ipath, NULL);
	return (1);
}

void	csv_index_options_default(CsvIndexOptions *opt)
{
	if (!opt)
		return ;
	opt->stride_rows = 1024;
}

static int	make_index_path(char *out, size_t outsz, const char *csv_path)
{
	size_t	bl;
	size_t	sl;

	if (!out || outsz == 0 || !csv_path)
		return (0);
	bl = strlen(csv_path);
	sl = strlen(INDEX_SUFFIX);
	if (bl + sl + 1 > outsz)
		return (0);
	memcpy(out, csv_path, bl);
	memcpy(out + bl, INDEX_SUFFIX, sl + 1);
	return (1);
}

static char	*make_tmp_path(const char *final_path)
{
	char	buf[32];
	char	*tmp;
	size_t	need;
	int		n;

	if (!final_path)
		return (NULL);
	n = snprintf(buf, sizeof(buf), ".tmp.%ld", (long)TM_GETPID());
	if (n <= 0)
		return (NULL);
	need = strlen(final_path) + (size_t)n + 1;
	tmp = (char *)malloc(need);
	if (!tmp)
		return (NULL);
	snprintf(tmp, need, "%s%s", final_path, buf);
	return (tmp);
}

static int	replace_file_atomic(const char *tmp_path, const char *final_path)
{
	if (!tmp_path || !final_path)
		return (0);
#if defined(_WIN32) || defined(_WIN64)
	(void)remove(final_path);
#endif
	if (rename(tmp_path, final_path) != 0)
		return (0);
	return (1);
}

static int	parse_u64_strict(const char *s, unsigned long long *out)
{
	unsigned long long	v;
	const unsigned char	*p;

	if (!s || !out)
		return (0);
	p = (const unsigned char *)s;
	while (*p && isspace(*p))
		p++;
	if (!*p)
		return (0);
	v = 0;
	while (*p && isdigit(*p))
	{
		unsigned int d = (unsigned int)(*p - '0');
		if (v > (ULLONG_MAX / 10ULL))
			return (0);
		v *= 10ULL;
		if (v > (ULLONG_MAX - d))
			return (0);
		v += d;
		p++;
	}
	while (*p && isspace(*p))
		p++;
	if (*p != '\0')
		return (0);
	*out = v;
	return (1);
}

static int	parse_ll_strict_local(const char *s, long long *out)
{
	long long	v;
	int			sign;
	const unsigned char *p;

	if (!s || !out)
		return (0);
	p = (const unsigned char *)s;
	while (*p && isspace(*p))
		p++;
	sign = 1;
	if (*p == '+' || *p == '-')
	{
		if (*p == '-')
			sign = -1;
		p++;
	}
	if (!*p || !isdigit(*p))
		return (0);
	v = 0;
	while (*p && isdigit(*p))
	{
		int d = (int)(*p - '0');
		if (v > (LLONG_MAX / 10LL))
			return (0);
		v *= 10LL;
		if (v > (LLONG_MAX - d))
			return (0);
		v += d;
		p++;
	}
	while (*p && isspace(*p))
		p++;
	if (*p != '\0')
		return (0);
	*out = (sign > 0) ? v : -v;
	return (1);
}

/* Extract timestamp from a data line: read until ',' or ';' (first column). */
static int	extract_ts_fast(const char *line, long long *out_ts)
{
	char	buf[64];
	size_t	i;

	if (!line || !out_ts)
		return (0);
	while (*line && isspace((unsigned char)*line))
		line++;
	i = 0;
	while (*line && *line != ',' && *line != ';' && *line != '\n' && *line != '\r')
	{
		if (i + 1 >= sizeof(buf))
			return (0);
		buf[i++] = *line++;
	}
	buf[i] = '\0';
	return (parse_ll_strict_local(buf, out_ts));
}

int	csv_index_build_ex(const char *csv_path,
					const CsvIndexOptions *opt_in,
					CsvIndexReport *out_report)
{
	CsvIndexOptions	opt;
	CsvIndexReport	rep;
	char			ipath[512];
	char			*tmp_path;
	FILE			*in;
	FILE			*out;
	char			*line;
	size_t			cap;
	size_t			len;
	int				io_err;
	unsigned long long	pos;
	size_t			data_row;
	size_t			entries;
	int				saw_first_data;

	memset(&rep, 0, sizeof(rep));
	line = NULL;
	cap = 0;
	len = 0;
	io_err = 0;
	pos = 0;
	data_row = 0;
	entries = 0;
	saw_first_data = 0;
	tmp_path = NULL;
	in = NULL;
	out = NULL;

	if (!csv_path)
	{
		rep_set(out_report, CSV_INDEX_OPEN_FAILED, NULL, "invalid csv path");
		errno = EINVAL;
		return (0);
	}
	opt = (opt_in) ? *opt_in : (CsvIndexOptions){1024};
	if (opt.stride_rows == 0)
		opt.stride_rows = 1024;
	if (!make_index_path(ipath, sizeof(ipath), csv_path))
	{
		rep_set(out_report, CSV_INDEX_OOM, NULL, "index path too long");
		errno = ENAMETOOLONG;
		return (0);
	}
	if (fs_mkdir_p_for_file(ipath) != 0)
	{
		rep_set(out_report, CSV_INDEX_OPEN_FAILED, ipath, "cannot create parent directory");
		return (0);
	}
	in = fopen(csv_path, "rb");
	if (!in)
	{
		rep_set(out_report, CSV_INDEX_OPEN_FAILED, ipath, "cannot open csv");
		return (0);
	}
	tmp_path = make_tmp_path(ipath);
	if (!tmp_path)
	{
		rep_set(out_report, CSV_INDEX_OOM, ipath, "out of memory (tmp path)");
		fclose(in);
		return (0);
	}
	out = fopen(tmp_path, "wb");
	if (!out)
	{
		rep_set(out_report, CSV_INDEX_OPEN_FAILED, ipath, "cannot open index tmp");
		free(tmp_path);
		fclose(in);
		return (0);
	}

	fprintf(out, "%s stride=%zu\n", INDEX_MAGIC, opt.stride_rows);
	fprintf(out, "row_index,timestamp,byte_offset\n");

	/* Skip header/comments/blank lines; then index data rows. */
	while (1)
	{
		pos = (unsigned long long)TM_FTELL64(in);
		if (!read_line_dyn(in, &line, &cap, &io_err, &len, 0))
			break ;
		/* Normalize EOL for checks. */
		while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
			line[--len] = '\0';
		if (line[0] == '\0')
			continue ;
		if (line[0] == '#')
			continue ;
		/* Header line contains "timestamp" in first column. */
		if (!saw_first_data)
		{
			const char *p = line;
			while (*p && isspace((unsigned char)*p))
				p++;
			/* Strip UTF-8 BOM if present. */
			if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF)
				p += 3;
			if (strncmp(p, "timestamp", 9) == 0)
				continue ;
		}
		/* Data line. */
		{
			long long ts;

			if (!extract_ts_fast(line, &ts))
				continue ;
			saw_first_data = 1;
			if ((data_row % opt.stride_rows) == 0)
			{
				fprintf(out, "%zu,%lld,%llu\n", data_row, ts, pos);
				entries++;
			}
			data_row++;
		}
	}

	if (io_err != RL_OK || ferror(in) || ferror(out))
	{
		rep_set(&rep, CSV_INDEX_IO_ERROR, ipath, "I/O error while building index");
		fclose(in);
		fclose(out);
		if (tmp_path)
			(void)remove(tmp_path);
		free(tmp_path);
		free(line);
		if (out_report)
			*out_report = rep;
		return (0);
	}

	if (fclose(out) != 0)
	{
		rep_set(&rep, CSV_INDEX_IO_ERROR, ipath, "cannot finalize index");
		fclose(in);
		if (tmp_path)
			(void)remove(tmp_path);
		free(tmp_path);
		free(line);
		if (out_report)
			*out_report = rep;
		return (0);
	}
	out = NULL;
	fclose(in);
	in = NULL;

	if (!replace_file_atomic(tmp_path, ipath))
	{
		rep_set(&rep, CSV_INDEX_IO_ERROR, ipath, "cannot replace index file");
		(void)remove(tmp_path);
		free(tmp_path);
		free(line);
		if (out_report)
			*out_report = rep;
		return (0);
	}
	free(tmp_path);
	free(line);

	rep_set(&rep, CSV_INDEX_OK, ipath, NULL);
	rep.stride_rows = opt.stride_rows;
	rep.entries = entries;
	if (out_report)
		*out_report = rep;
	return (1);
}

int	csv_index_lookup_offset_ex(const char *csv_path,
					long long timestamp,
					unsigned long long *out_offset,
					long long *out_checkpoint_ts,
					unsigned long long *out_checkpoint_row,
					CsvIndexReport *out_report)
{
	char	ipath[512];
	FILE	*f;
	char	*line;
	size_t	cap;
	size_t	len;
	int		io_err;
	unsigned long long	best_off;
	unsigned long long	best_row;
	long long	best_ts;
	int		got;

	if (out_offset)
		*out_offset = 0;
	if (out_checkpoint_ts)
		*out_checkpoint_ts = 0;
	if (out_checkpoint_row)
		*out_checkpoint_row = 0;
	if (!csv_path)
	{
		rep_set(out_report, CSV_INDEX_OPEN_FAILED, NULL, "invalid csv path");
		errno = EINVAL;
		return (0);
	}
	if (!make_index_path(ipath, sizeof(ipath), csv_path))
	{
		rep_set(out_report, CSV_INDEX_OOM, NULL, "index path too long");
		return (0);
	}
	f = fopen(ipath, "rb");
	if (!f)
	{
		rep_set(out_report, CSV_INDEX_OPEN_FAILED, ipath, "index not found");
		return (0);
	}
	line = NULL;
	cap = 0;
	len = 0;
	io_err = 0;
	best_off = 0;
	best_row = 0;
	best_ts = LLONG_MIN;
	got = 0;

	/* Line1 magic, line2 header. */
	if (!read_line_dyn(f, &line, &cap, &io_err, &len, 0))
		goto done;
	if (strncmp(line, INDEX_MAGIC, strlen(INDEX_MAGIC)) != 0)
		goto done;
	if (!read_line_dyn(f, &line, &cap, &io_err, &len, 0))
		goto done;

	while (read_line_dyn(f, &line, &cap, &io_err, &len, 0))
	{
		char	*cols[3];
		char	*tmp;
		unsigned long long row;
		unsigned long long off;
		long long ts;

		/* Normalize EOL. */
		while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
			line[--len] = '\0';
		if (line[0] == '\0' || line[0] == '#')
			continue ;
		/* Simple split 3 columns by comma. */
		tmp = line;
		cols[0] = tmp;
		cols[1] = NULL;
		cols[2] = NULL;
		{
			char *c1 = strchr(tmp, ',');
			if (!c1) continue;
			*c1 = '\0';
			cols[1] = c1 + 1;
			char *c2 = strchr(cols[1], ',');
			if (!c2) continue;
			*c2 = '\0';
			cols[2] = c2 + 1;
		}
		if (!parse_u64_strict(cols[0], &row))
			continue ;
		if (!parse_ll_strict_local(cols[1], &ts))
			continue ;
		if (!parse_u64_strict(cols[2], &off))
			continue ;
		if (ts <= timestamp && ts >= best_ts)
		{
			best_ts = ts;
			best_off = off;
			best_row = row;
			got = 1;
		}
		/* NOTE: Do not assume monotonic timestamps.
		 * The index stays small (sparse), so scanning it fully is cheap and
		 * preserves correctness even if timestamps are out-of-order.
		 */
	}

done:
	if (io_err != RL_OK || ferror(f))
		rep_set(out_report, CSV_INDEX_IO_ERROR, ipath, "I/O error while reading index");
	else if (!got)
		rep_set(out_report, CSV_INDEX_BAD_FORMAT, ipath, "no checkpoint found");
	else
		rep_set(out_report, CSV_INDEX_OK, ipath, NULL);
	if (got)
	{
		if (out_offset)
			*out_offset = best_off;
		if (out_checkpoint_ts)
			*out_checkpoint_ts = best_ts;
		if (out_checkpoint_row)
			*out_checkpoint_row = best_row;
	}
	free(line);
	fclose(f);
	return (got);
}

int	csv_index_remove(const char *csv_path)
{
	char	ipath[512];

	if (!csv_path)
		return (0);
	if (!make_index_path(ipath, sizeof(ipath), csv_path))
		return (0);
	if (remove(ipath) != 0)
		return (0);
	return (1);
}
