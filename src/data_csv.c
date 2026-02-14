#include "data_csv.h"
#include "csv.h"
#include "csv_index.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <ctype.h>
#include <math.h>
#include <stdint.h>

#if defined(_WIN32) || defined(_WIN64)
# include <io.h>
# include <process.h>
# define TM_FILENO _fileno
# define TM_FSYNC  _commit
# define TM_GETPID _getpid
# define TM_FSEEK64 _fseeki64
#else
# include <unistd.h>
# include <sys/types.h>

/* Some libcs hide fseeko unless feature macros are set.
 * Declare it explicitly to keep the build C99 + -Werror friendly.
 */
extern int	fseeko(FILE *stream, off_t offset, int whence);
# define TM_FILENO fileno
# define TM_FSYNC  fsync
# define TM_GETPID getpid
# define TM_FSEEK64 fseeko
#endif

#if !defined(_WIN32) && !defined(_WIN64)
int	fileno(FILE *stream);
#endif

/* CSV standardization (bi-directional): UTF-8, normalized header fields. */
#define CSV_HEADER "timestamp,event_type,value,hit_count_reset"

static const char	*g_csv_header_fields[4] = {
	"timestamp",
	"event_type",
	"value",
	"hit_count_reset"
};

/* Optional footer (not standard CSV, but safe as a comment line). */
#define CSV_CRC32_FOOTER_PREFIX "#crc32="

#ifndef CSV_MAX_LINE_BYTES
/* Safety net against pathological files (prevents runaway allocations). */
# define CSV_MAX_LINE_BYTES (1024u * 1024u) /* 1 MiB */

#ifndef CSV_MAX_FILE_BYTES
/* Safety net against giant files (prevents runaway allocations / long stalls). */
# define CSV_MAX_FILE_BYTES (256u * 1024u * 1024u) /* 256 MiB */
#endif

#ifndef CSV_MAX_ROWS
/* Safety net against runaway row counts (memory guard). */
# define CSV_MAX_ROWS (2000000u) /* 2,000,000 rows */
#endif

#ifndef CSV_MAX_ERROR_SAMPLES
/* When logging is enabled, print at most this many per-line warnings. */
# define CSV_MAX_ERROR_SAMPLES 8u
#endif

#endif

#if defined(_MSC_VER)
# define TM_ISFINITE(x) _finite((x))
#else
# define TM_ISFINITE(x) isfinite((x))
#endif


/* Last load report (allows detailed error retrieval without spamming stderr). */
static CsvLoadReport	g_last_report;

const CsvLoadReport	*csv_last_report(void)
{
	return (&g_last_report);
}

void	csv_load_options_default(CsvLoadOptions *opt)
{
	if (!opt)
		return ;
	opt->policy = CSV_ERROR_SKIP;
	opt->log_stderr = 0;
	opt->max_file_bytes = (size_t)CSV_MAX_FILE_BYTES;
	opt->max_rows = (size_t)CSV_MAX_ROWS;
	opt->max_error_samples = (size_t)CSV_MAX_ERROR_SAMPLES;
	opt->max_line_bytes = (size_t)CSV_MAX_LINE_BYTES;
	opt->verify_crc32 = 0;
}

static void	rep_clear(CsvLoadReport *r)
{
	if (!r)
		return ;
	memset(r, 0, sizeof(*r));
	r->status = CSV_LOAD_OK;
	r->delimiter = ',';
}

static void	rep_first_error(CsvLoadReport *r, size_t line_no,
			const char *reason, const char *preview)
{
	if (!r || r->first_error_line != 0)
		return ;
	r->first_error_line = line_no;
	r->last_error_line = line_no;
	if (!reason)
		reason = "error";
	if (!preview)
		preview = "";
	snprintf(r->first_error, sizeof(r->first_error), "%s: \"%s\"", reason, preview);
}

static void	rep_last_error(CsvLoadReport *r, size_t line_no)
{
	if (!r)
		return ;
	if (line_no != 0)
		r->last_error_line = line_no;
}



/* Flushes the stream and asks the OS to commit bytes to disk (best effort). */
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

/* Windows rename() won't replace an existing file; POSIX rename() does. */
static int	tm_replace_file(const char *tmp_path, const char *dst_path)
{
#if defined(_WIN32) || defined(_WIN64)
	/* Best effort: ignore if the destination doesn't exist. */
	if (remove(dst_path) != 0 && errno != ENOENT)
		return (0);
#endif
	return (rename(tmp_path, dst_path) == 0);
}

static char	*tm_make_tmp_path(const char *dst_path)
{
	static unsigned long	seq = 0;
	const unsigned long		n = ++seq;
	const long				pid = (long)TM_GETPID();
	const size_t			len = strlen(dst_path);
	char					*tmp;

	tmp = (char *)malloc(len + 64);
	if (!tmp)
		return (NULL);
	/* Same directory => rename is as close to atomic as the OS allows. */
	snprintf(tmp, len + 64, "%s.%ld.%lu.tmp", dst_path, pid, n);
	return (tmp);
}

