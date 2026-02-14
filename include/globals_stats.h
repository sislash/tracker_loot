/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   globals_stats.h                                    :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: entropia-tracker                              +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/01/25                                #+#    #+#             */
/*   Updated: 2026/01/25                                #+#    #+#             */
/*                                                                            */
/* ************************************************************************** */

#ifndef GLOBALS_STATS_H
# define GLOBALS_STATS_H

# include <stddef.h>

# define GLOBALS_TOP_MAX 10

typedef struct s_globals_top
{
    char	name[128];
    long	count;
    double	sum_ped;
}	t_globals_top;

typedef struct s_globals_stats
{
    int		csv_has_header;
    long	data_lines_read;
    
    long	mob_events;
    double	mob_sum_ped;
    
    long	craft_events;
    double	craft_sum_ped;
    
    long	rare_events;
    double	rare_sum_ped;
    
    t_globals_top	top_mobs[GLOBALS_TOP_MAX];
    size_t			top_mobs_count;
    
    t_globals_top	top_crafts[GLOBALS_TOP_MAX];
    size_t			top_crafts_count;
    
    t_globals_top	top_rares[GLOBALS_TOP_MAX];
    size_t			top_rares_count;
}	t_globals_stats;

int	globals_stats_compute(const char *csv_path, long start_line,
                          t_globals_stats *out);

#endif
