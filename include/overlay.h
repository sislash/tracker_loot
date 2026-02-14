#ifndef OVERLAY_H
#define OVERLAY_H

#include "window.h"
#include "tracker_stats.h"

#include <stdint.h>

/* Toggleable always-on-top overlay window. */
void	overlay_toggle(void);
int		overlay_is_enabled(void);
void	overlay_tick(const t_hunt_stats *s, unsigned long long elapsed_ms);

/* Session clock used by overlay (shared across screens).
 * - overlay_set_session_start_ms(): one-way push into overlay.
 * - overlay_sync_session_clock(): two-way sync (lets overlay buttons drive the app clock).
 */
void		overlay_set_session_start_ms(uint64_t ms);
uint64_t	overlay_get_session_start_ms(void);
void		overlay_sync_session_clock(uint64_t *io_session_start_ms);

/* Convenience tick: keeps overlay updating even inside modal screens. */
void	overlay_tick_auto_hunt(void);

#endif