static void	csv_warn(const char *filename, size_t line_no,
			const char *fmt, ...)
{
	va_list	ap;

	if (!fmt)
		return ;
	fprintf(stderr, "[CSV] %s:%zu: ", (filename) ? filename : "(null)", line_no);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

static void	csv_preview(char *dst, size_t cap, const char *src)
{
	size_t	i;

	if (!dst || cap == 0)
		return ;
	dst[0] = '\0';
	if (!src)
		return ;
	i = 0;
	while (src[i] && src[i] != '\n' && src[i] != '\r' && i + 1 < cap)
	{
		dst[i] = src[i];
		i++;
	}
	dst[i] = '\0';
}

static void	csv_strip_utf8_bom(char *s)
{
	unsigned char	*u;

	if (!s)
		return ;
	u = (unsigned char *)s;
	if (u[0] == 0xEF && u[1] == 0xBB && u[2] == 0xBF)
		memmove(s, s + 3, strlen(s + 3) + 1);
}

static char	*csv_trim_ws(char *s)
{
	char	*end;

	if (!s)
		return (NULL);
	while (*s && isspace((unsigned char)*s))
		s++;
	end = s + strlen(s);
	while (end > s && isspace((unsigned char)end[-1]))
		end--;
	*end = '\0';
	return (s);
}

static char	*xstrdup(const char *s);

/*
 * Header recognition (strict but BOM/whitespace tolerant):
 * Accepts: timestamp,event_type,value,hit_count_reset (optionally quoted).
 */
static int	csv_header_matches_sep(char *line, char sep)
{
	char	*cols[4];
	char	*c0;
	char	*c1;
	char	*c2;
	char	*c3;

	if (!line)
		return (0);
	csv_strip_utf8_bom(line);
	if (!csv_split_n_strict_sep(line, cols, 4, sep))
		return (0);
	c0 = csv_trim_ws(cols[0]);
	c1 = csv_trim_ws(cols[1]);
	c2 = csv_trim_ws(cols[2]);
	c3 = csv_trim_ws(cols[3]);
	if (!c0 || !c1 || !c2 || !c3)
		return (0);
	return (strcmp(c0, "timestamp") == 0
		&& strcmp(c1, "event_type") == 0
		&& strcmp(c2, "value") == 0
		&& strcmp(c3, "hit_count_reset") == 0);
}

static int	csv_header_detect(const char *line, char *out_sep)
{
	char	*tmp;

	if (!line || !out_sep)
		return (0);
	*out_sep = ',';
	tmp = xstrdup(line);
	if (tmp)
	{
		if (csv_header_matches_sep(tmp, ','))
		{
			free(tmp);
			*out_sep = ',';
			return (1);
		}
		free(tmp);
	}
	tmp = xstrdup(line);
	if (tmp)
	{
		if (csv_header_matches_sep(tmp, ';'))
		{
			free(tmp);
			*out_sep = ';';
			return (1);
		}
		free(tmp);
	}
	return (0);
}

static char	*xstrdup(const char *s)
{
    size_t	len;
    char	*o;
    
    if (!s)
        return NULL;
    len = strlen(s);
    o = (char *)malloc(len + 1);
    if (!o)
        return NULL;
    memcpy(o, s, len + 1);
    return o;
}

/* Locale-proof: if snprintf outputs comma decimals, normalize to dot. */
static void	fmt_double(char *dst, size_t cap, double v)
{
    size_t	i;
    
    if (!dst || cap == 0)
        return;
    snprintf(dst, cap, "%.17g", v);
    i = 0;
    while (dst[i])
    {
        if (dst[i] == ',')
            dst[i] = '.';
        i++;
    }
}

static int	parse_ll_strict(const char *s, long long *out)
{
    char		*end;
    long long	v;
	
	if (!s || !out)
		return (0);
	errno = 0;
	v = strtoll(s, &end, 10);
	if (end == s || errno != 0)
		return (0);
	while (*end && isspace((unsigned char)*end))
		end++;
	if (*end != '\0')
		return (0);
	*out = v;
	return (1);
}

/* Accept only 0 or 1 (bi-directional with save_to_csv). */
static int	parse_int01_strict(const char *s, int *out)
{
	char	*end;
	long	v;
    
	if (!s || !out)
		return (0);
	errno = 0;
	v = strtol(s, &end, 10);
	if (end == s || errno != 0)
		return (0);
	while (*end && isspace((unsigned char)*end))
		end++;
	if (*end != '\0')
		return (0);
	if (!(v == 0 || v == 1))
		return (0);
	*out = (int)v;
	return (1);
}

static int	parse_double_strict(const char *s, double *out)
{
	size_t	len;
	char	*buf;
	char	stack[128];
	char	*end;
	double	v;
	size_t	i;
    
	if (!s || !out)
		return (0);
	*out = 0.0;
	len = strlen(s);
	/* Use stack for short values, heap for long ones (still bounded). */
	if (len + 1 <= sizeof(stack))
		buf = stack;
	else
	{
		if (len + 1 > CSV_MAX_LINE_BYTES)
			return (0);
		buf = (char *)malloc(len + 1);
		if (!buf)
			return (0);
	}
	i = 0;
	while (i < len)
	{
		buf[i] = (s[i] == ',') ? '.' : s[i];
		i++;
	}
	buf[len] = '\0';
	errno = 0;
	v = strtod(buf, &end);
	if (end == buf || errno != 0)
	{
		if (buf != stack)
			free(buf);
		return (0);
	}
	while (*end && isspace((unsigned char)*end))
		end++;
	if (*end != '\0')
	{
		if (buf != stack)
			free(buf);
		return (0);
	}
	if (!TM_ISFINITE(v))
	{
		if (buf != stack)
			free(buf);
		return (0);
	}
	if (buf != stack)
		free(buf);
	*out = v;
	return (1);
}

static int	csv_detect_sep_from_data_line(const char *line, char *out_sep)
{
	char		*tmp;
	char		*cols[4];
	long long	ts;
	double		v;
	int			rst;

	if (!line || !out_sep)
		return (0);
	/* Prefer ',' when both work. */
	tmp = xstrdup(line);
	if (tmp)
	{
		if (csv_split_n_strict_sep(tmp, cols, 4, ',')
			&& parse_ll_strict(cols[0], &ts)
			&& parse_double_strict(cols[2], &v)
			&& parse_int01_strict(cols[3], &rst))
		{
			free(tmp);
			*out_sep = ',';
			return (1);
		}
		free(tmp);
	}
	tmp = xstrdup(line);
	if (tmp)
	{
		if (csv_split_n_strict_sep(tmp, cols, 4, ';')
			&& parse_ll_strict(cols[0], &ts)
			&& parse_double_strict(cols[2], &v)
			&& parse_int01_strict(cols[3], &rst))
		{
			free(tmp);
			*out_sep = ';';
			return (1);
		}
		free(tmp);
	}
	return (0);
}

static int	ensure_cap(DataStruct *d, size_t need)
{
	t_data_row	*nr;
	size_t		ncap;

	if (!d)
		return (0);
	if (need <= d->capacity)
		return (1);
	ncap = (d->capacity == 0) ? 64 : d->capacity;
	while (ncap < need)
	{
		if (ncap > (SIZE_MAX / 2))
			return (0);
		ncap *= 2;
	}
	if (ncap > (SIZE_MAX / sizeof(*nr)))
		return (0);
	nr = (t_data_row *)realloc(d->rows, ncap * sizeof(*nr));
	if (!nr)
		return (0);
	d->rows = nr;
	d->capacity = ncap;
	return (1);
}

/*
 * Safe dynamic line reader (portable, avoids fixed buffers).
 * Returns 1 when a line is read, 0 on EOF, and sets *io_err on error.
 */
/*
 * Safe dynamic line reader (portable, avoids fixed buffers).
 * Returns 1 when a line is read, 0 on EOF. On error returns 0 and sets *io_err.
 */
enum { RL_ERR_NONE = 0, RL_ERR_OOM = 1, RL_ERR_TOO_LONG = 2 };

static int	read_line_dyn(FILE *f, char **line, size_t *cap, int *io_err,
				size_t *out_len, size_t max_line_bytes)
{
	size_t	len;
	int		c;

	if (io_err)
		*io_err = RL_ERR_NONE;
	if (out_len)
		*out_len = 0;
	if (!f || !line || !cap)
		return (0);
	if (max_line_bytes == 0)
		max_line_bytes = CSV_MAX_LINE_BYTES;
	if (!*line || *cap == 0)
	{
		*cap = 1024;
		if (*cap > max_line_bytes)
			*cap = max_line_bytes;
		*line = (char *)malloc(*cap);
		if (!*line)
		{
			if (io_err)
				*io_err = RL_ERR_OOM;
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
					*io_err = RL_ERR_TOO_LONG;
				return (0);
			}
			if (*cap > (SIZE_MAX / 2))
			{
				if (io_err)
					*io_err = RL_ERR_OOM;
				return (0);
			}
			ncap = (*cap) * 2;
			if (ncap > max_line_bytes)
				ncap = max_line_bytes;
			nl = (char *)realloc(*line, ncap);
			if (!nl)
			{
				if (io_err)
					*io_err = RL_ERR_OOM;
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

void	csv_write_options_default(CsvWriteOptions *opt)
{
	if (!opt)
		return ;
	opt->delimiter = ',';
	opt->atomic_write = 1;
	opt->fsync_on_close = 1;
	opt->write_crc32_footer = 0;
}

/* ------------------------------- CRC32 ---------------------------------- */

static uint32_t	csv_crc32_update(uint32_t crc, const void *data, size_t len)
{
	static int		init = 0;
	static uint32_t	tbl[256];
	const unsigned char	*p;
	size_t				 i;

	if (!init)
	{
		for (i = 0; i < 256; i++)
		{
			uint32_t c = (uint32_t)i;
			int k;
			for (k = 0; k < 8; k++)
				c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
			tbl[i] = c;
		}
		init = 1;
	}
	p = (const unsigned char *)data;
	for (i = 0; i < len; i++)
		crc = tbl[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
	return (crc);
}

static int	csv_parse_hex_u32(const char *s, uint32_t *out)
{
	uint32_t	v;
	int			n;

	if (!s || !out)
		return (0);
	while (*s && isspace((unsigned char)*s))
		s++;
	if (!*s)
		return (0);
	v = 0;
	n = 0;
	while (*s)
	{
		unsigned char c = (unsigned char)*s;
		uint32_t d;
		if (c >= '0' && c <= '9')
			d = (uint32_t)(c - '0');
		else if (c >= 'a' && c <= 'f')
			d = (uint32_t)(10 + (c - 'a'));
		else if (c >= 'A' && c <= 'F')
			d = (uint32_t)(10 + (c - 'A'));
		else
			break ;
		v = (v << 4) | d;
		n++;
		if (n > 8)
			return (0);
		s++;
	}
	if (n == 0)
		return (0);
	while (*s && isspace((unsigned char)*s))
		s++;
	if (*s != '\0')
		return (0);
	*out = v;
	return (1);
}

/* --------------------------- Streaming writer ---------------------------- */

struct s_csv_writer
{
	FILE			*f;
	char			*tmp_path;
	char			*dst_path;
	CsvWriteOptions	opt;
	uint32_t		crc;
	char			*buf;
	size_t			buf_cap;
};

static void	csv_write_report_set(CsvWriteReport *r, CsvWriteStatus st,
								char delim, const char *msg)
{
	if (!r)
		return ;
	memset(r, 0, sizeof(*r));
	r->status = st;
	r->delimiter = delim;
	if (msg)
		snprintf(r->error, sizeof(r->error), "%s", msg);
}

static int	csv_field_needs_quotes_sep(const char *s, char sep)
{
	size_t	len;

	if (!s || !*s)
		return (0);
	if (strchr(s, sep) || strchr(s, '"') || strchr(s, '\n') || strchr(s, '\r'))
		return (1);
	len = strlen(s);
	if (len > 0 && (s[0] == ' ' || s[0] == '\t' || s[len - 1] == ' ' || s[len - 1] == '\t'))
		return (1);
	return (0);
}

static size_t	csv_row_needed_bytes_sep(const char **fields, int n, char sep)
{
	size_t	need;
	int		i;

	need = 0;
	for (i = 0; i < n; i++)
	{
		const char *s = fields[i] ? fields[i] : "";
		if (!csv_field_needs_quotes_sep(s, sep))
			need += strlen(s);
		else
		{
			need += 2;
			for (; *s; s++)
				need += (*s == '"') ? 2 : 1;
		}
		if (i + 1 < n)
			need += 1;
	}
	need += 1;
	return (need);
}

static int	csv_writer_ensure_buf(CsvWriter *w, size_t need)
{
	char	*nb;

	if (!w)
		return (0);
	if (need + 1 <= w->buf_cap)
		return (1);
	if (need + 1 > (size_t)CSV_MAX_LINE_BYTES)
		return (0);
	nb = (char *)realloc(w->buf, need + 1);
	if (!nb)
		return (0);
	w->buf = nb;
	w->buf_cap = need + 1;
	return (1);
}

static size_t	csv_build_row_into_buf(CsvWriter *w, const char **fields, int n, char sep)
{
	char	*p;
	int		i;

	if (!w || !w->buf)
		return (0);
	p = w->buf;
	for (i = 0; i < n; i++)
	{
		const char *s = fields[i] ? fields[i] : "";
		if (!csv_field_needs_quotes_sep(s, sep))
		{
			size_t len = strlen(s);
			memcpy(p, s, len);
			p += len;
		}
		else
		{
			*p++ = '"';
			for (; *s; s++)
			{
				if (*s == '"')
				{
					*p++ = '"';
					*p++ = '"';
				}
				else
					*p++ = *s;
			}
			*p++ = '"';
		}
		if (i + 1 < n)
			*p++ = sep;
	}
	*p++ = '\n';
	*p = '\0';
	return ((size_t)(p - w->buf));
}

static int	csv_writer_write_bytes(CsvWriter *w, const void *buf, size_t len)
{
	if (!w || !w->f)
		return (0);
	if (len == 0)
		return (1);
	if (fwrite(buf, 1, len, w->f) != len)
		return (0);
	if (w->opt.write_crc32_footer)
		w->crc = csv_crc32_update(w->crc, buf, len);
	return (1);
}

CsvWriter	*csv_writer_open_ex(const char *filename,
								const CsvWriteOptions *opt_in, CsvWriteReport *out_report)
{
	CsvWriter		*w;
	CsvWriteOptions	opt;
	FILE				*f;
	char				*tmp_path;
	const char		*hdr_fields[4];
	size_t			need;
	size_t			len;

	if (!filename)
	{
		csv_write_report_set(out_report, CSV_WRITE_OPEN_FAILED, ',', "invalid filename");
		errno = EINVAL;
		return (NULL);
	}
	if (opt_in)
		opt = *opt_in;
	else
		csv_write_options_default(&opt);
	if (!(opt.delimiter == ',' || opt.delimiter == ';'))
		opt.delimiter = ',';
	
	tmp_path = NULL;
	f = NULL;
	if (opt.atomic_write)
	{
		tmp_path = tm_make_tmp_path(filename);
		if (!tmp_path)
		{
			csv_write_report_set(out_report, CSV_WRITE_OOM, opt.delimiter, "out of memory (tmp path)");
			errno = ENOMEM;
			return (NULL);
		}
		f = fopen(tmp_path, "wb");
	}
	else
		f = fopen(filename, "wb");
	if (!f)
	{
		free(tmp_path);
		csv_write_report_set(out_report, CSV_WRITE_OPEN_FAILED, opt.delimiter, "cannot open file");
		return (NULL);
	}
	w = (CsvWriter *)calloc(1, sizeof(*w));
	if (!w)
	{
		fclose(f);
		if (tmp_path)
			remove(tmp_path);
		free(tmp_path);
		csv_write_report_set(out_report, CSV_WRITE_OOM, opt.delimiter, "out of memory (writer)");
		errno = ENOMEM;
		return (NULL);
	}
	w->f = f;
	w->tmp_path = tmp_path;
	w->dst_path = opt.atomic_write ? xstrdup(filename) : NULL;
	w->opt = opt;
	w->crc = 0xFFFFFFFFu;

	hdr_fields[0] = g_csv_header_fields[0];
	hdr_fields[1] = g_csv_header_fields[1];
	hdr_fields[2] = g_csv_header_fields[2];
	hdr_fields[3] = g_csv_header_fields[3];
	need = csv_row_needed_bytes_sep(hdr_fields, 4, opt.delimiter);
	if (!csv_writer_ensure_buf(w, need))
	{
		csv_write_report_set(out_report, CSV_WRITE_OOM, opt.delimiter, "out of memory (header buffer)");
		csv_writer_close(w);
		return (NULL);
	}
	len = csv_build_row_into_buf(w, hdr_fields, 4, opt.delimiter);
	if (!csv_writer_write_bytes(w, w->buf, len))
	{
		csv_write_report_set(out_report, CSV_WRITE_IO_ERROR, opt.delimiter, "write failed (header)");
		csv_writer_close(w);
		return (NULL);
	}
	csv_write_report_set(out_report, CSV_WRITE_OK, opt.delimiter, NULL);
	return (w);
}

int	csv_writer_write_row(CsvWriter *w, long long timestamp,
								const char *event_type, double value, int hit_count_reset)
{
	char		ts[32];
	char		val[64];
	char		rst[16];
	const char	*fields[4];
	size_t		need;
	size_t		len;

	if (!w)
		return (0);
	snprintf(ts, sizeof(ts), "%lld", timestamp);
	fmt_double(val, sizeof(val), value);
	snprintf(rst, sizeof(rst), "%d", hit_count_reset ? 1 : 0);
	fields[0] = ts;
	fields[1] = event_type ? event_type : "";
	fields[2] = val;
	fields[3] = rst;
	need = csv_row_needed_bytes_sep(fields, 4, w->opt.delimiter);
	if (!csv_writer_ensure_buf(w, need))
		return (0);
	len = csv_build_row_into_buf(w, fields, 4, w->opt.delimiter);
	if (!csv_writer_write_bytes(w, w->buf, len))
		return (0);
	return (1);
}

int	csv_writer_write_data_row(CsvWriter *w, const t_data_row *row)
{
	if (!w || !row)
		return (0);
	return (csv_writer_write_row(w, row->timestamp, row->event_type,
			row->value, row->hit_count_reset));
}

int	csv_writer_close(CsvWriter *w)
{
	uint32_t	final_crc;
	char		footer[64];
	int		ok;

	if (!w)
		return (0);
	ok = 1;
	if (w->opt.write_crc32_footer)
	{
		final_crc = w->crc ^ 0xFFFFFFFFu;
		snprintf(footer, sizeof(footer), "%s%08X\n",
				CSV_CRC32_FOOTER_PREFIX, (unsigned int)final_crc);
		/* Do NOT feed footer into CRC. */
		if (fwrite(footer, 1, strlen(footer), w->f) != strlen(footer))
			ok = 0;
	}
	if (ok && w->opt.fsync_on_close)
		ok = tm_fsync_stream(w->f);
	if (fclose(w->f) != 0)
		ok = 0;
	w->f = NULL;
	if (ok && w->opt.atomic_write && w->tmp_path && w->dst_path)
	{
		if (!tm_replace_file(w->tmp_path, w->dst_path))
			ok = 0;
	}
	if (!ok && w->tmp_path)
		remove(w->tmp_path);
	free(w->tmp_path);
	free(w->dst_path);
	free(w->buf);
	free(w);
	return (ok);
}

int	save_to_csv_ex(const char *filename, DataStruct *data,
						const CsvWriteOptions *opt, CsvWriteReport *out_report)
{
	CsvWriter		*w;
	CsvWriteReport	rep;
	CsvWriteOptions	wopt;
	size_t			i;
	uint32_t		final_crc;

	memset(&rep, 0, sizeof(rep));
	if (!filename || !data)
	{
		csv_write_report_set(out_report, CSV_WRITE_IO_ERROR, ',', "invalid arguments");
		errno = EINVAL;
		return (0);
	}
	if (opt)
		wopt = *opt;
	else
		csv_write_options_default(&wopt);
	w = csv_writer_open_ex(filename, &wopt, &rep);
	if (!w)
	{
		if (out_report)
			*out_report = rep;
		return (0);
	}
	i = 0;
	while (i < data->count)
	{
		if (!csv_writer_write_data_row(w, &data->rows[i]))
		{
			csv_write_report_set(&rep, CSV_WRITE_IO_ERROR, wopt.delimiter, "write failed (row)");
			csv_writer_close(w);
			if (out_report)
				*out_report = rep;
			return (0);
		}
		i++;
	}
	final_crc = (wopt.write_crc32_footer) ? (w->crc ^ 0xFFFFFFFFu) : 0;
	if (!csv_writer_close(w))
	{
		csv_write_report_set(&rep, CSV_WRITE_RENAME_FAILED, wopt.delimiter,
				"finalize failed (rename/fsync)");
		if (out_report)
			*out_report = rep;
		return (0);
	}
	rep.has_crc32 = (wopt.write_crc32_footer != 0);
	rep.crc32 = final_crc;
	if (out_report)
		*out_report = rep;
	return (1);
}

int	save_to_csv(const char *filename, DataStruct *data)
{
	CsvWriteOptions	opt;

	csv_write_options_default(&opt);
	return (save_to_csv_ex(filename, data, &opt, NULL));
}


static int	should_stop(const CsvLoadOptions *opt)
{
	return (opt && opt->policy == CSV_ERROR_STOP);
}

static void	log_bad(const CsvLoadOptions *opt, size_t *logged,
			const char *filename, size_t line_no,
			const char *reason, const char *preview)
{
	if (!opt || !opt->log_stderr || !logged)
		return ;
	if (opt->max_error_samples != 0 && *logged >= opt->max_error_samples)
		return ;
	csv_warn(filename, line_no, "%s, skipping: \"%s\"", reason, preview);
	(*logged)++;
}


static DataStruct	*load_from_csv_ex_seek_internal(const char *filename,
				const CsvLoadOptions *opt_in, CsvLoadReport *out_report,
				unsigned long long start_offset, int use_offset,
				long long min_timestamp, int use_min_timestamp)
{
	FILE			*f;
	DataStruct		*d;
	char			*line;
	size_t			cap;
	size_t			line_no;
	int				io_err;
	size_t			line_len;
	int				header_checked;
	size_t			logged;
	size_t			bytes_total;
	char			sep;
	int				sep_known;
	uint32_t		crc;
	int				crc_active;
	int				footer_seen;
	CsvLoadOptions	opt;
	CsvLoadReport	rep;

	rep_clear(&rep);
	if (opt_in)
		opt = *opt_in;
	else
		csv_load_options_default(&opt);
	/* Defensive defaults for line/error sample caps. */
	if (opt.max_line_bytes == 0)
		opt.max_line_bytes = (size_t)CSV_MAX_LINE_BYTES;
	if (opt.max_error_samples == 0)
		opt.max_error_samples = (size_t)CSV_MAX_ERROR_SAMPLES;

	if (!filename)
	{
		rep.status = CSV_LOAD_OPEN_FAILED;
		rep_first_error(&rep, 0, "invalid filename", "");
		g_last_report = rep;
		if (out_report)
			*out_report = rep;
		return (NULL);
	}
	f = fopen(filename, "rb");
	if (!f)
	{
		rep.status = CSV_LOAD_OPEN_FAILED;
		rep_first_error(&rep, 0, "cannot open file", filename);
		g_last_report = rep;
		if (out_report)
			*out_report = rep;
		return (NULL);
	}
	d = (DataStruct *)calloc(1, sizeof(*d));
	if (!d)
	{
		rep.status = CSV_LOAD_OOM;
		rep_first_error(&rep, 0, "out of memory", "");
		g_last_report = rep;
		if (out_report)
			*out_report = rep;
		fclose(f);
		return (NULL);
	}
	line = NULL;
	cap = 0;
	line_no = 0;
	io_err = 0;
	line_len = 0;
	header_checked = 0;
	logged = 0;
	bytes_total = 0;
	sep = ',';
	sep_known = 0;
	crc = 0xFFFFFFFFu;
	crc_active = (opt.verify_crc32 != 0);
	footer_seen = 0;
	rep.has_crc32 = 0;
	rep.crc32_ok = 0;

	if (use_offset && start_offset > 0)
	{
		if (TM_FSEEK64(f, (long long)start_offset, SEEK_SET) != 0)
		{
			rep.status = CSV_LOAD_IO_ERROR;
			rep_first_error(&rep, 0, "cannot seek to offset", "");
			goto fail;
		}
		/* We are starting at a checkpoint (data rows), so header is already handled. */
		header_checked = 1;
		if (start_offset > (unsigned long long)SIZE_MAX)
			bytes_total = (size_t)SIZE_MAX;
		else
			bytes_total = (size_t)start_offset;
		/* Partial reads can't validate full-file CRC32. */
		crc_active = 0;
	}

	while (read_line_dyn(f, &line, &cap, &io_err, &line_len, opt.max_line_bytes))
	{
		char	preview[160];
		char	*cols[4];
		long long ts;
		double	v;
		int		rst;
		char	*etype;
		size_t	trim_len;
		uint32_t	footer_crc;

		line_no++;
		bytes_total += line_len;
		rep.bytes_read = bytes_total;
		if (opt.max_file_bytes != 0 && bytes_total > opt.max_file_bytes)
		{
			rep.status = CSV_LOAD_TOO_BIG;
			rep_first_error(&rep, line_no, "file exceeds max_file_bytes", "");
			goto fail;
		}
		/* Normalize EOL for parsing + optional CRC32 verification. */
		trim_len = line_len;
		while (trim_len > 0 && (line[trim_len - 1] == '\n' || line[trim_len - 1] == '\r'))
			trim_len--;
		line[trim_len] = '\0';
		if (line[0] == '\0')
			continue ;
		/* Skip comment lines; parse optional checksum footer. */
		if (line[0] == '#')
		{
			if (!footer_seen && strncmp(line, CSV_CRC32_FOOTER_PREFIX,
						strlen(CSV_CRC32_FOOTER_PREFIX)) == 0)
			{
				if (csv_parse_hex_u32(line + strlen(CSV_CRC32_FOOTER_PREFIX), &footer_crc))
				{
					rep.has_crc32 = 1;
					rep.expected_crc32 = footer_crc;
					footer_seen = 1;
					continue ;
				}
				rep.skipped_lines++;
				rep_first_error(&rep, line_no, "invalid #crc32 footer", "");
				rep_last_error(&rep, line_no);
				if (should_stop(&opt))
				{
					rep.status = CSV_LOAD_BAD_FORMAT;
					goto fail;
				}
				continue ;
			}
			continue ;
		}
		if (footer_seen)
		{
			rep.skipped_lines++;
			rep_first_error(&rep, line_no, "data after #crc32 footer", "");
			rep_last_error(&rep, line_no);
			if (should_stop(&opt))
			{
				rep.status = CSV_LOAD_BAD_FORMAT;
				goto fail;
			}
			continue ;
		}
		if (crc_active)
		{
			crc = csv_crc32_update(crc, line, strlen(line));
			crc = csv_crc32_update(crc, "\n", 1);
		}
		csv_preview(preview, sizeof(preview), line);

		/* Check (and skip) the header on the first non-empty, non-comment line. */
		if (!header_checked)
		{
			char	detected;

			header_checked = 1;
			csv_strip_utf8_bom(line);
			if (csv_header_detect(line, &detected))
			{
				sep = detected;
				sep_known = 1;
				rep.delimiter = sep;
				continue ;
			}
			/* If it looks like a header but does not match, skip (but record). */
			if (strstr(line, "timestamp") != NULL)
			{
				rep.skipped_lines++;
				rep_first_error(&rep, line_no, "unexpected header", preview);
				rep_last_error(&rep, line_no);
				if (opt.log_stderr)
					log_bad(&opt, &logged, filename, line_no,
						"unexpected header", preview);
				continue ;
			}
			/* No header: detect delimiter from first data row. */
			if (!sep_known)
			{
				if (!csv_detect_sep_from_data_line(line, &sep))
					sep = ',';
				sep_known = 1;
				rep.delimiter = sep;
			}
		}
		if (!sep_known)
		{
			if (!csv_detect_sep_from_data_line(line, &sep))
				sep = ',';
			sep_known = 1;
			rep.delimiter = sep;
		}

		/* Parse data row (strict: exactly 4 columns). */
		if (!csv_split_n_strict_sep(line, cols, 4, sep))
		{
			rep.skipped_lines++;
			rep_first_error(&rep, line_no, "bad CSV (expected 4 columns)", preview);
			rep_last_error(&rep, line_no);
			if (should_stop(&opt))
			{
				rep.status = CSV_LOAD_BAD_FORMAT;
				goto fail;
			}
			log_bad(&opt, &logged, filename, line_no,
				"bad CSV (expected 4 columns)", preview);
			continue ;
		}
		if (!parse_ll_strict(cols[0], &ts))
		{
			rep.skipped_lines++;
			rep_first_error(&rep, line_no, "invalid timestamp", preview);
			rep_last_error(&rep, line_no);
			if (should_stop(&opt))
			{
				rep.status = CSV_LOAD_BAD_FORMAT;
				goto fail;
			}
			log_bad(&opt, &logged, filename, line_no,
				"invalid timestamp", preview);
			continue ;
		}
		if (use_min_timestamp && ts < min_timestamp)
			continue ;
		if (!parse_double_strict(cols[2], &v))
		{
			rep.skipped_lines++;
			rep_first_error(&rep, line_no, "invalid value", preview);
			rep_last_error(&rep, line_no);
			if (should_stop(&opt))
			{
				rep.status = CSV_LOAD_BAD_FORMAT;
				goto fail;
			}
			log_bad(&opt, &logged, filename, line_no,
				"invalid value", preview);
			continue ;
		}
		if (!parse_int01_strict(cols[3], &rst))
		{
			rep.skipped_lines++;
			rep_first_error(&rep, line_no, "invalid hit_count_reset (expected 0/1)", preview);
			rep_last_error(&rep, line_no);
			if (should_stop(&opt))
			{
				rep.status = CSV_LOAD_BAD_FORMAT;
				goto fail;
			}
			log_bad(&opt, &logged, filename, line_no,
				"invalid hit_count_reset (expected 0/1)", preview);
			continue ;
		}
		if (opt.max_rows != 0 && d->count >= opt.max_rows)
		{
			rep.status = CSV_LOAD_TOO_MANY_ROWS;
			rep_first_error(&rep, line_no, "row count exceeds max_rows", preview);
			goto fail;
		}
		if (!ensure_cap(d, d->count + 1))
		{
			rep.status = CSV_LOAD_OOM;
			rep_first_error(&rep, line_no, "out of memory (grow rows)", preview);
			goto fail;
		}
		etype = xstrdup(cols[1] ? cols[1] : "");
		if (!etype)
		{
			rep.status = CSV_LOAD_OOM;
			rep_first_error(&rep, line_no, "out of memory (event_type)", preview);
			goto fail;
		}
		d->rows[d->count].timestamp = ts;
		d->rows[d->count].event_type = etype;
		d->rows[d->count].value = v;
		d->rows[d->count].hit_count_reset = rst;
		d->count++;
	}

	/* Distinguish EOF from errors. */
	if (io_err != RL_ERR_NONE)
	{
		if (io_err == RL_ERR_TOO_LONG)
			rep.status = CSV_LOAD_BAD_FORMAT;
		else
			rep.status = CSV_LOAD_OOM;
		rep_first_error(&rep, line_no + 1, "read error (line too long / OOM)", "");
		goto fail;
	}
	if (ferror(f))
	{
		rep.status = CSV_LOAD_IO_ERROR;
		rep_first_error(&rep, line_no + 1, "I/O error while reading", "");
		goto fail;
	}
	if (rep.has_crc32 && crc_active)
	{
		rep.computed_crc32 = crc ^ 0xFFFFFFFFu;
		rep.crc32_ok = (rep.computed_crc32 == rep.expected_crc32);
		if (!rep.crc32_ok)
		{
			rep.status = CSV_LOAD_CHECKSUM_MISMATCH;
			rep_first_error(&rep, line_no, "crc32 mismatch", "");
			goto fail;
		}
	}

	rep.loaded_rows = d->count;
	g_last_report = rep;
	if (out_report)
		*out_report = rep;
	free(line);
	fclose(f);
	return (d);

fail:
	rep.loaded_rows = (d) ? d->count : 0;
	g_last_report = rep;
	if (out_report)
		*out_report = rep;
	data_struct_free(d);
	free(line);
	fclose(f);
	return (NULL);
}

DataStruct	*load_from_csv_ex(const char *filename,
				const CsvLoadOptions *opt_in, CsvLoadReport *out_report)
{
	return (load_from_csv_ex_seek_internal(filename, opt_in, out_report,
			0ULL, 0, 0LL, 0));
}

DataStruct	*load_from_csv_since_ex(const char *filename,
				long long min_timestamp,
				const CsvLoadOptions *opt_in,
				CsvLoadReport *out_report)
{
	return (load_from_csv_ex_seek_internal(filename, opt_in, out_report,
			0ULL, 0, min_timestamp, 1));
}

DataStruct	*load_from_csv_since_indexed_ex(const char *filename,
				long long min_timestamp,
				const CsvLoadOptions *opt_in,
				CsvLoadReport *out_report)
{
	unsigned long long	off;
	long long			cp_ts;
	unsigned long long	cp_row;
	CsvIndexReport		idxrep;
	CsvLoadOptions		opt;
	CsvLoadReport		rep_local;
	CsvLoadReport		*rep_ptr;
	int				use_off;

	off = 0ULL;
	cp_ts = 0LL;
	cp_row = 0ULL;
	memset(&idxrep, 0, sizeof(idxrep));
	if (opt_in)
		opt = *opt_in;
	else
		csv_load_options_default(&opt);
	/* Partial load: CRC32 cannot be validated reliably. */
	opt.verify_crc32 = 0;
	use_off = csv_index_lookup_offset_ex(filename, min_timestamp,
			&off, &cp_ts, &cp_row, &idxrep);
	if (!use_off)
		off = 0ULL;
	rep_ptr = out_report;
	if (!rep_ptr)
	{
		rep_clear(&rep_local);
		rep_ptr = &rep_local;
	}
	(void)idxrep;
	return (load_from_csv_ex_seek_internal(filename, &opt, rep_ptr,
			off, (use_off && off > 0ULL), min_timestamp, 1));
}
DataStruct	*load_from_csv(const char *filename)
{
	CsvLoadOptions	opt;

	csv_load_options_default(&opt);
	return (load_from_csv_ex(filename, &opt, NULL));
}

void	data_struct_free(DataStruct *data)
{
    size_t	i;
    
    if (!data)
        return;
    for (i = 0; i < data->count; i++)
        free(data->rows[i].event_type);
    free(data->rows);
    free(data);
}
