/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   markup_ini.c                                       :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: login <login@student.42.fr>                +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/01/31 00:00:00                      #+#    #+#             */
/*   Updated: 2026/01/31 00:00:00                     ###   ###########       */
/*                                                                            */
/* ************************************************************************** */

#include "markup_ini.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* from markup.c (internal insert/update) */
int	markup__db_insert_or_update(t_markup_db *db, const t_markup_rule *r);

static void	trim_left(char **s)
{
    while (**s && isspace((unsigned char)**s))
        (*s)++;
}

static void	trim_right(char *s)
{
    size_t	n;
    
    if (!s)
        return ;
    n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1]))
    {
        s[n - 1] = '\0';
        n--;
    }
}

static int	is_empty_or_comment(const char *s)
{
    if (!s)
        return (1);
    while (*s && isspace((unsigned char)*s))
        s++;
    if (*s == '\0')
        return (1);
    if (*s == ';' || *s == '#')
        return (1);
    return (0);
}

static int	parse_section(const char *line, char *out, size_t outsz)
{
    const char	*o;
    const char	*c;
    size_t		n;
    
    o = strchr(line, '[');
    if (!o)
        return (0);
    c = strchr(o + 1, ']');
    if (!c)
        return (0);
    n = (size_t)(c - (o + 1));
    if (n == 0)
        return (0);
    if (n >= outsz)
        n = outsz - 1;
    memcpy(out, o + 1, n);
    out[n] = '\0';
    return (1);
}

static int	streq_nocase(const char *a, const char *b)
{
    while (*a && *b)
    {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return (0);
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0');
}

static int	parse_key_value(char *line, char *key, size_t ksz,
                            char *val, size_t vsz)
{
    char	*eq;
    char	*s;
    s = line;
    key[0] = '\0'; val[0] = '\0';
    trim_left(&s);
    trim_right(s);
    if (is_empty_or_comment(s))
        return (0);
    eq = strchr(s, '=');
    if (!eq)
        return (0);
    *eq = '\0';
    trim_right(s);
    while (*s && isspace((unsigned char)*s))
        s++;
    strncpy(key, s, ksz - 1);
    key[ksz - 1] = '\0';
    eq++;
    trim_left(&eq);
    trim_right(eq);
    strncpy(val, eq, vsz - 1);
    val[vsz - 1] = '\0';
    return (1);
}

static int	parse_type_value(const char *s, t_markup_type *out)
{
    if (!s || !out)
        return (0);
    if (streq_nocase(s, "percent"))
    {
        *out = MARKUP_PERCENT;
        return (1);
    }
    if (streq_nocase(s, "tt_plus"))
    {
        *out = MARKUP_TT_PLUS;
        return (1);
    }
    return (0);
}

typedef struct s_ini_ctx
{
    t_markup_rule	cur;
    int			in_section;
    int			has_type;
    int			has_value;
}	t_ini_ctx;

static void	ctx_reset(t_ini_ctx *ctx)
{
    memset(&ctx->cur, 0, sizeof(ctx->cur));
    ctx->in_section = 0;
    ctx->has_type = 0;
    ctx->has_value = 0;
}

static int	ctx_flush(t_markup_db *db, t_ini_ctx *ctx)
{
    if (!ctx->in_section || !ctx->cur.name[0] || !ctx->has_value)
        return (0);
    if (!ctx->has_type)
        ctx->cur.type = MARKUP_PERCENT;
    if (markup__db_insert_or_update(db, &ctx->cur) != 0)
        return (-1);
    return (0);
}

static int	ctx_start_section(t_markup_db *db, t_ini_ctx *ctx,
                              char *line, char *section, size_t sectionsz)
{
    if (ctx_flush(db, ctx) != 0)
        return (-1);
    memset(&ctx->cur, 0, sizeof(ctx->cur));
    ctx->has_type = 0;
    ctx->has_value = 0;
    if (!parse_section(line, section, sectionsz))
        return (0);
    strncpy(ctx->cur.name, section, sizeof(ctx->cur.name) - 1);
    ctx->cur.name[sizeof(ctx->cur.name) - 1] = '\0';
    ctx->in_section = 1;
    return (1);
}

static void	ctx_apply_kv(t_ini_ctx *ctx, const char *key, const char *val)
{
    if (streq_nocase(key, "type"))
    {
        if (parse_type_value(val, &ctx->cur.type))
            ctx->has_type = 1;
    }
    else if (streq_nocase(key, "value"))
    {
        ctx->cur.value = strtod(val, NULL);
        ctx->has_value = 1;
    }
}

static int	process_line(t_markup_db *db, t_ini_ctx *ctx, char *line,
                         char *section, size_t sectionsz, char *key, char *val)
{
    int	ret;
    
    trim_left(&line);
    trim_right(line);
    if (is_empty_or_comment(line))
        return (0);
    if (line[0] == '[')
    {
        ret = ctx_start_section(db, ctx, line, section, sectionsz);
        if (ret < 0)
            return (-1);
        return (0);
    }
    if (!ctx->in_section)
        return (0);
    if (!parse_key_value(line, key, 64, val, 256))
        return (0);
    ctx_apply_kv(ctx, key, val);
    return (0);
}

static int	ini_parse_stream(t_markup_db *db, FILE *f, t_ini_ctx *ctx)
{
    char	buf[512];
    char	section[128];
    char	key[64];
    char	val[256];
    
    while (fgets(buf, sizeof(buf), f))
    {
        if (process_line(db, ctx, buf, section, sizeof(section), key, val) != 0)
            return (-1);
    }
    if (ferror(f) || ctx_flush(db, ctx) != 0)
        return (-1);
    return (0);
}

int	markup_ini_parse_file(t_markup_db *db, const char *path)
{
    FILE		*f;
    t_ini_ctx	ctx;
    int			ret;
    
    if (!db || !path)
        return (-1);
    f = fopen(path, "rb");
    if (!f)
        return (-1);
    ctx_reset(&ctx);
    ret = ini_parse_stream(db, f, &ctx);
    fclose(f);
    return (ret);
}
