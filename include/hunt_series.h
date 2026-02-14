#ifndef HUNT_SERIES_H
# define HUNT_SERIES_H

# include <time.h>
# include <stdint.h>
# include "tm_money.h"

/*
 * Hunt time-series helper used by the LIVE graph screen.
 *
 * - We bucket events by fixed time slices (default: 60s) and keep a sliding
 *   window so the graph stays fast even on long sessions.
 * - For some UI views (ex: kills), we also keep a lightweight list of events
 *   to display one point per event.
 */

/*
 * Capacity targets (RCE-safe):
 * - Buckets: 60s buckets for ~11.4 days @ 16384
 * - Events: 262k loot packets / kills / shots (intensive hunts)
 */
# define HS_MAX_POINTS 16384
# define HS_MAX_EVENTS 262144

typedef enum e_hs_metric
{
	HS_METRIC_SHOTS = 0,
	HS_METRIC_HITS,
	HS_METRIC_KILLS,
	HS_METRIC_LOOT_PED
} 	t_hs_metric;

typedef struct s_hs_bucket
{
	int		shots;
	int		hits;
	int		kills;
	tm_money_t	loot_uPED;
	/* Logged expenses (AMMO/DECAY/REPAIR/SPEND...) if present in CSV. */
	tm_money_t	expense_uPED;
} 	t_hs_bucket;

typedef struct s_hunt_series
{
	int		initialized;
	long		start_offset;
	int		bucket_sec;
	long		file_pos;

	time_t		t0;
	time_t		last_t;

	long		shots_total;
	long		hits_total;
	long		kills_total;
	tm_money_t	loot_total_uPED;
	/* Sum of logged expenses seen in CSV (may be 0 if not logged). */
	tm_money_t	expense_total_uPED;

	long		first_bucket; /* absolute bucket index */
	int		count;        /* number of buckets stored */
	t_hs_bucket	buckets[HS_MAX_POINTS];

	/* Event list (relative seconds since t0), used for 1 point per kill, etc. */
	int		kill_ev_count;
	int		kill_ev_sec[HS_MAX_EVENTS];

	/* Hits per kill (1 point per kill). */
	int		hits_ev_count;
	int		hits_ev_sec[HS_MAX_EVENTS];
	int		hits_ev_hits[HS_MAX_EVENTS];
	long		hits_since_kill;

	/* Shots per kill (1 point per kill). Stores both shots and hits for hit-rate. */
	int		shots_ev_count;
	int		shots_ev_sec[HS_MAX_EVENTS];
	int		shots_ev_shots[HS_MAX_EVENTS];
	int		shots_ev_hits[HS_MAX_EVENTS];
	long		shots_since_kill;

	/* Loot packets (1 point per loot packet / kill) */
	int		loot_ev_count;
	int		loot_ev_sec[HS_MAX_EVENTS];
	tm_money_t	loot_ev_uPED[HS_MAX_EVENTS];
	/* How many CSV rows were aggregated into this loot packet (for UI marker ×N). */
	int		loot_ev_group_count[HS_MAX_EVENTS];
	unsigned char	loot_ev_has_kill[HS_MAX_EVENTS];
	time_t		last_loot_ev_t;
	int64_t		last_loot_ev_kill_id;

	/* Incremented when new rows are successfully integrated (UI caches). */
	uint32_t	version;
} 	t_hunt_series;

void	hunt_series_reset(t_hunt_series *s, long start_offset, int bucket_sec);

/*
 * Incrementally updates the series by reading newly appended CSV rows.
 * Returns 1 on success (even if no new data), 0 on error.
 */
int		hunt_series_update(t_hunt_series *s, const char *csv_path);

/*
 * Builds a plot array from the current buckets.
 * - last_n_buckets: 0 => all
 * - cumulative: 0 => per-bucket values, 1 => cumulative curve
 * - out_x_seconds: X positions are seconds since session start (t0)
 */
int		hunt_series_build_plot(const t_hunt_series *s,
						int last_n_buckets,
						t_hs_metric metric,
						int cumulative,
						double *out_values,
						int *out_x_seconds,
						int *out_n,
						double *out_vmax);

