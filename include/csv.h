/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   csv.h                                              :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: entropia-tracker <entropia-tracker@student.42.fr> +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/01/31 00:00:00 by entropia-tracker    #+#    #+#             */
/*   Updated: 2026/01/31 00:00:00 by entropia-tracker    ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef CSV_H
# define CSV_H

# include <stdio.h>

/*
 * CSV policy (portable Windows/Linux):
 * - UTF-8 bytes (no transcoding)
 * - RFC4180-ish quoting: fields containing separator/quotes/CR/LF are quoted,
 *   quotes are escaped as "".
 */
# define CSV_SEP ','

int		csv_split_n(char *line, char **out, int n);
/* Strict: returns 1 only if the line has exactly n columns (no more, no less). */
int		csv_split_n_strict(char *line, char **out, int n);

/* Variants with explicit separator (e.g. ';' for Excel locales). */
int		csv_split_n_sep(char *line, char **out, int n, char sep);
/* Strict: exactly n columns for the given separator. */
int		csv_split_n_strict_sep(char *line, char **out, int n, char sep);
void	csv_write_field(FILE *f, const char *s);
void	csv_write_field_sep(FILE *f, const char *s, char sep);
/* Generic row writer (n fields). */
void	csv_write_row(FILE *f, const char **fields, int n);
void	csv_write_row_sep(FILE *f, const char **fields, int n, char sep);
void	csv_write_row6(FILE *f, const char *f0, const char *f1,
			const char *f2, const char *f3,
			const char *f4, const char *f5);
void	csv_write_row6_sep(FILE *f, const char *f0, const char *f1,
			const char *f2, const char *f3,
			const char *f4, const char *f5, char sep);
void	csv_ensure_header6(FILE *f);
void	csv_ensure_header6_sep(FILE *f, char sep);

#endif
