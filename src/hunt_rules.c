/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   hunt_rules.c                                       :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: login <login@student.42.fr>                +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/01/31 00:00:00 by login             #+#    #+#             */
/*   Updated: 2026/01/31 00:00:00 by login            ###   ###########       */
/*                                                                            */
/* ************************************************************************** */

#include "hunt_rules.h"
#include "eu_economy.h"
#include "tm_string.h"
#include "tm_money.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_LINE 2048
/*
 * Loot packets in Entropia arrive "at the same time" for a given kill
 * (often multiple lines for different items).
 * If another loot arrives 1 second later, it must be treated as a NEW kill
 * => NEW loot packet.
 *
 * Chat log timestamps are second-granularity, so grouping by the same second
 * is the safest rule.
 */
#define LOOT_GROUP_WINDOW_SEC 0
#define COMBAT_TO_LOOT_WINDOW_SEC 60
static int			g_has_pending = 0;
static t_hunt_event	g_pending;
static char			g_player_name[128];
static char			g_last_kill_ts[32];
static time_t		g_last_loot_t = 0;
static time_t		g_last_combat_t = 0;
static time_t		g_last_explicit_kill_t = 0;

/* If a proper "You killed ..." line happened very recently, do NOT infer a kill
 * from a loot packet (prevents KILL duplicates / UNKNOWN kills).
 */
#define KILL_TO_LOOT_GRACE_SEC 10

static void	now_timestamp(char *buf, size_t bufsz)
{
	time_t		t;
	struct tm	*lt;
	
	if (!buf || bufsz == 0)
		return;
	t = time(NULL);
	lt = localtime(&t);
	if (!lt)
	{
		buf[0] = '\0';
		return;
	}
	strftime(buf, bufsz, "%Y-%m-%d %H:%M:%S", lt);
}

static int	is_2digits(const char *p)
{
	return (p && isdigit((unsigned char)p[0]) && isdigit((unsigned char)p[1]));
}

static int	is_4digits(const char *p)
{
	if (!p)
		return (0);
	return (isdigit((unsigned char)p[0]) && isdigit((unsigned char)p[1])
	&& isdigit((unsigned char)p[2]) && isdigit((unsigned char)p[3]));
}

static int	extract_chatlog_timestamp(const char *line, char *out, size_t outsz)
{
	if (!line || !out || outsz < 20)
		return (0);
	if (strlen(line) < 19)
		return (0);
	if (!is_4digits(line) || line[4] != '-' || !is_2digits(line + 5))
		return (0);
	if (line[7] != '-' || !is_2digits(line + 8) || line[10] != ' ')
		return (0);
	if (!is_2digits(line + 11) || line[13] != ':' || !is_2digits(line + 14))
		return (0);
	if (line[16] != ':' || !is_2digits(line + 17))
		return (0);
	snprintf(out, outsz, "%.19s", line);
	return (1);
}

static time_t	parse_ts_to_time(const char *ts)
{
	struct tm	tm;
	int			yr;
	int			mo;
	int			da;
	int			ho;
	int			mi;
	int			se;
	
	if (!ts || strlen(ts) < 19)
		return ((time_t)0);
	memset(&tm, 0, sizeof(tm));
	if (sscanf(ts, "%d-%d-%d %d:%d:%d", &yr, &mo, &da, &ho, &mi, &se) != 6)
		return ((time_t)0);
	tm.tm_year = yr - 1900;
	tm.tm_mon = mo - 1;
	tm.tm_mday = da;
	tm.tm_hour = ho;
	tm.tm_min = mi;
	tm.tm_sec = se;
	tm.tm_isdst = -1;
	return (mktime(&tm));
}

static void	trim_final_dot(char *s)
{
	size_t	n;
	
	if (!s)
		return;
	n = strlen(s);
	while (n && (s[n - 1] == ' ' || s[n - 1] == '\t'))
		s[--n] = '\0';
	if (n && s[n - 1] == '.')
		s[n - 1] = '\0';
}

static int	contains_any(const char *line, const char *const *pats, size_t n)
{
	size_t	i;
	
	if (!line)
		return (0);
	i = 0;
	while (i < n)
	{
		if (pats[i] && strstr(line, pats[i]))
			return (1);
		i++;
	}
	return (0);
}

