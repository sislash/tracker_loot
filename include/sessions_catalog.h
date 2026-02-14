/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   sessions_catalog.h                                  :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: tracker_loot <tracker_loot@student.42.fr>    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/02/12                                  #+#    #+#         */
/*   Updated: 2026/02/12                                  ###   ########.fr   */
/*                                                                            */
/* ************************************************************************** */

#ifndef SESSIONS_CATALOG_H
# define SESSIONS_CATALOG_H

# include <stddef.h>

/*
 * sessions_stats.csv reader (append-only):
 *
 * Legacy columns (v1):
 *   session_start,session_end,weapon,kills,shots,loot_ped,expense_ped,net_ped,return_pct
 *
 * New columns (v2, optional at end):
 *   ...,start_offset,end_offset

 * New columns (v3, optional at end):
 *   ...,mob
 *
 * We keep backward compatibility by accepting both formats line-by-line.
 */

typedef struct s_session_entry
{
	char	start_ts[64];
	char	end_ts[64];
	char	weapon[128];
	long	kills;
	long	shots;
	double	loot_ped;
	double	expense_ped;
	double	net_ped;
	double	return_pct;
	long	start_offset;
	long	end_offset;
	int	has_offsets;
	char	mob[128];
	int	has_mob;
} 		t_session_entry;

typedef struct s_sessions_list
{
	t_session_entry	*items;
	size_t			count;
} 		t_sessions_list;

/* Load up to max_items last sessions (0 => load all). Returns 0 on success. */
int		sessions_list_load(const char *csv_path, size_t max_items, t_sessions_list *out);
void	sessions_list_free(t_sessions_list *l);

/* Build a compact label for UI lists. */
int		sessions_format_label(const t_session_entry *e, char *out, size_t outsz);

#endif
