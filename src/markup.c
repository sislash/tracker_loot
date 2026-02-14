/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   markup.c                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: login <login@student.42.fr>                +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/01/27 00:00:00 by login             #+#    #+#             */
/*   Updated: 2026/01/27 00:00:00 by login            ###   ###########       */
/*                                                                            */
/* ************************************************************************** */

#include "markup.h"
#include "markup_ini.h"

#include <stdlib.h>
#include <string.h>

static void	safe_zero(void *p, size_t n)
{
    if (p && n)
        memset(p, 0, n);
}

void	markup_db_init(t_markup_db *db)
{
    if (!db)
        return ;
    db->items = NULL;
    db->count = 0;
    db->cap = 0;
}

void	markup_db_free(t_markup_db *db)
{
    if (!db)
        return ;
    free(db->items);
    db->items = NULL;
    db->count = 0;
    db->cap = 0;
}

static int	db_reserve(t_markup_db *db, size_t need)
{
    t_markup_rule	*new_items;
    size_t			new_cap;
    
    if (db->cap >= need)
        return (0);
    new_cap = db->cap;
    if (new_cap == 0)
        new_cap = 16;
    while (new_cap < need)
        new_cap *= 2;
    new_items = (t_markup_rule *)realloc(db->items,
                                         new_cap * sizeof(t_markup_rule));
    if (!new_items)
        return (-1);
    db->items = new_items;
    db->cap = new_cap;
    return (0);
}

static int	db_push(t_markup_db *db, const t_markup_rule *r)
{
    if (!db || !r)
        return (-1);
    if (db_reserve(db, db->count + 1) != 0)
        return (-1);
    db->items[db->count] = *r;
    db->count++;
    return (0);
}

static int	db_update_if_exists(t_markup_db *db, const t_markup_rule *r)
{
    size_t	i;
    
    i = 0;
    while (i < db->count)
    {
        if (strcmp(db->items[i].name, r->name) == 0)
        {
            db->items[i] = *r;
            return (1);
        }
        i++;
    }
    return (0);
}

int	markup_db_load(t_markup_db *db, const char *path)
{
    if (!db || !path)
        return (-1);
    markup_db_free(db);
    markup_db_init(db);
    if (markup_ini_parse_file(db, path) != 0)
    {
        markup_db_free(db);
        return (-1);
    }
    return (0);
}

int	markup_db_get(const t_markup_db *db, const char *item_name,
                  t_markup_rule *out)
{
    size_t	i;
    
    if (!db || !item_name || !out)
        return (0);
    i = 0;
    while (i < db->count)
    {
        if (strcmp(db->items[i].name, item_name) == 0)
        {
            *out = db->items[i];
            return (1);
        }
        i++;
    }
    return (0);
}

double	markup_apply(const t_markup_rule *r, double tt_value)
{
    if (!r)
        return (tt_value);
    if (r->type == MARKUP_TT_PLUS)
        return (tt_value + r->value);
    return (tt_value * r->value);
}

t_markup_rule	markup_default_rule(void)
{
    t_markup_rule	r;
    
    safe_zero(&r, sizeof(r));
    strncpy(r.name, "(default)", sizeof(r.name) - 1);
    r.type = MARKUP_PERCENT;
    r.value = 1.0;
    return (r);
}

/*
 * * Called by the INI parser when it wants to insert a rule:
 ** - if the name already exists => update
 ** - otherwise => append
 */
int	markup__db_insert_or_update(t_markup_db *db, const t_markup_rule *r)
{
    int	updated;
    
    if (!db || !r)
        return (-1);
    updated = db_update_if_exists(db, r);
    if (updated == 1)
        return (0);
    return (db_push(db, r));
}
