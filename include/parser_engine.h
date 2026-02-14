#ifndef PARSER_ENGINE_H
# define PARSER_ENGINE_H

/*
** Parser engine: lit chat.log (replay ou live) et écrit TM_FILE_HUNT_CSV.
**
** Ne contient AUCUNE UI.
*/

void parser_engine_set_player_name(const char *name);


# include <stdatomic.h>

int	parser_run_replay(const char *chatlog_path, const char *csv_path,
					atomic_int *stop_flag);

int	parser_run_live(const char *chatlog_path, const char *csv_path,
					atomic_int *stop_flag);

#endif
