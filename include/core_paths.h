#ifndef CORE_PATHS_H
# define CORE_PATHS_H

# include <stddef.h>

/*
** Tracker Modulaire - core_paths
**
** Centralise les chemins de fichiers/dossiers.
**
** Regle: aucun autre module ne doit hardcoder "logs/...".
*/

# define TM_DIR_LOGS               "logs"
# define TM_FILE_HUNT_CSV          "logs/hunt_log.csv"
# define TM_FILE_SESSION_OFFSET    "logs/hunt_session.offset"
# define TM_FILE_SESSION_RANGE     "logs/hunt_session.range"
# define TM_FILE_WEAPON_SELECTED   "logs/weapon_selected.txt"
# define TM_FILE_MOB_SELECTED      "logs/mob_selected.txt"
# define TM_FILE_ARMES_INI         "armes.ini"
# define TM_FILE_OPTIONS_CFG       "logs/options.cfg"
# define TM_FILE_SESSIONS_STATS_CSV "logs/sessions_stats.csv"
/* Globals / HOF / ATH (mob + craft) */
# define TM_FILE_GLOBALS_CSV "logs/globals.csv"
# define TM_FILE_MARKUP_INI "markup.ini"


const char	*tm_path_markup_ini(void);
const char	*tm_path_globals_csv(void);
const char  *tm_path_sessions_stats_csv(void);
const char  *tm_path_options_cfg(void);
const char	*tm_path_hunt_csv(void);
const char	*tm_path_session_offset(void);
const char	*tm_path_session_range(void);
const char	*tm_path_weapon_selected(void);
const char	*tm_path_mob_selected(void);
const char	*tm_path_armes_ini(void);

/* Initialise les chemins a partir du chemin de l'executable.
 * Permet de lancer le programme depuis n'importe quel dossier (double-clic .exe).
 */
int			 tm_paths_init(const char *argv0);
const char	*tm_app_root(void);
const char	*tm_path_logs_dir(void);
const char	*tm_path_parser_debug_log(void);

int			 tm_ensure_logs_dir(void);

#endif
