/* sweat_option.h */
#ifndef SWEAT_OPTION_H
# define SWEAT_OPTION_H

int     sweat_option_load(const char *path, int *enabled);
int     sweat_option_save(const char *path, int enabled);

#endif
