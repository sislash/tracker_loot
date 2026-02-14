/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   eu_economy.h                                       :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: entropia-tracker                              +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/01/31                                #+#    #+#             */
/*   Updated: 2026/01/31                                ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef EU_ECONOMY_H
# define EU_ECONOMY_H

/*
** Entropia Universe - economic constants
**
** NOTE:
**  - 0.0013 PED / bottle  =>  1.3 PED / 1000 bottles
**  - Keep 4 decimals to match common trade pricing.
*/

# define EU_SWEAT_PED_PER_BOTTLE 0.0013

/* Fixed-point helper (see tm_money.h: 1 PED = 10000 units) */
# define EU_SWEAT_uPED_PER_BOTTLE 13

#endif
