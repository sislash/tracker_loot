#ifndef CONFIG_ARME_H
#define CONFIG_ARME_H

/*
 * config_arme.h
 * -------------
 * Charge une base d'armes depuis un fichier INI simple + section PLAYER.
 * Portable Linux / Windows (C standard).
 *
 * IMPORTANT RCE (Real Cash Economy):
 * - Tous les couts sont stockes en fixed-point uPED (1/10000 PED).
 * - Les MU (markup) sont stockes en fixed-point 1e4 (10000 = x1.0000).
 *
 * Section joueur (optionnelle):
 *   [PLAYER]
 *   name=TonNom
 *
 * Section arme:
 *   [Nom Exact de l'arme]
 *   dpp=...
 *   ammo_shot=...          ; PED/shot
 *   decay_shot=...         ; PED/shot
 *   amp_decay_shot=...     ; PED/shot (ou via amp=)
 *   markup=...             ; legacy MU (1.00=TT, 1.25=125%) applique sur decays
 *   ammo_mu=...            ; MU ammo (1.00 default)
 *   weapon_mu=...          ; MU arme (0 => legacy)
 *   amp_mu=...             ; MU amp (0 => legacy)
 *   amp=A101               ; optionnel: lie les stats AMP:
 *   notes=...
 *
 * Sections amplis:
 *   [AMP:A101]
 *   amp_decay_shot=...
 *   amp_mu=...
 *   notes=...
 */

#include <stddef.h>
#include <stdint.h>
#include "tm_money.h"

#ifdef __cplusplus
extern "C" {
#endif

    typedef struct arme_stats
    {
        char        name[128];       /* Nom de l'arme (section INI) */
        char        notes[256];      /* Texte libre */
        char        amp_name[128];   /* nom de l'amp reference: ex "A101" */

        double      dpp;             /* Damage Per PEC (ou DPP) */

        /* Cout par tir (TT) en uPED */
        tm_money_t  ammo_shot;       /* Cout munition par tir (PED) */
        tm_money_t  decay_shot;      /* Decay arme par tir (PED) */
        tm_money_t  amp_decay_shot;  /* Decay ampli par tir (PED) */

        /* Legacy: MU unique applique sur les decays (EU-correct) */
        int64_t     markup_mu_1e4;   /* 10000=TT, 12500=125% */

        /* NEW: MU separes */
        int64_t     ammo_mu_1e4;     /* ex: 10000 */
        int64_t     weapon_mu_1e4;   /* 0 => legacy */
        int64_t     amp_mu_1e4;      /* 0 => legacy */

    } arme_stats;

    typedef struct s_amp_stats
    {
        char        name[128];
        tm_money_t  amp_decay_shot;
        int64_t     amp_mu_1e4;
        char        notes[256];
    } amp_stats;

    typedef struct s_amps_db
    {
        amp_stats   *items;
        size_t      count;
    } amps_db;

    typedef struct armes_db
    {
        arme_stats  *items;
        size_t      count;

        amps_db     amps;

        /* AJOUT: nom du joueur lu dans [PLAYER] name=... */
        char        player_name[128];
    } armes_db;

    /* Charge config/armes.ini (ou autre chemin).
     * Retour: 1 si OK, 0 si erreur.
     */
    int     armes_db_load(armes_db *db, const char *path);

    /* Save whole db back to an INI file (rewrite file). Returns 1 on success. */
    int     armes_db_save(const armes_db *db, const char *path);

    /* Libere la memoire. */
    void    armes_db_free(armes_db *db);

    /* Recherche une arme par nom exact (strcmp).
     * Retour: pointeur vers l'arme, ou NULL.
     */
    const arme_stats *armes_db_find(const armes_db *db, const char *name);

    /* Calcule cout total par tir.
     * - Mode MU separes (si weapon_mu/amp_mu definis):
     *     cost = ammo_shot*ammo_mu + decay_shot*weapon_mu + amp_decay_shot*amp_mu
     * - Fallback legacy (EU-correct):
     *     cost = ammo_shot + (decay_shot + amp_decay_shot) * markup
     */
    tm_money_t  arme_cost_shot_uPED(const arme_stats *w);
    double      arme_cost_shot_ped(const arme_stats *w); /* helper UI */

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_ARME_H */
