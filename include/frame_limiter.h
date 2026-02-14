/* ************************************************************************** */
/*                                                                            */
/*   frame_limiter.h                                                          */
/*                                                                            */
/*   Small FPS limiter utility.                                                */
/*                                                                            */
/* ************************************************************************** */

#ifndef FRAME_LIMITER_H
# define FRAME_LIMITER_H

# include <stdint.h>

typedef struct s_frame_limiter
{
	int		target_ms;
	uint64_t	frame_start_ms;
}   t_frame_limiter;

/* Start of a frame: records current time. */
void	fl_begin(t_frame_limiter *fl);

/* End of a frame: sleeps the remaining time (if any). */
void	fl_end_sleep(t_frame_limiter *fl);

#endif
