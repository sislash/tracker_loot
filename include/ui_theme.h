#ifndef UI_THEME_H
#define UI_THEME_H

/*
** Simple theme definition for the X11 immediate-mode UI.
** Colors are 0xRRGGBB.
*/

typedef struct s_theme
{
	unsigned int	bg;
	unsigned int	surface;
	unsigned int	surface2;
	unsigned int	border;
	unsigned int	text;
	unsigned int	text2;
	unsigned int	accent;
	unsigned int	success;
	unsigned int	warn;
	unsigned int	danger;
} 	t_theme;

extern const t_theme	g_theme_dark;
extern const t_theme	g_theme_light;

/* Utility: blend two 0xRRGGBB colors. t in [0..255]. */
unsigned int	ui_color_lerp(unsigned int a, unsigned int b, unsigned int t);

#endif