static const char	*find_after_token(const char *line, const char *const *tok,
									  size_t n)
{
	size_t		i;
	const char	*p;
	
	if (!line)
		return (NULL);
	i = 0;
	while (i < n)
	{
		if (tok[i])
		{
			p = strstr(line, tok[i]);
			if (p)
				return (p + strlen(tok[i]));
		}
		i++;
	}
	return (NULL);
}

/*
 * RCE safety: NEVER parse loot values through floating point.
 * Chatlog values are decimal PED strings (often with ',' as decimal separator).
 * We convert them directly to fixed-point uPED (1e-4 PED) using tm_money.
 */

static void	zero_event(t_hunt_event *ev)
{
	if (!ev)
		return;
	memset(ev, 0, sizeof(*ev));
}

int	hunt_pending_pop(t_hunt_event *ev)
{
	if (!ev || !g_has_pending)
		return (0);
	*ev = g_pending;
	g_has_pending = 0;
	memset(&g_pending, 0, sizeof(g_pending));
	return (1);
}

void	hunt_rules_set_player_name(const char *name)
{
	safe_copy(g_player_name, sizeof(g_player_name), name);
}

/* -------------------------------------------------------------------------- */
/* Parsing helpers                                                            */
/* -------------------------------------------------------------------------- */

static void	init_event(const char *line_in, t_hunt_event *ev, char *line,
					   char *ts)
{
	zero_event(ev);
	safe_copy(line, MAX_LINE, line_in);
	tm_chomp_crlf(line);
	safe_copy(ev->raw, sizeof(ev->raw), line);
	ts[0] = '\0';
	if (!extract_chatlog_timestamp(line, ts, 32))
		now_timestamp(ts, 32);
	safe_copy(ev->ts, sizeof(ev->ts), ts);
}

static void	push_pending_kill(const char *line, const char *ts)
{
	zero_event(&g_pending);
	safe_copy(g_pending.ts, sizeof(g_pending.ts), ts);
	safe_copy(g_pending.type, sizeof(g_pending.type), "KILL");
	safe_copy(g_pending.name, sizeof(g_pending.name), "UNKNOWN");
	safe_copy(g_pending.raw, sizeof(g_pending.raw), line);
	g_has_pending = 1;
	snprintf(g_last_kill_ts, sizeof(g_last_kill_ts), "%s", ts);
}

static int	should_make_kill(time_t t)
{
	if (!g_last_combat_t)
		return (0);
	return ((t - g_last_combat_t) <= COMBAT_TO_LOOT_WINDOW_SEC);
}

static int	is_same_loot_group(time_t t)
{
	if (!g_last_loot_t)
		return (0);
	return ((t - g_last_loot_t) <= LOOT_GROUP_WINDOW_SEC);
}

static int	legacy_pending_kill(const char *line, const char *ts)
{
	if (ts[0] && strcmp(g_last_kill_ts, ts) != 0)
	{
		push_pending_kill(line, ts);
		return (1);
	}
	return (0);
}

static int	handle_loot_time(time_t t, const char *line, const char *ts)
{
	if (g_last_explicit_kill_t)
	{
		time_t	dt;

		dt = t - g_last_explicit_kill_t;
		if (dt < 0)
			dt = -dt;
		if (dt <= KILL_TO_LOOT_GRACE_SEC)
		{
			g_last_loot_t = t;
			return (0);
		}
	}
	if (is_same_loot_group(t))
	{
		g_last_loot_t = t;
		return (0);
	}
	g_last_loot_t = t;
	if (should_make_kill(t))
	{
		push_pending_kill(line, ts);
		return (1);
	}
	return (0);
}

static int	maybe_grouped_kill(const char *line, const char *ts)
{
	time_t	t;
	
	t = parse_ts_to_time(ts);
	if (t)
		return (handle_loot_time(t, line, ts));
	return (legacy_pending_kill(line, ts));
}

static void	update_combat_time(const char *ts)
{
	time_t	t;
	
	t = parse_ts_to_time(ts);
	if (t)
		g_last_combat_t = t;
}

