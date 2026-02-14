#ifndef MOB_SELECTED_H
# define MOB_SELECTED_H

# include <stddef.h>

/*
 * mob_selected
 * ------------
 * Helper pour memoriser le "mob cible" de la session.
 * Stockage: fichier texte (1 ligne) => portable Linux/Windows.
 */

int	mob_selected_load(const char *path, char *out, size_t outsz);
int	mob_selected_save(const char *path, const char *name);
int	mob_selected_clear(const char *path);

/* Nettoie le texte (trim + suppression controles + virgules). */
void	mob_selected_sanitize(char *s);

#endif
