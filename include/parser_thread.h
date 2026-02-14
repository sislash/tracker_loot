#ifndef PARSER_THREAD_H
# define PARSER_THREAD_H

/*
** Wrapper thread portable (Linux pthread / Windows CreateThread).
**
** API simple pour les menus.
*/

int	parser_thread_start_live(void);
int	parser_thread_start_replay(void);
void	parser_thread_stop(void);
int	parser_thread_is_running(void);

#endif
