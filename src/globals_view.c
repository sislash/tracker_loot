#include "globals_view.h"
#include "menu_principale.h"
#include "ui_utils.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/*
 * Dashboard GLOBALS: impression uniforme dans un cadre (| ... |).
 * Les colonnes ci-dessous sont dimensionnees pour tenir dans STATUS_WIDTH.
 */
#define COL_NAME  20
#define COL_VALUE 10
#define COL_COUNT 6
#define BAR_MAX   20

static void	print_framed(const char *fmt, ...)
{
	char	buf[256];
	va_list	ap;

	if (!fmt)
	{
		print_menu_line("");
		return ;
	}
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	print_menu_line(buf);
}

static int	bar_fill(double v, double vmax)
{
	int		n;

	n = 0;
	if (vmax > 0.0)
		n = (int)((v / vmax) * (double)BAR_MAX);
	if (n < 0)
		n = 0;
	if (n > BAR_MAX)
		n = BAR_MAX;
	return (n);
}

static void	build_bar(char out[BAR_MAX + 1], double v, double vmax)
{
	int	n;

	n = bar_fill(v, vmax);
	if (n > 0)
		memset(out, '#', (size_t)n);
	if (n < BAR_MAX)
		memset(out + n, ' ', (size_t)(BAR_MAX - n));
	out[BAR_MAX] = '\0';
}

static void	print_line(size_t idx, const char *name, double sum,
					long count, double maxsum)
{
	const char	*safe;
	char		bar[BAR_MAX + 1];
	char		line[256];

	safe = (name ? name : "");
	build_bar(bar, sum, maxsum);
	snprintf(line, sizeof(line),
			"%2zu) %-*.*s %*.*f PED | %s | (%*ld)",
			idx,
			COL_NAME, COL_NAME, safe,
			COL_VALUE, 2, sum,
			bar,
			COL_COUNT, count);
	print_menu_line(line);
}

static double	max_sum(const t_globals_top *arr, size_t n)
{
	double	m;
	size_t	i;

	m = 0.0;
	i = 0;
	while (i < n)
	{
		if (arr[i].sum_ped > m)
			m = arr[i].sum_ped;
		i++;
	}
	return (m);
}

static void	print_top_block(const char *title, const t_globals_top *arr,
					size_t n)
{
	double	mx;
	size_t	i;

	print_menu_line(title ? title : "");
	print_hr();
	if (!arr || n == 0)
	{
		print_menu_line("(aucun)");
		return ;
	}
	mx = max_sum(arr, n);
	i = 0;
	while (i < n)
	{
		print_line(i + 1, arr[i].name, arr[i].sum_ped, arr[i].count, mx);
		i++;
	}
}

void	globals_view_print(const t_globals_stats *s)
{
	if (!s)
		return ;
	ui_clear_viewport();
	print_hrs();
	print_menu_line("DASHBOARD GLOBALS / HOF / ATH (MOB + CRAFT)");
	print_hrs();

	print_framed("CSV header detecte : %s", (s->csv_has_header ? "oui" : "non"));
	print_framed("Lignes data lues   : %ld", s->data_lines_read);
	print_hr();

	print_menu_line("RESUME");
	print_hr();
	print_framed("%-12s : %6ld  (%8.2f PED)", "Mob events", s->mob_events, s->mob_sum_ped);
	print_framed("%-12s : %6ld  (%8.2f PED)", "Craft events", s->craft_events, s->craft_sum_ped);
	if (s->rare_events > 0)
		print_framed("%-12s : %6ld  (%8.4f PED)", "Rare events", s->rare_events, s->rare_sum_ped);
	print_hr();

	print_top_block("TOP MOBS (somme PED)", s->top_mobs, s->top_mobs_count);
	print_hr();
	print_top_block("TOP CRAFTS (somme PED)", s->top_crafts, s->top_crafts_count);
	if (s->rare_events > 0)
	{
		print_hr();
		print_top_block("TOP RARE ITEMS (somme PED)", s->top_rares, s->top_rares_count);
	}
	print_hrs();
	print_menu_line("q = quitter dashboard");
}
