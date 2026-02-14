#ifndef GLOBALS_THREAD_H
# define GLOBALS_THREAD_H

int		globals_thread_start_live(void);
int		globals_thread_start_replay(void);
void	globals_thread_stop(void);
int		globals_thread_is_running(void);

#endif
