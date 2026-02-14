#ifndef CHATLOG_PATH_H
# define CHATLOG_PATH_H

# include <stddef.h>

/*
** Construit le chemin vers chat.log en fonction des variables d'environnement.
**
** Windows: %USERPROFILE%\Documents\Entropia Universe\chat.log
** Linux:  $HOME/... (a adapter selon ton setup; par defaut $HOME/chat.log)
*/

int	chatlog_build_path(char *out, size_t outsz);

#endif
