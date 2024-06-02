// SONIC ROBO BLAST 2 KART ~ ZarroTsu
//-----------------------------------------------------------------------------
/// \file  k_kart.h
/// \brief SRB2kart stuff.

#ifndef __K_KART__
#define __K_KART__

#include "../doomdef.h"
#include "../d_player.h" // Need for player_t

#define KART_FULLTURN 800

void K_GetKartBoostPower(player_t *player);
fixed_t K_GetKartSpeed(player_t *player, boolean doboostpower);
fixed_t K_GetKartAccel(player_t *player);

fixed_t K_3dKartMovement(player_t *player, boolean onground, fixed_t forwardmove);

void K_KartPlayerThink(player_t *player, ticcmd_t *cmd);

// =========================================================================
#endif  // __K_KART__
