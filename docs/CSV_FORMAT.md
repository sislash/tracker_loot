# Format CSV (vérité de session)

## Principes
- Le CSV est la **source de vérité** : toute reconstruction de session doit pouvoir se faire depuis `logs/*.csv`.
- Les montants doivent éviter les erreurs d’arrondi (PED/PEC) : types stricts, conversions explicites.

## Fichiers
- `logs/hunt_log.csv` : événements chasse (hits/kills/loot…)
- `logs/hunt_session.offset` : offset logique de session (début/fin)
- `logs/globals.csv` : événements globals
- `logs/sessions_stats.csv` : résumés exportés (Stop+Export)

## Évolutions
Si une colonne est ajoutée:
- Ajouter en fin de ligne si possible.
- Garder une valeur par défaut si colonne absente (rétrocompatibilité).
