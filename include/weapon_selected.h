#ifndef WEAPON_SELECTED_H
# define WEAPON_SELECTED_H

# include <stddef.h>

int	weapon_selected_load(const char *path, char *out, size_t outsz);
int	weapon_selected_save(const char *path, const char *name);

#endif
