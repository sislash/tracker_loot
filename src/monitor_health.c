#include "monitor_health.h"
#include "tm_string.h" /* safe_copy */
#include "utils.h"     /* ft_time_ms */

#include <stdatomic.h>
#include <string.h>
#include <stdio.h>

#define HEALTH_SLOTS 10

typedef struct s_health_slot
{
	uint64_t	sec;
	int		count;
} 	t_health_slot;

typedef struct s_health_state
{
	_Atomic unsigned	seq;

	char	chat_path[1024];
	char	csv_path[1024];

	uint64_t	last_event_ms;
	uint64_t	last_flush_ms;

	long	chat_size;
	long	chat_pos;
	long	csv_size;
	int		rotation_detected;

	int		io_errors;
	int		parse_errors;
	int		last_errno;
	int		last_ferror;

	t_health_slot	slots[HEALTH_SLOTS];

	t_health_error	err_ring[HEALTH_ERR_RING];
	int		err_head;  /* next write */
	int		err_count; /* <= RING */
} 	t_health_state;

static t_health_state	g_h;

static void	write_begin(void)
{
	atomic_fetch_add(&g_h.seq, 1u);
}

static void	write_end(void)
{
	atomic_fetch_add(&g_h.seq, 1u);
}

static void	push_error(HealthLevel level, uint64_t ts_ms,
				const char *msg, int err_no, int ferror_code)
{
	int	idx;

	if (!msg)
		msg = "";
	idx = g_h.err_head;
	g_h.err_ring[idx].level = level;
	g_h.err_ring[idx].ts_ms = ts_ms;
	g_h.err_ring[idx].err_no = err_no;
	g_h.err_ring[idx].ferror_code = ferror_code;
	safe_copy(g_h.err_ring[idx].msg, sizeof(g_h.err_ring[idx].msg), msg);
	g_h.err_head = (g_h.err_head + 1) % HEALTH_ERR_RING;
	if (g_h.err_count < HEALTH_ERR_RING)
		g_h.err_count++;
}

static void	slot_add_event(uint64_t now_ms)
{
	uint64_t	sec;
	int		idx;

	sec = now_ms / 1000ULL;
	idx = (int)(sec % (uint64_t)HEALTH_SLOTS);
	if (g_h.slots[idx].sec != sec)
	{
		g_h.slots[idx].sec = sec;
		g_h.slots[idx].count = 0;
	}
	g_h.slots[idx].count++;
}

void	monitor_health_reset(const char *chat_path, const char *csv_path)
{
	write_begin();
	memset(&g_h.chat_path[0], 0, sizeof(g_h.chat_path));
	memset(&g_h.csv_path[0], 0, sizeof(g_h.csv_path));
	if (chat_path)
		safe_copy(g_h.chat_path, sizeof(g_h.chat_path), chat_path);
	if (csv_path)
		safe_copy(g_h.csv_path, sizeof(g_h.csv_path), csv_path);

	g_h.last_event_ms = 0;
	g_h.last_flush_ms = 0;
	g_h.chat_size = 0;
	g_h.chat_pos = 0;
	g_h.csv_size = 0;
	g_h.rotation_detected = 0;
	g_h.io_errors = 0;
	g_h.parse_errors = 0;
	g_h.last_errno = 0;
	g_h.last_ferror = 0;
	memset(g_h.slots, 0, sizeof(g_h.slots));
	memset(g_h.err_ring, 0, sizeof(g_h.err_ring));
	g_h.err_head = 0;
	g_h.err_count = 0;
	write_end();
}

void	monitor_health_on_event(uint64_t now_ms)
{
	write_begin();
	g_h.last_event_ms = now_ms;
	slot_add_event(now_ms);
	write_end();
}

void	monitor_health_on_flush(uint64_t now_ms, int ok, int err_no, int ferror_code)
{
	write_begin();
	if (ok)
	{
		g_h.last_flush_ms = now_ms;
		g_h.last_errno = 0;
		g_h.last_ferror = 0;
	}
	else
	{
		g_h.io_errors++;
		g_h.last_errno = err_no;
		g_h.last_ferror = ferror_code;
		push_error(HEALTH_FAIL, now_ms, "CSV flush failed", err_no, ferror_code);
	}
	write_end();
}

void	monitor_health_on_io_error(const char *ctx, int err_no, int ferror_code)
{
	uint64_t	now_ms;
	char		msg[HEALTH_ERR_MSG];

	now_ms = ft_time_ms();
	if (!ctx)
		ctx = "I/O";
	msg[0] = '\0';
	/* Keep message short for UI columns */
	snprintf(msg, sizeof(msg), "%s", ctx);

	write_begin();
	g_h.io_errors++;
	g_h.last_errno = err_no;
	g_h.last_ferror = ferror_code;
	push_error(HEALTH_FAIL, now_ms, msg, err_no, ferror_code);
	write_end();
}

