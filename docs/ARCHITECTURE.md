# Architecture

## Objectif
`tracker_loot` lit le `chat.log` d’Entropia Universe (LIVE/REPLAY) et produit une **vérité CSV** comptable (PED/PEC) pour analyser ROI/Return.

## Dossiers
- `src/` : implémentations
- `include/` : headers
- `logs/` : sorties runtime (ignorées par Git)
- `config/` : exemples de configuration

## Modules (aperçu)
- Parsing: `parser_engine.*`, `parser_thread.*`
- Session: `session.*`, `session_export.*`, `hunt_series*.*`
- UI: `ui_*.*`, `overlay.*`, `window_*.*`, `menu_*.*`
- CSV: `csv.*`, `hunt_csv.*`, `csv_index.*`
- Utilitaires: `tm_money.*`, `tm_string.*`, `fs_utils.*`, `core_paths.*`

## Portabilité
- Linux: X11
- Windows: Win32 API
- Build via `make` + cross-compile MinGW-w64.
