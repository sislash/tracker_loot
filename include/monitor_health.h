#ifndef MONITOR_HEALTH_H
#define MONITOR_HEALTH_H

#include <stdint.h>

/* Health levels for operator trust (RCE safe). */
typedef enum e_health_level
{
	HEALTH_OK = 0,
	HEALTH_WARN = 1,
	HEALTH_FAIL = 2
} 	HealthLevel;

#define HEALTH_ERR_RING 10
#define HEALTH_ERR_MSG  160

typedef struct s_health_error
{
	HealthLevel	level;
	uint64_t	ts_ms;
	int		err_no;
	int		ferror_code;
	char		msg[HEALTH_ERR_MSG];
} 	t_health_error;

typedef struct s_monitor_health
{
	uint64_t	now_ms;
	uint64_t	last_event_ms;
	uint64_t	last_flush_ms;

	/* Files */
	long		chat_size;
	long		chat_pos;
	long		csv_size;
	int		rotation_detected;

	/* Counters */
	int		io_errors;
	int		parse_errors;
	int		last_errno;
	int		last_ferror;

	/* Derived / operator view */
	int		events_per_sec_x100; /* avg 10s * 100 */
	long long	lag_ms;            /* now - last_event */
	HealthLevel	io_level;
	HealthLevel	lag_level;

	/* Ring buffer */
	t_health_error	errors[HEALTH_ERR_RING];
	int		errors_count;
} 	MonitorHealth;

/* Called when opening a session / (re)starting parser. */
void	monitor_health_reset(const char *chat_path, const char *csv_path);

/* Parser thread hooks */
void	monitor_health_on_event(uint64_t now_ms);
void	monitor_health_on_flush(uint64_t now_ms, int ok, int err_no, int ferror_code);
void	monitor_health_on_io_error(const char *ctx, int err_no, int ferror_code);
void	monitor_health_on_parse_error(const char *ctx);
void	monitor_health_update_io(uint64_t now_ms, long chat_size, long chat_pos,
							long csv_size, int rotated);

/* UI thread: consistent snapshot (lock-free). */
void	monitor_health_snapshot(MonitorHealth *out, uint64_t now_ms);

#endif
