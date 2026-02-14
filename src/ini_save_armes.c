#include "config_arme.h"
#include "tm_money.h"
#include <stdio.h>

static void	write_double_kv(FILE *f, const char *k, double v)
{
	if (!f || !k)
		return;
	/* Keep a predictable decimal separator */
	fprintf(f, "%s=%.10g\n", k, v);
}

static void	write_money_kv(FILE *f, const char *k, tm_money_t v)
{
	char buf[64];
	if (!f || !k)
		return;
	tm_money_format_ped4(buf, sizeof(buf), v);
	fprintf(f, "%s=%s\n", k, buf);
}

static void	write_mu_kv(FILE *f, const char *k, int64_t mu_1e4)
{
	char buf[64];
	if (!f || !k)
		return;
	tm_money_format_ped4(buf, sizeof(buf), (tm_money_t)mu_1e4);
	fprintf(f, "%s=%s\n", k, buf);
}

int	armes_db_save(const armes_db *db, const char *path)
{
	FILE	*f;
	size_t	i;

	if (!db || !path)
		return (0);
	f = fopen(path, "wb");
	if (!f)
		return (0);

	/* Optional player section */
	if (db->player_name[0])
	{
		fprintf(f, "[PLAYER]\n");
		fprintf(f, "name=%s\n\n", db->player_name);
	}

	/* Amps */
	i = 0;
	while (i < db->amps.count)
	{
		fprintf(f, "[AMP:%s]\n", db->amps.items[i].name);
		write_money_kv(f, "amp_decay_shot", db->amps.items[i].amp_decay_shot);
		write_mu_kv(f, "amp_mu", db->amps.items[i].amp_mu_1e4);
		if (db->amps.items[i].notes[0])
			fprintf(f, "notes=%s\n", db->amps.items[i].notes);
		fprintf(f, "\n");
		i++;
	}

	/* Weapons */
	i = 0;
	while (i < db->count)
	{
		const arme_stats *w = &db->items[i];
		fprintf(f, "[%s]\n", w->name);
		write_double_kv(f, "dpp", w->dpp);
		write_money_kv(f, "ammo_shot", w->ammo_shot);
		write_money_kv(f, "decay_shot", w->decay_shot);
		write_money_kv(f, "amp_decay_shot", w->amp_decay_shot);
		write_mu_kv(f, "markup", w->markup_mu_1e4);
		write_mu_kv(f, "ammo_mu", w->ammo_mu_1e4);
		write_mu_kv(f, "weapon_mu", w->weapon_mu_1e4);
		write_mu_kv(f, "amp_mu", w->amp_mu_1e4);
		if (w->amp_name[0])
			fprintf(f, "amp=%s\n", w->amp_name);
		if (w->notes[0])
			fprintf(f, "notes=%s\n", w->notes);
		fprintf(f, "\n");
		i++;
	}
	fclose(f);
	return (1);
}
