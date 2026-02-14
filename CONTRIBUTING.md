# Contribuer

Merci de vouloir contribuer 🙌

## Pré-requis
- **C99** (gcc/clang) + `make`
- Linux: `libx11-dev` (+ `pkg-config` conseillé)
- Windows: MSYS2/MinGW ou cross-compile via MinGW-w64

## Build
Linux:
```bash
make
./bin/tracker_loot
```

Windows (cross-compile):
```bash
make win
```

Désactiver `-Werror` si besoin:
```bash
make WERROR=0
```

## Style & règles
- C99, code clair, pas de dépendances lourdes.
- Les montants PED/PEC doivent rester **précis** (éviter les arrondis implicites).
- Toute évolution du CSV doit être **rétrocompatible** ou versionnée (voir `docs/CSV_FORMAT.md`).
- Pas de données personnelles dans le repo (ex: nom d’avatar, chemins locaux, logs).

## Proposer une PR
1. Fork + branche feature (`feature/...`) ou fix (`fix/...`).
2. Ajoute une description claire (contexte, reproduction, résultat attendu).
3. Si tu touches au parsing/CSV, ajoute un exemple minimal dans `docs/`.
4. Assure-toi que `make release` passe sur Linux.

## Signaler un bug
Inclure:
- OS (Linux/Windows), environnement (Wine?), version.
- Extrait anonymisé du `chat.log` (quelques lignes), **sans données perso**.
- Le CSV généré (ou un extrait) si pertinent.
