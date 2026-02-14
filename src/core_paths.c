/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   core_paths.c                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: login <login@student.42.fr>                 +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/01/31 00:00:00 by login             #+#    #+#             */
/*   Updated: 2026/02/10 00:00:00 by you              ###   ###########       */
/*                                                                            */
/* ************************************************************************** */

#include "core_paths.h"
#include "fs_utils.h"

#include <string.h>
#include <stdio.h>

static int	g_inited = 0;

/* Root directory used to store config + logs (portable, click-to-run). */
static char	g_root[1024] = ".";

/* Prebuilt absolute paths (thread-safe: immutable after init). */
static char	g_logs_dir[1024] = "logs";
static char	g_hunt_csv[1024] = TM_FILE_HUNT_CSV;
static char	g_session_offset[1024] = TM_FILE_SESSION_OFFSET;
static char	g_session_range[1024] = TM_FILE_SESSION_RANGE;
static char	g_weapon_selected[1024] = TM_FILE_WEAPON_SELECTED;
static char	g_mob_selected[1024] = TM_FILE_MOB_SELECTED;
static char	g_armes_ini[1024] = TM_FILE_ARMES_INI;
static char	g_options_cfg[1024] = TM_FILE_OPTIONS_CFG;
static char	g_sessions_stats_csv[1024] = TM_FILE_SESSIONS_STATS_CSV;
static char	g_globals_csv[1024] = TM_FILE_GLOBALS_CSV;
static char	g_markup_ini[1024] = TM_FILE_MARKUP_INI;
static char	g_parser_debug_log[1024] = "logs/parser_debug.log";

static int	build_paths_from_root(const char *root)
{
	char	tmp[1024];

	if (!root || !*root)
		root = ".";
	strncpy(g_root, root, sizeof(g_root) - 1);
	g_root[sizeof(g_root) - 1] = '\0';

	if (fs_path_join(g_logs_dir, sizeof(g_logs_dir), g_root, TM_DIR_LOGS) != 0)
		return (-1);
	if (fs_path_join(g_hunt_csv, sizeof(g_hunt_csv), g_root, TM_FILE_HUNT_CSV) != 0)
		return (-1);
	if (fs_path_join(g_session_offset, sizeof(g_session_offset), g_root, TM_FILE_SESSION_OFFSET) != 0)
		return (-1);
	if (fs_path_join(g_session_range, sizeof(g_session_range), g_root, TM_FILE_SESSION_RANGE) != 0)
		return (-1);
	if (fs_path_join(g_weapon_selected, sizeof(g_weapon_selected), g_root, TM_FILE_WEAPON_SELECTED) != 0)
		return (-1);
	if (fs_path_join(g_mob_selected, sizeof(g_mob_selected), g_root, TM_FILE_MOB_SELECTED) != 0)
		return (-1);
	if (fs_path_join(g_armes_ini, sizeof(g_armes_ini), g_root, TM_FILE_ARMES_INI) != 0)
		return (-1);
	if (fs_path_join(g_options_cfg, sizeof(g_options_cfg), g_root, TM_FILE_OPTIONS_CFG) != 0)
		return (-1);
	if (fs_path_join(g_sessions_stats_csv, sizeof(g_sessions_stats_csv), g_root, TM_FILE_SESSIONS_STATS_CSV) != 0)
		return (-1);
	if (fs_path_join(g_globals_csv, sizeof(g_globals_csv), g_root, TM_FILE_GLOBALS_CSV) != 0)
		return (-1);
	if (fs_path_join(g_markup_ini, sizeof(g_markup_ini), g_root, TM_FILE_MARKUP_INI) != 0)
		return (-1);
	if (fs_path_join(tmp, sizeof(tmp), TM_DIR_LOGS, "parser_debug.log") != 0)
		return (-1);
	if (fs_path_join(g_parser_debug_log, sizeof(g_parser_debug_log), g_root, tmp) != 0)
		return (-1);
	return (0);
}

static int	has_required_files_in(const char *dir)
{
	char	p1[1024];
	char	p2[1024];

	if (!dir || !*dir)
		return (0);
	if (fs_path_join(p1, sizeof(p1), dir, TM_FILE_ARMES_INI) != 0)
		return (0);
	if (fs_path_join(p2, sizeof(p2), dir, TM_FILE_MARKUP_INI) != 0)
		return (0);
	return (fs_file_exists(p1) && fs_file_exists(p2));
}

/*
 * Choose a stable app root so user can double-click the .exe:
 * - Prefer executable directory if it contains armes.ini + markup.ini.
 * - Else, try parent directory (repo layout: bin/..).
 * - Else, fallback to exe dir.
 * Also chdir() to this root to keep legacy relative fopen("logs/...") working.
 */
int	tm_paths_init(const char *argv0)
{
	char	exe_dir[1024];
	char	parent[1024];
	const char	*root = ".";

	if (g_inited)
		return (0);
	if (fs_get_exe_dir(exe_dir, sizeof(exe_dir), argv0) != 0 || exe_dir[0] == '\0')
		strncpy(exe_dir, ".", sizeof(exe_dir) - 1);

	root = exe_dir;
	if (!has_required_files_in(root))
	{
		if (fs_path_parent(parent, sizeof(parent), exe_dir) == 0
			&& parent[0] && has_required_files_in(parent))
			root = parent;
	}
	if (build_paths_from_root(root) != 0)
		return (-1);
	/* Ensure logs dir exists early so UI/threads can write immediately. */
	tm_ensure_logs_dir();
	/* Keep old relative opens valid (parser_debug.log, etc.). */
	fs_chdir(g_root);
	g_inited = 1;
	return (0);
}

const char	*tm_app_root(void)
{
	return (g_root);
}

const char	*tm_path_logs_dir(void)
{
	return (g_logs_dir);
}

const char	*tm_path_parser_debug_log(void)
{
	return (g_parser_debug_log);
}

const char	*tm_path_hunt_csv(void)
{
	return (g_hunt_csv);
}

const char	*tm_path_options_cfg(void)
{
	return (g_options_cfg);
}

const char	*tm_path_sessions_stats_csv(void)
{
	return (g_sessions_stats_csv);
}

const char	*tm_path_session_offset(void)
{
	return (g_session_offset);
}

const char	*tm_path_session_range(void)
{
	return (g_session_range);
}

const char	*tm_path_weapon_selected(void)
{
	return (g_weapon_selected);
}

const char	*tm_path_mob_selected(void)
{
	return (g_mob_selected);
}

const char	*tm_path_armes_ini(void)
{
	return (g_armes_ini);
}

/*
 * Globals / HOF / ATH
 */

const char	*tm_path_globals_csv(void)
{
	return (g_globals_csv);
}

const char	*tm_path_markup_ini(void)
{
	return (g_markup_ini);
}

int	tm_ensure_logs_dir(void)
{
	return (fs_ensure_dir(g_logs_dir));
}
