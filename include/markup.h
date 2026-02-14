/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   markup.h                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: entropia-tracker                              +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/01/27                                #+#    #+#             */
/*   Updated: 2026/01/27                                #+#    #+#             */
/*                                                                            */
/* ************************************************************************** */

#ifndef MARKUP_H
# define MARKUP_H

# include <stddef.h>

typedef enum e_markup_type
{
    MARKUP_PERCENT = 0,
    MARKUP_TT_PLUS = 1
}	t_markup_type;

typedef struct s_markup_rule
{
    char			name[128];
    t_markup_type	type;
    double			value;
}	t_markup_rule;

typedef struct s_markup_db
{
    t_markup_rule	*items;
    size_t			count;
    size_t			cap;
}	t_markup_db;

void	markup_db_init(t_markup_db *db);
void	markup_db_free(t_markup_db *db);

/*
 * * Charge markup.ini (format: [Item] + type/value).
 ** Retour:
 **  0 OK
 ** -1 erreur (fichier / parse / alloc)
 */
int		markup_db_load(t_markup_db *db, const char *path);

/* Save whole db back to an INI file (rewrite file). Returns 0 on success. */
int		markup_db_save(const t_markup_db *db, const char *path);

/*
 * * Recherche une règle par nom exact.
 ** Retourne 1 si trouvé, 0 sinon.
 */
int		markup_db_get(const t_markup_db *db, const char *item_name,
                      t_markup_rule *out);

/*
 * * Applique une règle:
 **  percent => tt * value
 **  tt_plus => tt + value
 */
double	markup_apply(const t_markup_rule *r, double tt_value);

/*
 * * Fallback rule (percent 1.00)
 */
t_markup_rule	markup_default_rule(void);

#endif
