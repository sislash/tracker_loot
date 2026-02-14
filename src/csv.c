/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   csv.c                                              :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   Portable CSV helpers (UTF-8, RFC4180-ish quoting)                         */
/*                                                                            */
/* ************************************************************************** */

#include "csv.h"

#include <ctype.h>
#include <string.h>

static int	csv_is_eol(char c)
{
	return (c == '\0' || c == '\n' || c == '\r');
}

/*
 * Parse one CSV field in-place for a given separator.
 * - Supports RFC4180-ish quoted fields and "" escaping.
 * - Writes a '\0' terminator at field end.
 * Returns pointer to next field start, or NULL if end-of-line.
 * If 'malformed' is set to 1, the line is syntactically invalid.
 */
static char	*csv_parse_one_sep(char *p, char **out_field, int *malformed, char sep)
{
	char	*rd;
	char	*wr;
	int		closed;

	if (malformed)
		*malformed = 0;
	if (!p || !out_field)
		return (NULL);
	*out_field = p;
	if (csv_is_eol(*p))
		return (NULL);
	if (*p != '"')
	{
		while (*p && *p != sep && *p != '\n' && *p != '\r')
			p++;
		if (*p == sep)
		{
			*p = '\0';
			return (p + 1);
		}
		if (*p)
			*p = '\0';
		return (NULL);
	}

	/* Quoted field */
	rd = p + 1;
	wr = p;
	closed = 0;
	while (*rd)
	{
		if (*rd == '"')
		{
			if (rd[1] == '"')
			{
				*wr++ = '"';
				rd += 2;
				continue ;
			}
			rd++;
			closed = 1;
			break ;
		}
		*wr++ = *rd++;
	}
	*wr = '\0';
	if (!closed)
	{
		if (malformed)
			*malformed = 1;
		return (NULL);
	}

	/* After closing quote: allow only whitespace until separator or EOL */
	while (*rd && *rd != sep && *rd != '\n' && *rd != '\r')
	{
		if (!isspace((unsigned char)*rd))
		{
			if (malformed)
				*malformed = 1;
			break ;
		}
		rd++;
	}
	if (*rd == sep)
		return (rd + 1);
	return (NULL);
}

void	csv_ensure_header6_sep(FILE *f, char sep)
{
	long	endpos;

	if (!f)
		return ;
	if (fseek(f, 0, SEEK_END) != 0)
		return ;
	endpos = ftell(f);
	if (endpos == 0)
	{
		fprintf(f, "timestamp%cevent_type%ctarget_or_item%cqty%cvalue%craw\n",
			sep, sep, sep, sep, sep);
	}
	(void)fseek(f, 0, SEEK_END);
}

void	csv_ensure_header6(FILE *f)
{
	csv_ensure_header6_sep(f, CSV_SEP);
}

int	csv_split_n_sep(char *line, char **out, int n, char sep)
{
	int		i;
	char	*p;
	int		malformed;
	char	*next;

	if (!line || !out || n <= 0)
		return (0);
	for (i = 0; i < n; i++)
		out[i] = NULL;
	malformed = 0;
	p = line;
	i = 0;
	while (i < n)
	{
		if (!p)
			break ;
		/* Empty field at end (trailing separator) */
		if (*p == '\0')
		{
			out[i++] = p;
			break ;
		}
		next = csv_parse_one_sep(p, &out[i], &malformed, sep);
		if (malformed)
			break ;
		i++;
		p = next;
		if (!p)
			break ;
	}
	return (i);
}

int	csv_split_n(char *line, char **out, int n)
{
	return (csv_split_n_sep(line, out, n, CSV_SEP));
}

int	csv_split_n_strict_sep(char *line, char **out, int n, char sep)
{
	int		i;
	char	*p;
	int		malformed;
	char	*next;

	if (!line || !out || n <= 0)
		return (0);
	for (i = 0; i < n; i++)
		out[i] = NULL;
	malformed = 0;
	p = line;
	i = 0;
	while (i < n)
	{
		if (!p)
			return (0);
		/* Accept empty last column if line ends with a trailing separator */
		if (*p == '\0')
		{
			if (i + 1 != n)
				return (0);
			out[i++] = p;
			p = NULL;
			break ;
		}
		next = csv_parse_one_sep(p, &out[i], &malformed, sep);
		if (malformed)
			return (0);
		i++;
		p = next;
	}
	/* If there is a next field start, it means there are extra columns */
	if (p != NULL)
		return (0);
	return (1);
}

int	csv_split_n_strict(char *line, char **out, int n)
{
	return (csv_split_n_strict_sep(line, out, n, CSV_SEP));
}

static int	csv_needs_quotes_sep(const char *s, char sep)
{
	size_t	len;

	if (!s || !*s)
		return (0);
	if (strchr(s, sep) || strchr(s, '"') || strchr(s, '\n') || strchr(s, '\r'))
		return (1);
	len = strlen(s);
	if (len > 0 && (s[0] == ' ' || s[0] == '\t'
			|| s[len - 1] == ' ' || s[len - 1] == '\t'))
		return (1);
	return (0);
}

void	csv_write_field_sep(FILE *f, const char *s, char sep)
{
	const char	*p;

	if (!f)
		return ;
	if (!s)
		s = "";
	if (!csv_needs_quotes_sep(s, sep))
	{
		fputs(s, f);
		return ;
	}
	fputc('"', f);
	p = s;
	while (*p)
	{
		if (*p == '"')
			fputs("\"\"", f);
		else
			fputc(*p, f);
		p++;
	}
	fputc('"', f);
}

void	csv_write_field(FILE *f, const char *s)
{
	csv_write_field_sep(f, s, CSV_SEP);
}

void	csv_write_row_sep(FILE *f, const char **fields, int n, char sep)
{
	int	i;

	if (!f || !fields || n <= 0)
		return ;
	i = 0;
	while (i < n)
	{
		csv_write_field_sep(f, fields[i], sep);
		if (i + 1 < n)
			fputc(sep, f);
		i++;
	}
	fputc('\n', f);
}

void	csv_write_row(FILE *f, const char **fields, int n)
{
	csv_write_row_sep(f, fields, n, CSV_SEP);
}

void	csv_write_row6_sep(FILE *f, const char *f0, const char *f1,
			const char *f2, const char *f3,
			const char *f4, const char *f5, char sep)
{
	const char	*fields[6];

	fields[0] = f0;
	fields[1] = f1;
	fields[2] = f2;
	fields[3] = f3;
	fields[4] = f4;
	fields[5] = f5;
	csv_write_row_sep(f, fields, 6, sep);
}

void	csv_write_row6(FILE *f, const char *f0, const char *f1,
			const char *f2, const char *f3,
			const char *f4, const char *f5)
{
	csv_write_row6_sep(f, f0, f1, f2, f3, f4, f5, CSV_SEP);
}
