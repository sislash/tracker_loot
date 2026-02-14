/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ui_key.h                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: entropia-tracker                              +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/01/25                                #+#    #+#             */
/*   Updated: 2026/01/25                                #+#    #+#             */
/*                                                                            */
/* ************************************************************************** */

#ifndef UI_KEY_H
# define UI_KEY_H

/*
 * * Retourne 1 si une touche est dispo (non-bloquant), sinon 0.
 */
int	ui_key_available(void);

/*
 * * Lit 1 char (suppose qu'une touche est dispo), retourne -1 si erreur.
 */
int	ui_key_getch(void);

#endif
