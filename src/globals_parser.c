/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   globals_parser.c                                   :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: login <login@student.42.fr>                +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/01/25 00:00:00 by login             #+#    #+#             */
/*   Updated: 2026/01/31 00:00:00 by login            ###   ###########       */
/*                                                                            */
/* ************************************************************************** */

#include "globals_parser.h"
#include "tm_string.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int	is_ts_prefix(const char *s)
{
    int	i;
    
    if (!s)
        return (0);
    i = 0;
    while (i < 19)
    {
        if (!s[i])
            return (0);
        i++;
    }
    return (isdigit((unsigned char)s[0]) && isdigit((unsigned char)s[1])
    && isdigit((unsigned char)s[2]) && isdigit((unsigned char)s[3])
    && s[4] == '-' && s[7] == '-' && s[10] == ' '
    && s[13] == ':' && s[16] == ':');
}

static void	extract_ts(const char *line, char *out, size_t outsz)
{
    char	tmp[32];
    
    if (!is_ts_prefix(line))
    {
        safe_copy(out, outsz, "");
        return ;
    }
    memcpy(tmp, line, 19);
    tmp[19] = '\0';
    safe_copy(out, outsz, tmp);
}

static int	is_globals_channel(const char *line)
{
    if (!line)
        return (0);
    if (strstr(line, "[Globals]"))
        return (1);
    if (strstr(line, "[Globaux]"))
        return (1);
    return (0);
}

static int	has_hof(const char *line)
{
    if (!line)
        return (0);
    return (strstr(line, "Hall of Fame") != NULL);
}

static int	has_ath(const char *line)
{
    if (!line)
        return (0);
    if (strstr(line, "ALL TIME HIGH") || strstr(line, "All Time High"))
        return (1);
    if (strstr(line, " ATH") || strstr(line, "(ATH") || strstr(line, "ATH!"))
        return (1);
    if (strstr(line, "RECORD ABSOLU") || strstr(line, "Record absolu"))
        return (1);
    return (0);
}

