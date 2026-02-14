#ifndef UI_CHROME_H
#define UI_CHROME_H

#include "window.h"
#include "ui_layout.h"
#include "ui_widgets.h"

/* Draws the global app frame: background + topbar + sidebar + footer.
** Returns the content rect where screens should draw their main content.
*/
t_rect	ui_draw_chrome(t_window *w, t_ui_state *ui,
		const char *breadcrumb, const char *status_left,
		const char *footer_hint);

/* Same as ui_draw_chrome(), but allows a suggested sidebar width. */
t_rect	ui_draw_chrome_ex(t_window *w, t_ui_state *ui,
		const char *breadcrumb, const char *status_left,
		const char *footer_hint, int desired_sidebar_w);

#endif
