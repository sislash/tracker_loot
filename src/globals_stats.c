/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   globals_stats.c                                    :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: login <login@student.42.fr>                +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/01/31 00:00:00 by login             #+#    #+#             */
/*   Updated: 2026/01/31 00:00:00 by login             ###   ###########      */
/*                                                                            */
/* ************************************************************************** */
#include "globals_stats.h"
#include "csv.h"
#include "utils.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#define CSV_COLS_N 6
#define LINE_BUF_SZ 8192
#define TOP_MAX 10

typedef struct s_kv_sum
{
    char	*key;
    long	count;
    double	sum;
}	t_kv_sum;

static void	stats_zero(t_globals_stats *s)
{
	tm_zero(s, sizeof(*s));
}

static char	*xstrdup(const char *s)
{
    size_t	len;
    char	*p;
    
    if (!s)
        return (NULL);
    len = strlen(s) + 1;
    p = (char *)malloc(len);
    if (!p)
        return (NULL);
    memcpy(p, s, len);
    return (p);
}

/* number parsing helpers are centralized in utils.c (tm_parse_double) */

static const char	*kv_key(const char *key)
{
    if (!key || !key[0])
        return ("(unknown)");
    return (key);
}

static void	kv_sum_push(t_kv_sum **arr, size_t *len,
                        const char *key, double v)
{
    t_kv_sum	*tmp;
    
    tmp = (t_kv_sum *)realloc(*arr, (*len + 1) * sizeof(**arr));
    if (!tmp)
        return ;
    *arr = tmp;
    (*arr)[*len].key = xstrdup(key);
    (*arr)[*len].count = 1;
    (*arr)[*len].sum = v;
    (*len)++;
}

static void	kv_sum_add(t_kv_sum **arr, size_t *len, const char *key, double v)
{
    size_t	i;
    
    key = kv_key(key);
    i = 0;
    while (i < *len)
    {
        if ((*arr)[i].key && strcmp((*arr)[i].key, key) == 0)
        {
            (*arr)[i].count++;
            (*arr)[i].sum += v;
            return ;
        }
        i++;
    }
    kv_sum_push(arr, len, key, v);
}

static int	kv_sum_cmp_desc(const void *a, const void *b)
{
    const t_kv_sum	*ka;
    const t_kv_sum	*kb;
    
    ka = (const t_kv_sum *)a;
    kb = (const t_kv_sum *)b;
    if (ka->sum < kb->sum)
        return (1);
    if (ka->sum > kb->sum)
        return (-1);
    if (ka->count < kb->count)
        return (1);
    if (ka->count > kb->count)
        return (-1);
    if (!ka->key)
        return (1);
    if (!kb->key)
        return (-1);
    return (strcmp(ka->key, kb->key));
}

static void	kv_sum_free(t_kv_sum *arr, size_t len)
{
	tm_free_str_key_array(arr, len, sizeof(*arr), offsetof(t_kv_sum, key));
}

static int	is_mob_type(const char *type)
{
    return (type && strstr(type, "_MOB") != NULL);
}

static int	is_craft_type(const char *type)
{
    return (type && strstr(type, "_CRAFT") != NULL);
}

static int	is_rare_type(const char *type)
{
    return (type && strstr(type, "_RARE") != NULL);
}

/* trim helpers are centralized in utils.c (tm_trim_eol) */

static int	skip_csv_header(int *is_first, const char *line,
                            t_globals_stats *out)
{
    if (!*is_first)
        return (0);
    *is_first = 0;
	if (strstr(line, "timestamp")
		&& (strstr(line, "event_type") || strstr(line, ",type,")))
    {
        out->csv_has_header = 1;
        return (1);
    }
    return (0);
}

static void	fill_top(t_globals_top *out, size_t *out_n,
                     t_kv_sum *arr, size_t len)
{
    size_t	n;
    size_t	i;
    
    *out_n = 0;
    if (!arr || len == 0)
        return ;
    qsort(arr, len, sizeof(*arr), kv_sum_cmp_desc);
    n = len;
    if (n > TOP_MAX)
        n = TOP_MAX;
    i = 0;
    while (i < n)
    {
        snprintf(out[i].name, sizeof(out[i].name), "%s",
                 (arr[i].key) ? arr[i].key : "(null)");
        out[i].count = arr[i].count;
        out[i].sum_ped = arr[i].sum;
        i++;
    }
    *out_n = n;
}