static int	parse_between_parens(const char *p, char *out, size_t outsz)
{
    const char	*o;
    const char	*c;
    size_t		n;
    
    if (!p)
        return (0);
    o = strchr(p, '(');
    if (!o)
        return (0);
    c = strchr(o + 1, ')');
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

static int	is_value_char(int c)
{
    return (isdigit((unsigned char)c) || c == '.' || c == ',');
}

static int	parse_value_number(const char *p, double *out)
{
    char		buf[64];
    size_t		i;
    const char	*s;
    char		*end;
    
    if (!p || !out)
        return (0);
    s = p;
    while (*s && isspace((unsigned char)*s))
        s++;
    i = 0;
    while (s[i] && i + 1 < sizeof(buf) && is_value_char(s[i]))
    {
        buf[i] = s[i];
        if (buf[i] == ',')
            buf[i] = '.';
        i++;
    }
    buf[i] = '\0';
    if (i == 0)
        return (0);
    *out = strtod(buf, &end);
    return (end != buf);
}

static void	set_type(char *dst, size_t dstsz, const char *base,
                     int ath, int hof)
{
    char	tmp[64];
    
    if (ath)
        snprintf(tmp, sizeof(tmp), "ATH_%s", base);
    else if (hof)
        snprintf(tmp, sizeof(tmp), "HOF_%s", base);
    else
        snprintf(tmp, sizeof(tmp), "GLOB_%s", base);
    safe_copy(dst, dstsz, tmp);
}

static const char	*fr_value_ptr(const char *p)
{
    const char	*s;
    
    s = strstr(p, "avec une valeur de ");
    if (s)
        return (s + strlen("avec une valeur de "));
    s = strstr(p, "d'une valeur de ");
    if (s)
        return (s + strlen("d'une valeur de "));
    s = strstr(p, "valant ");
    if (s)
        return (s + strlen("valant "));
    s = strstr(p, "valeur de ");
    if (s)
        return (s + strlen("valeur de "));
    return (NULL);
}

static int	parse_mob_en(const char *line, t_globals_event *ev,
                         int ath, int hof)
{
    const char	*p;
    double		v;
    
    p = strstr(line, "killed a creature (");
    if (!p)
        return (0);
    if (!parse_between_parens(p, ev->name, sizeof(ev->name)))
        return (0);
    p = strstr(p, "with a value of ");
    if (!p)
        return (0);
    p += strlen("with a value of ");
    if (!parse_value_number(p, &v))
        return (0);
    snprintf(ev->value, sizeof(ev->value), "%.4f", v);
    set_type(ev->type, sizeof(ev->type), "MOB", ath, hof);
    return (1);
}

static int	parse_mob_fr(const char *line, t_globals_event *ev,
                         int ath, int hof)
{
    const char	*p;
    const char	*vp;
    double		v;
    
    p = strstr(line, "a tué une créature (");
    if (!p)
        p = strstr(line, "a tue une creature (");
    if (!p)
        return (0);
    if (!parse_between_parens(p, ev->name, sizeof(ev->name)))
        return (0);
    vp = fr_value_ptr(p);
    if (!vp || !parse_value_number(vp, &v))
        return (0);
    snprintf(ev->value, sizeof(ev->value), "%.4f", v);
    set_type(ev->type, sizeof(ev->type), "MOB", ath, hof);
    return (1);
}

static int	parse_craft_en(const char *line, t_globals_event *ev,
                           int ath, int hof)
{
    const char	*p;
    double		v;
    
    p = strstr(line, "constructed an item (");
    if (!p)
        return (0);
    if (!parse_between_parens(p, ev->name, sizeof(ev->name)))
        return (0);
    p = strstr(p, "worth ");
    if (!p)
        return (0);
    p += strlen("worth ");
    if (!parse_value_number(p, &v))
        return (0);
    snprintf(ev->value, sizeof(ev->value), "%.4f", v);
    set_type(ev->type, sizeof(ev->type), "CRAFT", ath, hof);
    return (1);
}

static int	parse_craft_fr(const char *line, t_globals_event *ev,
                           int ath, int hof)
{
    const char	*p;
    const char	*vp;
    double		v;
    
    p = strstr(line, "a construit un objet (");
    if (!p)
        p = strstr(line, "a fabriqué un objet (");
    if (!p)
        return (0);
    if (!parse_between_parens(p, ev->name, sizeof(ev->name)))
        return (0);
    vp = fr_value_ptr(p);
    if (!vp || !parse_value_number(vp, &v))
        return (0);
    snprintf(ev->value, sizeof(ev->value), "%.4f", v);
    set_type(ev->type, sizeof(ev->type), "CRAFT", ath, hof);
    return (1);
}

static int	parse_rare_en(const char *line, t_globals_event *ev,
                          int ath, int hof)
{
    const char	*p;
    double		v;
    
    p = strstr(line, "has found a rare item (");
    if (!p)
        return (0);
    if (!parse_between_parens(p, ev->name, sizeof(ev->name)))
        return (0);
    p = strstr(p, "with a value of ");
    if (!p)
        return (0);
    p += strlen("with a value of ");
    if (!parse_value_number(p, &v))
        return (0);
    if (strstr(p, "PEC"))
        v = v / 100.0;
    snprintf(ev->value, sizeof(ev->value), "%.6f", v);
    set_type(ev->type, sizeof(ev->type), "RARE", ath, hof);
    return (1);
}

static void	init_event(const char *line, t_globals_event *ev)
{
    memset(ev, 0, sizeof(*ev));
    extract_ts(line, ev->ts, sizeof(ev->ts));
    safe_copy(ev->raw, sizeof(ev->raw), line);
}

int	globals_parse_line(const char *line, t_globals_event *ev)
{
    int	ath;
    int	hof;
    
    if (!line || !ev)
        return (0);
    if (!is_globals_channel(line))
        return (0);
    init_event(line, ev);
    hof = has_hof(line);
    ath = has_ath(line);
    if (parse_mob_en(line, ev, ath, hof))
        return (1);
    if (parse_mob_fr(line, ev, ath, hof))
        return (1);
    if (parse_craft_en(line, ev, ath, hof))
        return (1);
    if (parse_craft_fr(line, ev, ath, hof))
        return (1);
    if (parse_rare_en(line, ev, ath, hof))
        return (1);
    return (0);
}
