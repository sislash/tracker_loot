/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   globals_thread.c                                   :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: login <login@student.42.fr>                +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/01/31 00:00:00 by login             #+#    #+#             */
/*   Updated: 2026/01/31 00:00:00 by login            ###   ###########       */
/*                                                                            */
/* ************************************************************************** */


#include "globals_thread.h"
#include "globals_engine.h"
#include "core_paths.h"
#include "chatlog_path.h"
#include "tm_string.h"

#include <string.h>
#include <stdatomic.h>

static atomic_int	g_stop = 0;
static atomic_int	g_running = 0;
static int			g_mode_live = 1;


static int	run_engine(void)
{
    char	chatlog[1024];
    int		rc;
    
    tm_ensure_logs_dir();
    memset(chatlog, 0, sizeof(chatlog));
	if (chatlog_build_path(chatlog, sizeof(chatlog)) != 0)
	{
		safe_copy(chatlog, sizeof(chatlog), "chat.log");
	}
	if (g_mode_live)
		rc = globals_run_live(chatlog, tm_path_globals_csv(), &g_stop);
	else
		rc = globals_run_replay(chatlog, tm_path_globals_csv(), &g_stop);
    return (rc);
}

#ifdef _WIN32
# include <windows.h>

static HANDLE	g_th = NULL;

static DWORD WINAPI	thread_fn(LPVOID p)
{
    (void)p;
    run_engine();
	atomic_store(&g_running, 0);
    return (0);
}

static int	start_thread(void)
{
    DWORD	id;
    
    if (g_th)
        return (-1);
    atomic_store(&g_stop, 0);
    atomic_store(&g_running, 1);
    g_th = CreateThread(NULL, 0, thread_fn, NULL, 0, &id);
    if (!g_th)
    {
        atomic_store(&g_running, 0);
        return (-1);
    }
    return (0);
}


static void	join_thread(void)
{
    if (g_th)
    {
        WaitForSingleObject(g_th, INFINITE);
        CloseHandle(g_th);
        g_th = NULL;
    }
}
#else
# include <pthread.h>

static pthread_t	g_th;
static int			g_th_created = 0;

static void	*thread_fn(void *p)
{
    (void)p;
    run_engine();
	atomic_store(&g_running, 0);
    return (NULL);
}

static int	start_thread(void)
{
	if (g_th_created)
		return (-1);
	atomic_store(&g_stop, 0);
	atomic_store(&g_running, 1);
    if (pthread_create(&g_th, NULL, thread_fn, NULL) != 0)
    {
		atomic_store(&g_running, 0);
        g_th_created = 0;
        return (-1);
    }
    g_th_created = 1;
    return (0);
}

static void	join_thread(void)
{
    if (g_th_created)
    {
        pthread_join(g_th, NULL);
        g_th_created = 0;
    }
}
#endif

int	globals_thread_start_live(void)
{
	if (atomic_load(&g_running))
        return (0);
    g_mode_live = 1;
    return (start_thread());
}

int	globals_thread_start_replay(void)
{
	if (atomic_load(&g_running))
        return (0);
    g_mode_live = 0;
    return (start_thread());
}

void	globals_thread_stop(void)
{
	if (!atomic_load(&g_running))
        return ;
	atomic_store(&g_stop, 1);
    join_thread();
	atomic_store(&g_running, 0);
	atomic_store(&g_stop, 0);
}

int	globals_thread_is_running(void)
{
	return (atomic_load(&g_running));
}