void	monitor_health_on_parse_error(const char *ctx)
{
	uint64_t	now_ms;
	char		msg[HEALTH_ERR_MSG];

	now_ms = ft_time_ms();
	if (!ctx)
		ctx = "parse";
	snprintf(msg, sizeof(msg), "Parse: %s", ctx);

	write_begin();
	g_h.parse_errors++;
	push_error(HEALTH_WARN, now_ms, msg, 0, 0);
	write_end();
}

void	monitor_health_update_io(uint64_t now_ms, long chat_size, long chat_pos,
							long csv_size, int rotated)
{
	write_begin();
	g_h.chat_size = chat_size;
	g_h.chat_pos = chat_pos;
	g_h.csv_size = csv_size;
	if (rotated)
	{
		g_h.rotation_detected = 1;
		push_error(HEALTH_WARN, now_ms, "chat.log rotation detected", 0, 0);
	}
	write_end();
}

static HealthLevel	level_from_lag_ms(long long lag_ms)
{
	if (lag_ms < 1500)
		return (HEALTH_OK);
	if (lag_ms < 5000)
		return (HEALTH_WARN);
	return (HEALTH_FAIL);
}

void	monitor_health_snapshot(MonitorHealth *out, uint64_t now_ms)
{
	unsigned	s1;
	unsigned	s2;
	uint64_t	last_event_ms;
	uint64_t	last_flush_ms;
	long		chat_size;
	long		chat_pos;
	long		csv_size;
	int		rotation_detected;
	int		io_errors;
	int		parse_errors;
	int		last_errno;
	int		last_ferror;
	t_health_slot	slots[HEALTH_SLOTS];
	t_health_error	err_ring[HEALTH_ERR_RING];
	int		err_head;
	int		err_count;

	if (!out)
		return ;
	/* seqlock snapshot */
	while (1)
	{
		s1 = atomic_load(&g_h.seq);
		if (s1 & 1u)
			continue ;
		last_event_ms = g_h.last_event_ms;
		last_flush_ms = g_h.last_flush_ms;
		chat_size = g_h.chat_size;
		chat_pos = g_h.chat_pos;
		csv_size = g_h.csv_size;
		rotation_detected = g_h.rotation_detected;
		io_errors = g_h.io_errors;
		parse_errors = g_h.parse_errors;
		last_errno = g_h.last_errno;
		last_ferror = g_h.last_ferror;
		memcpy(slots, g_h.slots, sizeof(slots));
		memcpy(err_ring, g_h.err_ring, sizeof(err_ring));
		err_head = g_h.err_head;
		err_count = g_h.err_count;
		s2 = atomic_load(&g_h.seq);
		if (s1 == s2 && !(s2 & 1u))
			break ;
	}

	memset(out, 0, sizeof(*out));
	out->now_ms = now_ms;
	out->last_event_ms = last_event_ms;
	out->last_flush_ms = last_flush_ms;
	out->chat_size = chat_size;
	out->chat_pos = chat_pos;
	out->csv_size = csv_size;
	out->rotation_detected = rotation_detected;
	out->io_errors = io_errors;
	out->parse_errors = parse_errors;
	out->last_errno = last_errno;
	out->last_ferror = last_ferror;

	/* Derived: lag */
	if (last_event_ms == 0)
		out->lag_ms = (long long)now_ms;
	else if (now_ms >= last_event_ms)
		out->lag_ms = (long long)(now_ms - last_event_ms);
	else
		out->lag_ms = 0;
	out->lag_level = level_from_lag_ms(out->lag_ms);

	/* Derived: IO level */
	if (io_errors > 0 || last_ferror != 0)
		out->io_level = HEALTH_FAIL;
	else if (rotation_detected)
		out->io_level = HEALTH_WARN;
	else
		out->io_level = HEALTH_OK;

	/* Derived: events/sec average over last 10 seconds */
	{
		uint64_t	sec_now = now_ms / 1000ULL;
		int		sum = 0;
		for (int i = 0; i < HEALTH_SLOTS; i++)
		{
			if (slots[i].sec <= sec_now && (sec_now - slots[i].sec) < (uint64_t)HEALTH_SLOTS)
				sum += slots[i].count;
		}
		out->events_per_sec_x100 = (sum * 100) / HEALTH_SLOTS;
	}

	/* Errors: newest first */
	out->errors_count = err_count;
	for (int i = 0; i < err_count && i < HEALTH_ERR_RING; i++)
	{
		int idx = err_head - 1 - i;
		if (idx < 0)
			idx += HEALTH_ERR_RING;
		out->errors[i] = err_ring[idx];
	}
}
