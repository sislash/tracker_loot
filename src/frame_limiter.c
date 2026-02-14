/* ************************************************************************** */
/*                                                                            */
/*   frame_limiter.c                                                          */
/*                                                                            */
/* ************************************************************************** */

#include "frame_limiter.h"
#include "utils.h" /* ft_time_ms / ft_sleep_ms */

void	fl_begin(t_frame_limiter *fl)
{
	if (!fl)
		return ;
	fl->frame_start_ms = ft_time_ms();
}

void	fl_end_sleep(t_frame_limiter *fl)
{
	uint64_t	elapsed;
	int		sleep_ms;

	if (!fl)
		return ;
	elapsed = ft_time_ms() - fl->frame_start_ms;
	sleep_ms = fl->target_ms - (int)elapsed;
	if (sleep_ms > 0)
		ft_sleep_ms(sleep_ms);
}
