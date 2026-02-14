#include "markup.h"
#include <stdio.h>

int	markup_db_save(const t_markup_db *db, const char *path)
{
	FILE	*f;
	size_t	i;

	if (!db || !path)
		return (-1);
	f = fopen(path, "wb");
	if (!f)
		return (-1);

	i = 0;
	while (i < db->count)
	{
		const t_markup_rule *r = &db->items[i];
		fprintf(f, "[%s]\n", r->name);
		if (r->type == MARKUP_TT_PLUS)
			fprintf(f, "type=tt_plus\n");
		else
			fprintf(f, "type=percent\n");
		fprintf(f, "value=%.10g\n\n", r->value);
		i++;
	}
	fclose(f);
	return (0);
}
