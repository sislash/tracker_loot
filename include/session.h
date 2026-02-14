#ifndef SESSION_H
# define SESSION_H

/*
** Session: gestion d'un offset/point de depart pour les stats (hunt session).
*/

long	session_load_offset(const char *path);
int		session_save_offset(const char *path, long offset);

/*
** Optional: session "range" (start,end) in data-line indices (0-based, header ignored).
** If end is -1, it means "until EOF".
** File format: "<start> <end>\n".
*/
int		session_load_range(const char *path, long *out_start, long *out_end);
int		session_save_range(const char *path, long start, long end);
int		session_clear_range(const char *path);

/*
** Compte le nombre de lignes de DONNEES dans un CSV (ignore l'en-tete si
** presente). Retourne 0 si fichier absent.
*/
long	session_count_data_lines(const char *csv_path);

#endif
