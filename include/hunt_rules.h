#ifndef HUNT_RULES_H
# define HUNT_RULES_H

# include <stddef.h>

/*
** Hunt rules = toutes les règles de parsing (patterns).
**
** Ici, tu modifies les chaînes/patterns, sans toucher au tail/replay.
*/

typedef struct s_hunt_event
{
	char	ts[32];
	char	type[32];
	char	name[256];
	char	qty[32];
	char	value[64];
	char	raw[1024];
} t_hunt_event;

void hunt_rules_set_player_name(const char *name);


int	hunt_should_ignore_line(const char *line);

/*
** hunt_parse_line():
**  - remplit 'ev' avec le 1er évènement extrait de 'line'
**  - retourne:
**      0 : 1 évènement prêt (ev)
**      1 : 1 évènement prêt (ev) + un évènement "pending" à récupérer
**     -1 : ligne non reconnue / erreur
*/
int	hunt_parse_line(const char *line, t_hunt_event *ev);

/*
** Permet de récupérer un évènement supplémentaire produit par hunt_parse_line()
** (ex: LOOT_ITEM + KILL dédupliqué).
** Retourne 1 si un évènement a été copié dans 'ev', sinon 0.
*/
int	hunt_pending_pop(t_hunt_event *ev);

#endif