static int	is_shot_strict(const char *line)
{
	static const char *const	pats[] = {
		"You inflicted ",
		"Vous avez infligé ",
		"The target Dodged your attack",
		"The target Evaded your attack",
		"La cible a esquivé votre attaque",
		"La cible a évité votre attaque",
		"You missed",
		"Vous avez raté",
		"Vous manquez votre cible"
	};
	
	return (contains_any(line, pats, sizeof(pats) / sizeof(pats[0])));
}

static int	is_shot_critical(const char *line)
{
	if (!(strstr(line, "Critical hit") || strstr(line, "Coup critique")))
		return (0);
	if (strstr(line, "You inflicted "))
		return (1);
	if (strstr(line, "Vous avez infligé "))
		return (1);
	return (0);
}

static int	parse_shot_line(const char *line, const char *ts, t_hunt_event *ev)
{
	if (!is_shot_strict(line) && !is_shot_critical(line))
		return (0);
	update_combat_time(ts);
	safe_copy(ev->type, sizeof(ev->type), "SHOT");
	safe_copy(ev->qty, sizeof(ev->qty), "1");
	return (1);
}

static const char	*received_start(const char *line)
{
	static const char *const	tok[] = {
		"You received ",
		"Vous avez reçu "
	};
	
	return (find_after_token(line, tok, sizeof(tok) / sizeof(tok[0])));
}

static const char	*find_value_token(const char *start)
{
	static const char *const	val[] = {
		" Value:",
		" Value :",
		" Valeur:",
		" Valeur :"
	};
	size_t					i;
	const char				*p;
	
	i = 0;
	while (i < sizeof(val) / sizeof(val[0]))
	{
		p = strstr(start, val[i]);
		if (p)
			return (p);
		i++;
	}
	return (NULL);
}

static int	extract_item_name(const char *start, const char *xpos,
							  char *item, size_t itemsz)
{
	size_t	len;
	
	len = (size_t)(xpos - start);
	while (len > 0 && isspace((unsigned char)start[len - 1]))
		len--;
	if (len >= itemsz)
		len = itemsz - 1;
	memcpy(item, start, len);
	item[len] = '\0';
	return (len > 0);
}

static const char	*skip_value_token(const char *start, const char *valp)
{
	static const char *const	val[] = {
		" Value:",
		" Value :",
		" Valeur:",
		" Valeur :"
	};
	size_t					i;
	const char				*p;
	
	i = 0;
	while (i < sizeof(val) / sizeof(val[0]))
	{
		p = strstr(start, val[i]);
		if (p == valp)
			return (p + strlen(val[i]));
		i++;
	}
	return (valp);
}

static int	set_loot_event(t_hunt_event *ev, const char *item,
						   int qty, tm_money_t value_uPED)
{
	char	qtybuf[32];
	char	pedbuf[64];
	
	snprintf(qtybuf, sizeof(qtybuf), "%d", qty);
	tm_money_format_ped4(pedbuf, sizeof(pedbuf), value_uPED);
	safe_copy(ev->type, sizeof(ev->type), "LOOT_ITEM");
	safe_copy(ev->name, sizeof(ev->name), item);
	safe_copy(ev->qty, sizeof(ev->qty), qtybuf);
	safe_copy(ev->value, sizeof(ev->value), pedbuf);
	return (0);
}

static int	set_sweat_event(t_hunt_event *ev, int qty)
{
	char	qtybuf[32];
	char	pedbuf[64];
	tm_money_t	value_uPED;
	
	snprintf(qtybuf, sizeof(qtybuf), "%d", qty);
	value_uPED = (tm_money_t)qty * (tm_money_t)EU_SWEAT_uPED_PER_BOTTLE;
	tm_money_format_ped4(pedbuf, sizeof(pedbuf), value_uPED);
	safe_copy(ev->type, sizeof(ev->type), "SWEAT");
	safe_copy(ev->name, sizeof(ev->name), "Vibrant Sweat");
	safe_copy(ev->qty, sizeof(ev->qty), qtybuf);
	safe_copy(ev->value, sizeof(ev->value), pedbuf);
	return (0);
}

