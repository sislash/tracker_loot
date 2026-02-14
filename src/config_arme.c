/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   config_arme.c                                      :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: login <login@student.42.fr>                +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/01/31 00:00:00 by login             #+#    #+#             */
/*   Updated: 2026/01/31 00:00:00 by login            ###   ###########       */
/*                                                                            */
/* ************************************************************************** */

#include "config_arme.h"
#include "tm_string.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct s_parse_ctx
{
    arme_stats	cur;
    amp_stats	cur_amp;
    int			have_section;
    int			in_player;
    int			in_amp;
}	t_parse_ctx;

static void	str_rtrim(char *s)
{
    size_t	n;
    
    if (!s)
        return ;
    n = strlen(s);
    while (n > 0)
    {
        if (s[n - 1] == '\n' || s[n - 1] == '\r'
            || isspace((unsigned char)s[n - 1]))
            s[--n] = '\0';
        else
            break ;
    }
}

static char	*str_ltrim(char *s)
{
    if (!s)
        return (NULL);
    while (*s && isspace((unsigned char)*s))
        s++;
    return (s);
}

static int	line_is_comment_or_empty(const char *s)
{
    if (!s)
        return (1);
    while (*s && isspace((unsigned char)*s))
        s++;
    if (!*s)
        return (1);
    return (*s == ';' || *s == '#');
}

static int	is_value_char(int c)
{
    if (c == '+' || c == '-' || c == '.' || c == ',')
        return (1);
    if (c == 'e' || c == 'E')
        return (1);
    return (isdigit(c));
}

