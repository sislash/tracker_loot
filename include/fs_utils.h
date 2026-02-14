#ifndef FS_UTILS_H
# define FS_UTILS_H

# include <stddef.h>
# include <stdio.h>

/*
** Petit module utilitaire pour manipuler fichiers/dossiers.
** Portable Linux/Windows (mkdir, file size, etc.).
*/
FILE	*fs_fopen_shared_read(const char *path);
int		fs_mkdir_p_for_file(const char *filepath);
int		fs_ensure_dir(const char *dir);
long	fs_file_size(const char *path);
int		fs_file_exists(const char *path);

/* Best-effort file truncate (used for crash recovery of partial CSV lines). */
int		fs_truncate_fp(FILE *f, long new_size);

/* Paths / process working dir (portable) */
int		fs_chdir(const char *path);
int		fs_get_exe_dir(char *out, size_t outsz, const char *argv0);
int		fs_path_parent(char *out, size_t outsz, const char *path);
int		fs_path_join(char *out, size_t outsz, const char *a, const char *b);

#endif
