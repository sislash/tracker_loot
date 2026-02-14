#include "ui_theme.h"

/* Default dark theme (matches the UX/UI spec) */
const t_theme	g_theme_dark = {
	.bg = 0x0F1115,
	.surface = 0x171A21,
	.surface2 = 0x1E2230,
	.border = 0x2A3140,
	.text = 0xE6EAF2,
	.text2 = 0xA7B0C0,
	.accent = 0x5B8CFF,
	.success = 0x33D17A,
	.warn = 0xF5C542,
	.danger = 0xFF5B6E,
};

/* Optional light theme */
const t_theme	g_theme_light = {
	.bg = 0xF6F7FB,
	.surface = 0xFFFFFF,
	.surface2 = 0xEEF1F7,
	.border = 0xD9DDE7,
	.text = 0x12141A,
	.text2 = 0x4E5565,
	.accent = 0x2D6BFF,
	.success = 0x2ECC71,
	.warn = 0xF1C40F,
	.danger = 0xFF5B5B,
};

static unsigned int	ch(unsigned int c, int shift)
{
	return ((c >> shift) & 0xFF);
}

unsigned int	ui_color_lerp(unsigned int a, unsigned int b, unsigned int t)
{
	unsigned int	ra;
	unsigned int	ga;
	unsigned int	ba;
	unsigned int	rb;
	unsigned int	gb;
	unsigned int	bb;
	unsigned int	r;
	unsigned int	g;
	unsigned int	bl;

	ra = ch(a, 16);
	ga = ch(a, 8);
	ba = ch(a, 0);
	rb = ch(b, 16);
	gb = ch(b, 8);
	bb = ch(b, 0);
	r = (ra * (255 - t) + rb * t) / 255;
	g = (ga * (255 - t) + gb * t) / 255;
	bl = (ba * (255 - t) + bb * t) / 255;
	return ((r << 16) | (g << 8) | (bl));
}
