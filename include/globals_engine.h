#ifndef GLOBALS_ENGINE_H
# define GLOBALS_ENGINE_H

# include <stdatomic.h>

int	globals_run_live(const char *chatlog_path, const char *csv_path,
				 atomic_int *stop_flag);
int	globals_run_replay(const char *chatlog_path, const char *csv_path,
				 atomic_int *stop_flag);

#endif
