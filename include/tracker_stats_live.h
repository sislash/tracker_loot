/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   tracker_stats_live.h                               :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: tracker_loot                                  +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/02/12                                #+#    #+#             */
/*   Updated: 2026/02/12                                ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef TRACKER_STATS_LIVE_H
# define TRACKER_STATS_LIVE_H

# include "tracker_stats.h"

/*
 * Live incremental stats cache.
 *
 * Goals:
 *  - Avoid rescanning logs/hunt_log.csv every 250ms (overlay + pages).
 *  - Follow current session offset (logs/hunt_session.offset).
 *  - If a session range is loaded (logs/hunt_session.range), compute once.
 *
 * NOTE:
 *  - This cache is updated from the UI thread only.
 */

void				tracker_stats_live_force_reset(void);

/* Tick: updates the cache according to current offset/range + appended CSV lines. */
void				tracker_stats_live_tick(void);

/* Returns a pointer to the cached stats (or NULL if not ready). */
const t_hunt_stats	*tracker_stats_live_get(void);

/* Range support (Sessions picker). */
int				tracker_stats_live_is_range(void);
void				tracker_stats_live_get_range(long *out_start, long *out_end_raw,
									long *out_end_resolved);
long				tracker_stats_live_get_offset(void);

/* Bootstrap on startup: run one tick immediately. */
void				tracker_stats_live_bootstrap(void);

#endif