/*
 * Builds a KILL events plot (1 point per kill), cumulative by session.
 * - last_minutes: 0 => all, else keep only events from the last N minutes.
 */
int		hunt_series_build_kill_events(const t_hunt_series *s,
							int last_minutes,
							double *out_values,
							int *out_x_seconds,
							int *out_n,
							double *out_vmax);

/*
 * Builds a Hits-per-kill plot (1 point per kill).
 * - last_minutes: 0 => all, else keep only events from the last N minutes.
 */
int		hunt_series_build_hits_events(const t_hunt_series *s,
							int last_minutes,
							double *out_values,
							int *out_x_seconds,
							int *out_n,
							double *out_vmax);

/*
 * Builds a Shots-per-kill plot (1 point per kill).
 * - last_minutes: 0 => all, else keep only events from the last N minutes.
 */
int		hunt_series_build_shots_events(const t_hunt_series *s,
						int last_minutes,
						double *out_values,
						int *out_x_seconds,
						int *out_n,
						double *out_vmax);

/*
 * Builds a Hit-rate-per-kill plot (1 point per kill).
 * Output values are in percent [0..100].
 */
int		hunt_series_build_hit_rate_events(const t_hunt_series *s,
						int last_minutes,
						double *out_values,
						int *out_x_seconds,
						int *out_n,
						double *out_vmax);

/*
 * Builds a Loot packets plot (1 point per loot packet / kill).
 * - last_minutes: 0 => all, else keep only events from the last N minutes.
 * - cumulative: 0 => loot per packet, 1 => cumulative PED curve
 */
int		hunt_series_build_loot_events(const t_hunt_series *s,
							int last_minutes,
							int cumulative,
							double *out_values,
							int *out_x_seconds,
							int *out_n,
							double *out_vmax);

/* Same as hunt_series_build_loot_events(), but also returns the loot packet
 * aggregation size for each point (group marker ×N).
 * If out_group_counts is NULL, counts are ignored.
 */
int		hunt_series_build_loot_events_ex(const t_hunt_series *s,
						int last_minutes,
						int cumulative,
						double *out_values,
						int *out_x_seconds,
						int *out_group_counts,
						int *out_n,
						double *out_vmax);

/*
 * Builds a cumulative COST curve (PED) over time buckets.
 *
 * If logged expense events exist in CSV (AMMO/DECAY/REPAIR/SPEND...), the
 * curve uses them.
 * Otherwise, it falls back to the weapon model: shots * cost_shot_uPED.
 * If both exist, it uses the conservative "max(logged, model)" strategy.
 */
int		hunt_series_build_cost_cumulative(const t_hunt_series *s,
						int last_n_buckets,
						tm_money_t cost_shot_uPED,
						double *out_values,
						int *out_x_seconds,
						int *out_n,
						double *out_vmax);

/*
 * Builds a cumulative ROI curve (%) over time buckets:
 *   ROI = (loot_cum / cost_cum) * 100
 * The cost computation follows hunt_series_build_cost_cumulative().
 */
int		hunt_series_build_roi_cumulative(const t_hunt_series *s,
						int last_n_buckets,
						tm_money_t cost_shot_uPED,
						double *out_values,
						int *out_x_seconds,
						int *out_n,
						double *out_vmax);

double	hunt_series_elapsed_seconds(const t_hunt_series *s);

/*
 * Lightweight runtime validation (used as a regression guard).
 * Returns 1 if the struct looks consistent, 0 otherwise.
 */
int		hunt_series_sanity_check(const t_hunt_series *s);

/*
 * Rebuilds a complete series by scanning a bounded CSV range.
 *
 * - start_line/end_line are DATA-line indices (0-based, header ignored)
 * - end_line is exclusive; use -1 for "until EOF"
 * - bucket_sec: size of aggregation buckets in seconds (default 60)
 *
 * Used to display graphs when the user loads an exported session range.
 */
int		hunt_series_rebuild_range(t_hunt_series *s,
					const char *csv_path,
					long start_line,
					long end_line,
					int bucket_sec);

#endif
