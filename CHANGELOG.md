# Changelog

Toutes les modifications notables de ce projet seront documentées ici.

Le format est inspiré de *Keep a Changelog* et le versioning suit *SemVer*.

## [0.1.0] - 2026-02-13
### Ajouté
- Base du tracker `tracker_loot` (C99) : lecture `chat.log` Entropia Universe.
- Mode CHASSE : LIVE/REPLAY, export CSV, offset de session.
- Estimation coûts/tir via `armes.ini` (MU séparés ammo/arme/amp) et MU loot via `markup.ini`.
- Fenêtre UI + Overlay always-on-top.
- Build Linux X11 + cross-compile Windows (MinGW-w64).
- CI GitHub Actions : build Linux + Windows.