static int	parse_double_flex(const char *s, double *out)
{
    char	buf[128];
    size_t	i;
    char	*end;
    
    if (!s || !out)
        return (0);
    while (*s && isspace((unsigned char)*s))
        s++;
    i = 0;
    while (s[i] && i + 1 < sizeof(buf) && is_value_char((int)s[i]))
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

/* Parse money in PED (uPED fixed-point). Accepts ',' or '.' decimal. */
static int	parse_money_flex(const char *s, tm_money_t *out)
{
    char    buf[128];
    size_t  i;
    if (!s || !out)
        return (0);
    while (*s && isspace((unsigned char)*s))
        s++;
    i = 0;
    while (s[i] && i + 1 < sizeof(buf) && is_value_char((int)s[i]))
    {
        buf[i] = s[i];
        if (buf[i] == ',')
            buf[i] = '.';
        i++;
    }
    buf[i] = '\0';
    if (i == 0)
        return (0);
    return (tm_money_parse_ped(buf, out) != 0);
}

/* Parse MU multiplier as 1e4 fixed-point (10000=x1.0000). */
static int	parse_mu_1e4_flex(const char *s, int64_t *out, int zero_ok, int64_t def)
{
    tm_money_t tmp;
    if (!out)
        return (0);
    if (!s || !s[0])
    {
        *out = def;
        return (1);
    }
    if (!parse_money_flex(s, &tmp))
        return (0);
    if (!zero_ok && tmp <= 0)
        tmp = (tm_money_t)def;
    if (zero_ok && tmp < 0)
        tmp = 0;
    *out = (int64_t)tmp;
    return (1);
}

static void	arme_defaults(arme_stats *w)
{
    memset(w, 0, sizeof(*w));
    w->markup_mu_1e4 = 10000;
    w->ammo_mu_1e4 = 10000;
    w->weapon_mu_1e4 = 0;
    w->amp_mu_1e4 = 0;
    w->amp_name[0] = '\0';
}

static void	amp_defaults(amp_stats *a)
{
    memset(a, 0, sizeof(*a));
    a->amp_mu_1e4 = 10000;
}

static int	db_amp_push(armes_db *db, const amp_stats *a)
{
    amp_stats	*tmp;
    
    tmp = (amp_stats *)realloc(db->amps.items,
                               (db->amps.count + 1) * sizeof(*db->amps.items));
    if (!tmp)
        return (0);
    db->amps.items = tmp;
    db->amps.items[db->amps.count] = *a;
    db->amps.count++;
    return (1);
}

static int	db_push(armes_db *db, const arme_stats *w)
{
    arme_stats	*tmp;
    
    tmp = (arme_stats *)realloc(db->items,
                                (db->count + 1) * sizeof(*db->items));
    if (!tmp)
        return (0);
    db->items = tmp;
    db->items[db->count] = *w;
    db->count++;
    return (1);
}

static const amp_stats	*find_amp(const armes_db *db, const char *name)
{
    size_t	i;
    
    if (!db || !name || !name[0])
        return (NULL);
    i = 0;
    while (i < db->amps.count)
    {
        if (strcmp(db->amps.items[i].name, name) == 0)
            return (&db->amps.items[i]);
        i++;
    }
    return (NULL);
}

static void	db_init(armes_db *db)
{
    db->items = NULL;
    db->count = 0;
    db->amps.items = NULL;
    db->amps.count = 0;
    db->player_name[0] = '\0';
}

static void	ctx_init(t_parse_ctx *ctx)
{
    arme_defaults(&ctx->cur);
    amp_defaults(&ctx->cur_amp);
    ctx->have_section = 0;
    ctx->in_player = 0;
    ctx->in_amp = 0;
}

static void	save_previous_section(armes_db *db, t_parse_ctx *ctx)
{
    if (!ctx->have_section)
        return ;
    if (ctx->in_amp)
        (void)db_amp_push(db, &ctx->cur_amp);
    else if (!ctx->in_player)
        (void)db_push(db, &ctx->cur);
}

static void	start_section(t_parse_ctx *ctx, const char *sec)
{
    ctx->have_section = 1;
    ctx->in_player = 0;
    ctx->in_amp = 0;
    if (strcmp(sec, "PLAYER") == 0)
        ctx->in_player = 1;
    else if (strncmp(sec, "AMP:", 4) == 0)
    {
        ctx->in_amp = 1;
        amp_defaults(&ctx->cur_amp);
        safe_copy(ctx->cur_amp.name, sizeof(ctx->cur_amp.name), sec + 4);
    }
    else
    {
        arme_defaults(&ctx->cur);
        safe_copy(ctx->cur.name, sizeof(ctx->cur.name), sec);
    }
}

static int	handle_section_line(armes_db *db, t_parse_ctx *ctx, char *p)
{
    char	*end;
    char	*sec;
    
    end = strchr(p, ']');
    if (!end)
        return (0);
    *end = '\0';
    save_previous_section(db, ctx);
    sec = p + 1;
    start_section(ctx, sec);
    return (1);
}

static void	handle_player_kv(armes_db *db, const char *key, const char *val)
{
    if (strcmp(key, "name") == 0)
        safe_copy(db->player_name, sizeof(db->player_name), val);
}

static void	handle_amp_kv(t_parse_ctx *ctx, const char *key, const char *val)
{
    if (strcmp(key, "amp_decay_shot") == 0)
        (void)parse_money_flex(val, &ctx->cur_amp.amp_decay_shot);
    else if (strcmp(key, "amp_mu") == 0)
        (void)parse_mu_1e4_flex(val, &ctx->cur_amp.amp_mu_1e4, 0, 10000);
    else if (strcmp(key, "notes") == 0)
        safe_copy(ctx->cur_amp.notes, sizeof(ctx->cur_amp.notes), val);
}

static void	handle_arme_kv(t_parse_ctx *ctx, const char *key, const char *val)
{
    if (strcmp(key, "dpp") == 0)
        (void)parse_double_flex(val, &ctx->cur.dpp);
    else if (strcmp(key, "ammo_shot") == 0)
        (void)parse_money_flex(val, &ctx->cur.ammo_shot);
    else if (strcmp(key, "decay_shot") == 0)
        (void)parse_money_flex(val, &ctx->cur.decay_shot);
    else if (strcmp(key, "amp_decay_shot") == 0)
        (void)parse_money_flex(val, &ctx->cur.amp_decay_shot);
    else if (strcmp(key, "markup") == 0)
        (void)parse_mu_1e4_flex(val, &ctx->cur.markup_mu_1e4, 0, 10000);
    else if (strcmp(key, "ammo_mu") == 0)
        (void)parse_mu_1e4_flex(val, &ctx->cur.ammo_mu_1e4, 0, 10000);
    else if (strcmp(key, "weapon_mu") == 0)
        (void)parse_mu_1e4_flex(val, &ctx->cur.weapon_mu_1e4, 1, 0);
    else if (strcmp(key, "amp_mu") == 0)
        (void)parse_mu_1e4_flex(val, &ctx->cur.amp_mu_1e4, 1, 0);
    else if (strcmp(key, "notes") == 0)
        safe_copy(ctx->cur.notes, sizeof(ctx->cur.notes), val);
    else if (strcmp(key, "amp") == 0)
        safe_copy(ctx->cur.amp_name, sizeof(ctx->cur.amp_name), val);
}

static int	parse_kv_line(char *p, char **key, char **val)
{
    char	*eq;
    
    eq = strchr(p, '=');
    if (!eq)
        return (0);
    *eq = '\0';
    *key = str_ltrim(p);
    str_rtrim(*key);
    *val = str_ltrim(eq + 1);
    str_rtrim(*val);
    return (1);
}

static void	link_weapon_amps(armes_db *db)
{
    size_t			i;
    const amp_stats	*amp;
    
    i = 0;
    while (i < db->count)
    {
        if (db->items[i].amp_name[0] != '\0')
        {
            amp = find_amp(db, db->items[i].amp_name);
            if (amp)
            {
                db->items[i].amp_decay_shot = amp->amp_decay_shot;
                db->items[i].amp_mu_1e4 = amp->amp_mu_1e4;
            }
            else
            {
                fprintf(stderr, "[WARN] Amp '%s' not found for '%s'\n",
                        db->items[i].amp_name, db->items[i].name);
            }
        }
        i++;
    }
}

static void	process_line(armes_db *db, t_parse_ctx *ctx, char *line)
{
    char	*p;
    char	*key;
    char	*val;
    
    str_rtrim(line);
    p = str_ltrim(line);
    if (line_is_comment_or_empty(p))
        return ;
    if (*p == '[')
        (void)handle_section_line(db, ctx, p);
    else if (ctx->have_section && parse_kv_line(p, &key, &val))
    {
        if (ctx->in_player)
            handle_player_kv(db, key, val);
        else if (ctx->in_amp)
            handle_amp_kv(ctx, key, val);
        else
            handle_arme_kv(ctx, key, val);
    }
}

int	armes_db_load(armes_db *db, const char *path)
{
    FILE		*fp;
    char		line[512];
    t_parse_ctx	ctx;
    
    if (!db || !path)
        return (0);
    db_init(db);
    fp = fopen(path, "r");
    if (!fp)
        return (0);
    ctx_init(&ctx);
    while (fgets(line, sizeof(line), fp))
        process_line(db, &ctx, line);
    save_previous_section(db, &ctx);
    link_weapon_amps(db);
    fclose(fp);
    return (1);
}


void	armes_db_free(armes_db *db)
{
    if (!db)
        return ;
    free(db->items);
    free(db->amps.items);
    db_init(db);
}

const arme_stats	*armes_db_find(const armes_db *db, const char *name)
{
    size_t	i;
    
    if (!db || !name)
        return (NULL);
    i = 0;
    while (i < db->count)
    {
        if (strcmp(db->items[i].name, name) == 0)
            return (&db->items[i]);
        i++;
    }
    return (NULL);
}

tm_money_t	arme_cost_shot_uPED(const arme_stats *w)
{
    int64_t ammo_mu;
    int64_t weapon_mu;
    int64_t amp_mu;

    if (!w)
        return (0);

    if (w->weapon_mu_1e4 > 0 || w->amp_mu_1e4 > 0)
    {
        ammo_mu = (w->ammo_mu_1e4 > 0) ? w->ammo_mu_1e4 : 10000;
        weapon_mu = (w->weapon_mu_1e4 > 0) ? w->weapon_mu_1e4 : 10000;
        amp_mu = (w->amp_mu_1e4 > 0) ? w->amp_mu_1e4 : 10000;
        return (
            tm_money_mul_mu(w->ammo_shot, ammo_mu)
            + tm_money_mul_mu(w->decay_shot, weapon_mu)
            + tm_money_mul_mu(w->amp_decay_shot, amp_mu)
        );
    }
    /* Legacy EU-correct: ammo TT, decays * markup */
    ammo_mu = (w->markup_mu_1e4 > 0) ? w->markup_mu_1e4 : 10000;
    return (
        w->ammo_shot
        + tm_money_mul_mu((w->decay_shot + w->amp_decay_shot), ammo_mu)
    );
}

double	arme_cost_shot_ped(const arme_stats *w)
{
    return tm_money_to_ped_double(arme_cost_shot_uPED(w));
}