static void	update_bucket(t_globals_stats *out, const char *type,
                          const char *name, double v,
                          t_kv_sum **mobs, size_t *mobs_len,
                          t_kv_sum **crafts, size_t *crafts_len,
                          t_kv_sum **rares, size_t *rares_len)
{
    if (is_mob_type(type))
    {
        out->mob_events++;
        out->mob_sum_ped += v;
        kv_sum_add(mobs, mobs_len, name, v);
    }
    else if (is_craft_type(type))
    {
        out->craft_events++;
        out->craft_sum_ped += v;
        kv_sum_add(crafts, crafts_len, name, v);
    }
    else if (is_rare_type(type))
    {
        out->rare_events++;
        out->rare_sum_ped += v;
        kv_sum_add(rares, rares_len, name, v);
    }
}

static void	process_csv_line(t_globals_stats *out, const char *line,
                             t_kv_sum **mobs, size_t *mobs_len,
                             t_kv_sum **crafts, size_t *crafts_len,
                             t_kv_sum **rares, size_t *rares_len)
{
    char	linecpy[LINE_BUF_SZ];
    char	*cols[CSV_COLS_N];
    double	v;
    
    strncpy(linecpy, line, sizeof(linecpy) - 1);
    linecpy[sizeof(linecpy) - 1] = '\0';
    csv_split_n(linecpy, cols, CSV_COLS_N);
    v = 0.0;
	if (!tm_parse_double((cols[4]) ? cols[4] : "", &v))
        return ;
    update_bucket(out, (cols[1]) ? cols[1] : "",
                  (cols[2]) ? cols[2] : "", v,
                  mobs, mobs_len, crafts, crafts_len, rares, rares_len);
}

static int	should_process_line(t_globals_stats *out, char *buf, int *is_first,
                                long *data_idx, long start_line)
{
	tm_trim_eol(buf);
    if (skip_csv_header(is_first, buf, out))
        return (0);
    if (*data_idx < start_line)
    {
        (*data_idx)++;
        return (0);
    }
    (*data_idx)++;
    out->data_lines_read++;
    return (1);
}

static void	finalize_stats(t_globals_stats *out,
                           t_kv_sum *mobs, size_t mobs_len,
                           t_kv_sum *crafts, size_t crafts_len,
                           t_kv_sum *rares, size_t rares_len)
{
    fill_top(out->top_mobs, &out->top_mobs_count, mobs, mobs_len);
    fill_top(out->top_crafts, &out->top_crafts_count, crafts, crafts_len);
    fill_top(out->top_rares, &out->top_rares_count, rares, rares_len);
    kv_sum_free(mobs, mobs_len);
    kv_sum_free(crafts, crafts_len);
    kv_sum_free(rares, rares_len);
}

static void	read_csv_loop(FILE *f, long start_line, t_globals_stats *out,
                          t_kv_sum **mobs, size_t *mobs_len,
                          t_kv_sum **crafts, size_t *crafts_len,
                          t_kv_sum **rares, size_t *rares_len)
{
    char	buf[LINE_BUF_SZ];
    long	data_idx;
    int		is_first;
    
    data_idx = 0;
    is_first = 1;
    while (fgets(buf, (int)sizeof(buf), f))
    {
        if (!should_process_line(out, buf, &is_first, &data_idx, start_line))
            continue ;
        process_csv_line(out, buf,
                         mobs, mobs_len, crafts, crafts_len, rares, rares_len);
    }
}

static void	init_kv_arrays(t_kv_sum **mobs, size_t *mobs_len,
                           t_kv_sum **crafts, size_t *crafts_len,
                           t_kv_sum **rares, size_t *rares_len)
{
    *mobs = NULL;
    *crafts = NULL;
    *rares = NULL;
    *mobs_len = 0;
    *crafts_len = 0;
    *rares_len = 0;
}

int	globals_stats_compute(const char *csv_path, long start_line,
                          t_globals_stats *out)
{
    FILE		*f;
    t_kv_sum	*mobs;
    t_kv_sum	*crafts;
    t_kv_sum	*rares;
    size_t		mobs_len;
    size_t		crafts_len;
    size_t		rares_len;
    
    if (!csv_path || !out)
        return (-1);
    stats_zero(out);
    f = fopen(csv_path, "rb");
    if (!f)
        return (-1);
    init_kv_arrays(&mobs, &mobs_len, &crafts, &crafts_len, &rares, &rares_len);
    read_csv_loop(f, start_line, out,
                  &mobs, &mobs_len, &crafts, &crafts_len, &rares, &rares_len);
    fclose(f);
    finalize_stats(out, mobs, mobs_len, crafts, crafts_len, rares, rares_len);
    return (0);
}