static int	get_loot_fields(const char *start, char *item, size_t itemsz,
						int *qty, tm_money_t *value_uPED)
{
	const char	*xpos;
	const char	*valp;
	const char	*vstart;
	tm_money_t	v;
	
	xpos = strstr(start, " x (");
	valp = find_value_token(start);
	if (!xpos || !valp || xpos >= valp)
		return (0);
	if (!extract_item_name(start, xpos, item, itemsz))
		return (0);
	*qty = atoi(xpos + (int)strlen(" x ("));
	vstart = skip_value_token(start, valp);
	while (*vstart && isspace((unsigned char)*vstart))
		vstart++;
	v = 0;
	if (!tm_money_parse_ped(vstart, &v))
		return (0);
	*value_uPED = v;
	return (1);
}

static int	parse_received_loot(const char *line, const char *ts,
								t_hunt_event *ev)
{
	const char	*start;
	char		item[256];
	int			qty;
	tm_money_t	value_uPED;
	
	start = received_start(line);
	if (!start)
		return (0);
	if (!get_loot_fields(start, item, sizeof(item), &qty, &value_uPED))
		return (-1);
	if (strcmp(item, "Vibrant Sweat") == 0)
		return (set_sweat_event(ev, qty) + 2);
	set_loot_event(ev, item, qty, value_uPED);
	(void)ts;
	return (1);
}

static int	set_received_other(t_hunt_event *ev, const char *start)
{
	char	tmp[512];
	
	safe_copy(tmp, sizeof(tmp), start);
	trim_final_dot(tmp);
	safe_copy(ev->type, sizeof(ev->type), "RECEIVED_OTHER");
	safe_copy(ev->name, sizeof(ev->name), tmp);
	return (0);
}

static int	parse_received_line(const char *line, const char *ts,
								t_hunt_event *ev)
{
	const char	*start;
	int			ok;
	
	start = received_start(line);
	if (!start)
		return (0);
	ok = parse_received_loot(line, ts, ev);
	if (ok == 1)
		return (1);
	if (ok == 2)
		return (2);
	set_received_other(ev, start);
	return (1);
}

static int	parse_kill_line(const char *line, const char *ts, t_hunt_event *ev)
{
	static const char *const	tok[] = {
		"You killed ",
		"Vous avez tué ",
		"Vous tuez "
	};
	const char					*start;
	char						mob[256];
	
	start = find_after_token(line, tok, sizeof(tok) / sizeof(tok[0]));
	if (!start)
		return (0);
	safe_copy(mob, sizeof(mob), start);
	trim_final_dot(mob);
	if (ts[0] && strcmp(g_last_kill_ts, ts) != 0)
	{
		time_t	kt;

		kt = parse_ts_to_time(ts);
		if (kt)
			g_last_explicit_kill_t = kt;
		safe_copy(ev->type, sizeof(ev->type), "KILL");
		safe_copy(ev->name, sizeof(ev->name), mob);
		snprintf(g_last_kill_ts, sizeof(g_last_kill_ts), "%s", ts);
		return (1);
	}
	return (-1);
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */

int	hunt_should_ignore_line(const char *line)
{
	if (!line || !*line)
		return (1);
	if (strstr(line, "[Rookie]") != NULL)
		return (1);
	if (strstr(line, "[Débutant]") != NULL)
		return (1);
	if (strstr(line, "[#"))
		return (1);
	if (strstr(line, "[ROCKtropia") != NULL)
		return (1);
	if (strstr(line, "[HyperStim") != NULL)
		return (1);
	return (0);
}

int	hunt_parse_line(const char *line_in, t_hunt_event *ev)
{
	char	line[MAX_LINE];
	char	ts[32];
	int		ret;
	int		sweat_ret;
	
	if (!line_in || !ev)
		return (-1);
	init_event(line_in, ev, line, ts);
	if (parse_shot_line(line, ts, ev))
		return (0);
	sweat_ret = parse_received_line(line, ts, ev);
	if (sweat_ret)
	{
		if (sweat_ret == 2)
			return (0);
		ret = maybe_grouped_kill(line, ts);
		return (ret);
	}
	ret = parse_kill_line(line, ts, ev);
	if (ret > 0)
		return (0);
	return (-1);
}