/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   hunt_series_live.h                                 :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: tracker_loot                                  +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/02/09                                #+#    #+#             */
/*   Updated: 2026/02/09                                ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef HUNT_SERIES_LIVE_H
# define HUNT_SERIES_LIVE_H

# include "hunt_series.h"

/*
 * Live series cache shared by multiple screens.
 *
 * Goal:
 *  - ne pas "re-initialiser" le graph LIVE quand on sort/re-rentre
 *  - permettre de continuer a alimenter la serie meme hors de l'ecran graphe
 *
 * Contrainte:
 *  - l'update doit etre appele depuis le thread UI (pas thread-safe).
 */

void					hunt_series_live_force_reset(void);
void					hunt_series_live_tick(void);
const t_hunt_series	*hunt_series_live_get(void);

/* View mode helpers (useful for UI labels). */
int								hunt_series_live_is_range(void);
void						hunt_series_live_get_range(long *out_start,
									long *out_end_raw,
									long *out_end_resolved);
long							hunt_series_live_get_offset(void);

/* Regression guards / debug helpers (optional for UI). */
int							hunt_series_live_has_warning(void);
const char						*hunt_series_live_warning_text(void);


/*
 * Recharge V2 au demarrage:
 * - Reconstruit le cache a partir du Hunt CSV V2 existant.
 * - Respecte l'offset de session (logs/session_offset.txt) si present.
 */
void					hunt_series_live_bootstrap(void);

#endif
