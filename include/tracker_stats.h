#ifndef TRACKER_STATS_H
# define TRACKER_STATS_H

# include <stddef.h>
# include "tm_money.h"

# define TM_TOP_MOBS 10
# define TM_TOP_LOOT 10
# define TM_STATS_HAS_MARKUP 1

typedef struct s_top_mob
{
	char	name[128];
	long	kills;
} 			t_top_mob;

typedef struct s_top_loot
{
	char	name[128];
	tm_money_t	tt_ped;
	tm_money_t	mu_ped;
	tm_money_t	total_mu_ped;
	long	events;
} 			t_top_loot;

typedef struct s_hunt_stats
{
	int			csv_has_header;
	long		data_lines_read;

	long		kills;
	long		shots;
	
	long		sweat_total;
	long		sweat_events;

	tm_money_t	loot_ped;
	long		loot_events;

	tm_money_t	expense_ped_logged;
	long		expense_events;

	tm_money_t	expense_ped_calc;
	tm_money_t	expense_used;
	int		expense_used_is_logged;

	tm_money_t	net_ped;

	int		has_weapon;
	char		weapon_name[128];
	char		player_name[128];
	tm_money_t	cost_shot_uPED;
	tm_money_t	ammo_shot_uPED;
	tm_money_t	decay_shot_uPED;
	tm_money_t	amp_decay_shot_uPED;
	double		markup; /* affichage seulement */

	size_t		mobs_unique;
	size_t		top_mobs_count;
	t_top_mob	top_mobs[TM_TOP_MOBS];

	/* ====================================================== */
	/* TOP LOOT (per item)                                    */
	/* ====================================================== */
	size_t		top_loot_count;
	t_top_loot	top_loot[TM_TOP_LOOT];
	
	/* ====================================================== */
	/* MARKUP (TT / MU / TT+MU)                               */
	/* Actif uniquement si TM_STATS_HAS_MARKUP est defini     */
	/* ====================================================== */
#ifdef TM_STATS_HAS_MARKUP
	tm_money_t	loot_tt_ped;
	tm_money_t	loot_mu_ped;
	tm_money_t	loot_total_mu_ped;
#endif
	
} 			t_hunt_stats;

int	tracker_stats_compute(const char *csv_path, long start_line, t_hunt_stats *out);

/*
** Compute stats on a data-line range [start_line, end_line).
** - Lines are 0-based data lines (header ignored).
** - If end_line is -1, it means "until EOF".
*/
int	tracker_stats_compute_range(const char *csv_path, long start_line,
								  long end_line, t_hunt_stats *out);

#endif
