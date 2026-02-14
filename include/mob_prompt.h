#ifndef MOB_PROMPT_H
# define MOB_PROMPT_H

# include <stddef.h>
# include "window.h"

/*
 * Affiche un prompt "Nom du mob" si aucun mob n'est defini.
 * - Charge logs/mob_selected.txt
 * - Si vide: ecran modal avec input + Valider/Annuler
 * Retour:
 *   1 => mob disponible (out rempli si fourni)
 *   0 => annule
 */
int	mob_prompt_ensure(t_window *w, char *out, size_t outsz);

#endif
