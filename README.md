# tracker_loot — Entropia Universe (Fenêtre + Overlay)

`tracker_loot` est un utilitaire **portable C99** (**Linux X11** / **Windows**) qui lit le fichier **`chat.log`** d’**Entropia Universe** pour suivre une **session de chasse** et les **globals** en **temps réel (LIVE)** ou en **relecture (REPLAY)**.

- ✅ **Lecture uniquement** : lit `chat.log`, écrit des CSV dans `logs/`
- ✅ **Aucune injection / aucun hook** : uniquement basé sur les logs
- ✅ **Overlay always-on-top** : stats visibles par-dessus le jeu

---

## Sommaire
- [Fonctionnalités](#fonctionnalités)
- [UI & navigation](#ui--navigation)
- [Démarrage rapide](#démarrage-rapide)
- [Compilation](#compilation)
- [Configuration](#configuration)
  - [Chemin du chat.log](#chemin-du-chatlog)
  - [Si le programme ne trouve pas chat.log (console/terminal)](#si-le-programme-ne-trouve-pas-chatlog-consoletterminal)
  - [armes.ini (coût/tir + amplis + MU)](#armesini-coûttir--amplis--mu)
  - [markup.ini (TT → MU)](#markupini-tt--mu)
  - [Option Sweat](#option-sweat)
- [Fichiers générés](#fichiers-générés)
- [Format CSV](#format-csv)
- [Dépannage](#dépannage)
- [Architecture](#architecture)
- [Licence](#licence)

---

## Fonctionnalités

### Pages (fenêtre principale)
Interface en fenêtre (thème sombre sci-fi) avec **sidebar** :
- **Dashboard** : état des parsers + statut `chat.log`
- **Chasse** : pilotage session + stats + feed événements
- **Globals** : pilotage globals (mob/craft/rare) + tops + feed
- **Sessions** : historique des exports (résumés)
- **Configuration** : éditeurs intégrés `armes.ini` / `markup.ini`
- **Maintenance** : stop parsers + clear CSV
- **Aide** : raccourcis + chemins + debug

### CHASSE (Hunt)
- Parse `chat.log` en :
  - **LIVE** : “tail” en continu (prend les nouvelles lignes)
  - **REPLAY** : relit tout le fichier (utile pour reconstruire)
- Écrit les événements dans : `logs/hunt_log.csv`
- Stats calculées à partir d’un **offset de session** : `logs/hunt_session.offset`
  - Loot total (PED)
  - Tirs / Hits / Kills
  - Dépenses :
    - si le log contient des dépenses exploitables → utilisé
    - sinon → estimation via **coût/tir** (`armes.ini` + arme active)
  - Net / Return
  - Détails **TT / MU / TT+MU** si `markup.ini` est renseigné

➡️ **Stop+Export** :
- stoppe le parser chasse
- exporte un résumé dans `logs/sessions_stats.csv`
- met l’offset à la **fin** du CSV (prêt pour une nouvelle session)
- réinitialise la série du Graph LIVE (sur le nouvel offset)

### Graph LIVE (session)
Écran dédié accessible depuis **Chasse → “Graph LIVE”** :
- KPI en haut : **Tirs, Hits, Kills, Loot, Temps**
- Onglets :
  - **Hits / kill** (1 point par kill, comme Loot/kill)
  - **Kills (cumul)**
  - **Loot / kill**
  - **Loot cumul**
  - **Retour** (sortie)
- Range rapide : **15m / 30m / 60m / Tout**
- La série LIVE reste **“chaude” en arrière-plan** : si tu quittes l’écran puis reviens, le graphe **continue** (pas de réinitialisation visuelle).

> Note : l’onglet **Loot/min** a été retiré (simplification + cohérence).

### GLOBALS (mobs + craft + rares)
- Parser dédié LIVE/REPLAY → `logs/globals.csv`
- Stats & tops (somme PED, nombre d’events)
- Bouton **Clear CSV** pour repartir à zéro si besoin

### Overlay (always-on-top)
Fenêtre overlay optionnelle **toujours au-dessus** :
- **TT Retour**, **TT Dépense**, **% Retour**, **Mobs**, **Temps session**
- Activation :
  - bouton **Overlay ON/OFF** (topbar)
  - **touche `O`** (raccourci)
- L’overlay continue de s’actualiser même si tu changes de page (Chasse → Globals → etc.) et même pendant les écrans “modaux” (Graph LIVE, menus config).

---

## UI & navigation

### Navigation
- Sidebar : clic souris ou **Up/Down + Enter**
- **Molette** : scroll dans les feeds / pages longues
- **Esc** : fermer un sélecteur / écran modal / retour
- **O** : toggle overlay (always-on-top)

### Actions importantes (Chasse)
- **Start LIVE**
- **Start REPLAY**
- **Stop+Export**
- **Graph LIVE**
- **Sweat ON/OFF**
- **Offset fin CSV** (démarrer une nouvelle session “logique” sans vider le CSV)

### Maintenance
- **Stop ALL parsers**
- **Vider CSV chasse**
- **Vider CSV globals**

---

## Démarrage rapide

> Après clonage GitHub : copie les exemples de config `config/armes.ini.example` et `config/markup.ini.example` vers la racine (`armes.ini`, `markup.ini`).

1) **Dans Entropia**, active l’écriture des logs (option type “log to file / chat log”).
2) Lance `tracker_loot`.
3) Vérifie le statut `chat.log` (Dashboard / Aide).
4) Va sur **Chasse** :
   - sélectionne ton arme (bouton **Arme:** en topbar)
   - **Start LIVE** (ou **Start REPLAY**)
   - optionnel : **Overlay ON** (ou touche `O`)
   - optionnel : **Graph LIVE**
5) Fin de session :
   - **Stop+Export** (export + offset prêt pour la prochaine session)

---

## Compilation

### Linux (X11)
Prérequis :
- `gcc`, `make`
- X11 dev (ex : `libx11-dev`)
- (optionnel) `pkg-config`

Build :
```bash
make
./bin/tracker_loot
```

Raccourcis utiles :
```bash
make run
make debug      # -g -DDEBUG
make release    # -O2
make clean
```

Désactiver `-Werror` si besoin :
```bash
make WERROR=0
```

### Windows
Option A (cross-compile depuis Linux) : **MinGW-w64**
```bash
make win
```
Sortie :
- `bin/tracker_loot.exe`

Option B (compiler sous Windows) : via MSYS2/MinGW (principe identique : `make`)

---

## Configuration

### Chemin du chat.log

Le programme tente de trouver automatiquement `chat.log` dans les emplacements courants :

- **Windows**
  - `C:\Users\<toi>\Documents\Entropia Universe\chat.log`
  - ou `C:\Users\<toi>\OneDrive\Documents\Entropia Universe\chat.log`
- **Linux**
  - `~/Documents/Entropia Universe/chat.log`

Si ton chemin est différent (souvent le cas sous **Wine**), utilise l’override via variable d’environnement :
- `ENTROPIA_CHATLOG`

---

### Si le programme ne trouve pas chat.log (console/terminal)

Si `chat.log` est “introuvable” ou que le statut indique un problème, la méthode la plus propre est de **définir le chemin du fichier** via la variable `ENTROPIA_CHATLOG`.

#### 1) Trouver le fichier `chat.log`

**Linux**
```bash
find ~ -type f -iname "chat.log" 2>/dev/null | head -n 20
```

**Windows PowerShell**
```powershell
Get-ChildItem -Path $HOME -Recurse -Filter chat.log -ErrorAction SilentlyContinue | Select-Object -First 20
```

**Windows CMD**
```bat
dir "%USERPROFILE%\chat.log" /s /b
```

> Astuce Wine (fréquent) : le fichier peut se trouver dans un préfixe Wine, par ex.  
> `~/.wine/drive_c/users/<toi>/My Documents/Entropia Universe/chat.log`  
> (le dossier exact dépend de ton préfixe et de ta configuration)

---

#### 2) Lancer tracker_loot en donnant le chemin (temporaire)

**Linux (bash/zsh)**
```bash
export ENTROPIA_CHATLOG="/chemin/complet/vers/chat.log"
./bin/tracker_loot
```

Tu peux aussi le faire “one-shot” :
```bash
ENTROPIA_CHATLOG="/chemin/complet/vers/chat.log" ./bin/tracker_loot
```

**Windows PowerShell**
```powershell
$env:ENTROPIA_CHATLOG="C:\Chemin\Complet\Entropia Universe\chat.log"
.\bin\tracker_loot.exe
```

**Windows CMD**
```bat
set ENTROPIA_CHATLOG=C:\Chemin\Complet\Entropia Universe\chat.log
bin\tracker_loot.exe
```

---

#### 3) Rendre le chemin permanent (recommandé)

**Linux (bash/zsh)**  
Ajoute dans `~/.bashrc` ou `~/.zshrc` :
```bash
export ENTROPIA_CHATLOG="/chemin/complet/vers/chat.log"
```
Puis recharge :
```bash
source ~/.bashrc
```

**Windows (PowerShell, permanent utilisateur)**
```powershell
[Environment]::SetEnvironmentVariable("ENTROPIA_CHATLOG","C:\Chemin\Complet\Entropia Universe\chat.log","User")
```
Puis **relance** ton terminal / session Windows.

**Windows (CMD, permanent utilisateur)**
```bat
setx ENTROPIA_CHATLOG "C:\Chemin\Complet\Entropia Universe\chat.log"
```
Puis **relance** ton terminal / session Windows.

---

### armes.ini (coût/tir + amplis + MU)

`armes.ini` sert à estimer les **dépenses** quand le log ne fournit pas de valeurs exploitables.

- Section joueur (optionnelle) :
```ini
[PLAYER]
name=TonNom
```

- Amplis (optionnel) avec préfixe `AMP:` :
```ini
[AMP:A101]
amp_decay_shot=0.0000250
amp_mu=1.00
notes=A101
```

- Une section par arme (le nom doit matcher ton usage dans l’app) :
```ini
[Nom Exact de l'arme]
ammo_shot=0.04000
decay_shot=0.01234
amp=A101

; Mode MU séparés (prioritaire si présent)
ammo_mu=1.00
weapon_mu=1.10
amp_mu=1.00

; Fallback legacy (si tu n'utilises pas les MU séparés)
markup=1.10
```

**Formule MU séparés (prioritaire si renseigné)**
```
cost_shot = ammo_shot*ammo_mu + decay_shot*weapon_mu + amp_decay_shot*amp_mu
```

**Fallback legacy**
```
cost_shot = ammo_shot + (decay_shot + amp_decay_shot) * markup
```

Où modifier ?
- Dans l’app : **Configuration → Gérer Armes (INI)**
- Ou à la main : fichier `armes.ini`

Arme active :
- via le bouton **Arme:** (topbar)
- persistance : `logs/weapon_selected.txt`

---

### markup.ini (TT → MU)

Optionnel mais recommandé si tu veux **TT/MU/TT+MU**.

- Chaque section = nom exact item
- Types :
  - `percent` : multiplicateur (ex `1.10` = 110%)
  - `tt_plus` : ajoute une valeur fixe (PED)

Exemple :
```ini
[Shrapnel]
type=percent
value=1.10

[Paint Can orange]
type=percent
value=1.40
```

Où modifier ?
- Dans l’app : **Configuration → Gérer Markup (INI)**
- Ou à la main : fichier `markup.ini`

---

### Option Sweat
- Toggle dans **Chasse** : **Sweat ON/OFF**
- Persistance : `logs/options.cfg`
- Quand OFF : les lignes sweat sont ignorées pour les stats

---

## Fichiers générés

Tout est écrit dans `logs/` (créé automatiquement) :

- `logs/hunt_log.csv` : événements chasse (loot / tirs / kills / sweat / etc.)
- `logs/globals.csv` : globals / hof / craft / rares
- `logs/sessions_stats.csv` : exports de sessions (résumés)
- `logs/hunt_session.offset` : offset de session (point de départ des stats)
- `logs/weapon_selected.txt` : arme active
- `logs/options.cfg` : options (ex : sweat)
- `logs/parser_debug.log` : debug/erreurs (chemins + errno)

---

## Format CSV

### logs/hunt_log.csv (CHASSE) — CSV V2 strict (8 colonnes)

Header :
```
timestamp_unix,event_type,target_or_item,qty,value_uPED,kill_id,flags,raw
```

- `timestamp_unix` : timestamp **Unix en secondes** (issu du timestamp du chat.log, précision = seconde).
- `event_type` : `SHOT`, `KILL`, `LOOT_ITEM`, `SWEAT`, `RECEIVED_OTHER`, `GLOBAL`, `HOF`, `ATH`, ...
- `target_or_item` : mob (pour `KILL`) ou item (pour `LOOT_ITEM`).
- `qty` : quantité entière (ex: `SHOT=1`, `LOOT_ITEM=stack size`).
- `value_uPED` : **argent en entier** (fixed-point) : **1 PED = 10000 uPED**.
- `kill_id` : identifiant monotone de kill (assigné sur `KILL`).
  - Les `LOOT_ITEM` sont rattachés au **meilleur kill récent** (fenêtre ~60s) via `kill_id`.
  - Plusieurs loots dans la **même seconde** gardent le **même `kill_id`** (stabilité des paquets).
- `flags` : bitset
  - bit0 (`1`) : `value_uPED` valide (présent)
  - bit1 (`2`) : `kill_id` présent
- `raw` : ligne brute (trace/debug), CSV-quoted si besoin.

Durabilité / perf :
- Écriture **bufferisée** (chasse intensive), flush régulé (≈1s ou 64 lignes).
- **Crash recovery** : si la dernière ligne est incomplète (pas de `\n`), elle est tronquée au démarrage.
- Si un ancien CSV (non-V2) est détecté : backup automatique en `logs/hunt_log.csv.legacy.bak` puis recréation V2.

---

### logs/globals.csv (GLOBALS) — CSV simple (6 colonnes)

Header :
```
timestamp,event_type,target_or_item,qty,value,raw
```

- `timestamp` : timestamp texte du chat.log.
- `value` : valeur PED (texte), utilisée pour les tops/feeds globals.

---

## Dépannage

### Le parser ne démarre pas
- Ouvre : `logs/parser_debug.log`
- Vérifie :
  - droits de lecture sur `chat.log`
  - droits d’écriture dans `logs/`
  - chemin correct (`ENTROPIA_CHATLOG`)

### REPLAY “duplique” les lignes
Le REPLAY relit le `chat.log` et **append** dans le CSV.
- Si tu veux reconstruire proprement : **Maintenance → Vider CSV chasse** puis relance REPLAY
- Sinon : utilise l’**offset** pour que les stats démarrent où tu veux

### Dépenses à 0 / return incohérent
- pas de dépenses exploitables dans le log ET pas d’arme active/valide
- vérifie `armes.ini` (nom de section exact, valeurs en PED)

### Windows : l’exe ne trouve pas `armes.ini` / `markup.ini`
Le programme choisit automatiquement un **dossier racine stable** :
- soit le dossier de l’exe (si les INI y sont),
- soit le parent (layout repo `bin/..`)

👉 Assure-toi que `armes.ini` et `markup.ini` sont à côté de l’exe **ou** à la racine du projet.

---

## Architecture

Pipeline simplifié :
```
chat.log (Entropia)
   |
   v
[ parser_thread / globals_thread ]  -> append CSV
   |
   v
[ stats (offset) + series LIVE ]    -> calculs purs
   |
   v
[ UI + overlay ]                    -> dashboard / feeds / graph live
```

Notes :
- Le Graph LIVE utilise un cache mis à jour même hors écran
- L’overlay calcule les stats chasse depuis le CSV (toutes les ~250ms)

---

## Licence
Licence propriétaire restrictive. Voir `LICENSE`.
