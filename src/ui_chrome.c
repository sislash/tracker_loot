#include "ui_chrome.h"


t_rect	ui_draw_chrome_ex(t_window *w, t_ui_state *ui,
		const char *breadcrumb, const char *status_left,
		const char *footer_hint, int desired_sidebar_w)
{
	t_ui_layout	ly;
	unsigned int	bg;

	ui_calc_layout_ex(w, &ly, desired_sidebar_w);
	bg = ui->theme->bg;
	window_clear(w, bg);

	/* Sidebar / Topbar / Footer surfaces */
	ui_draw_panel(w, ly.sidebar, ui->theme->surface, ui->theme->border);
	ui_draw_panel(w, ly.topbar, ui->theme->surface, ui->theme->border);
	ui_draw_panel(w, ly.footer, ui->theme->surface, ui->theme->border);

	/* Topbar content */
	{
		int ty = ly.topbar.y + (ly.topbar.h / 2 - 6);
		if (breadcrumb)
			ui_draw_text(w, ly.topbar.x + UI_PAD, ty, breadcrumb,
				ui->theme->text);
		if (status_left)
			ui_draw_text(w, ly.topbar.x + (ly.topbar.w / 2), ty,
				status_left, ui->theme->text2);
	}

	/* Footer hints */
	if (footer_hint)
		ui_draw_text(w, ly.footer.x + UI_PAD, ly.footer.y + 20, footer_hint,
			ui->theme->text2);
	return (ly.content);
}

t_rect	ui_draw_chrome(t_window *w, t_ui_state *ui,
		const char *breadcrumb, const char *status_left,
		const char *footer_hint)
{
	return (ui_draw_chrome_ex(w, ui, breadcrumb, status_left, footer_hint, 0));
}
