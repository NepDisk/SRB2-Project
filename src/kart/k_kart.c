#include "../doomdef.h"
#include "../doomstat.h"
#include "../doomtype.h"
#include "../command.h"
#include "../netcode/d_netcmd.h"
#include "../d_player.h"
#include "../hu_stuff.h"
#include "../g_game.h"
#include "../m_fixed.h"
#include "../m_random.h"
#include "../m_menu.h" // ffdhidshfuisduifigergho9igj89dgodhfih AAAAAAAAAA
#include "../p_local.h"
#include "../p_slopes.h"
#include "../r_defs.h"
#include "../r_draw.h"
#include "../r_local.h"
#include "../r_state.h"
#include "../r_things.h"
#include "../r_fps.h"
#include "../s_sound.h"
#include "../screen.h"
#include "../st_stuff.h"
#include "../tables.h"
#include "../v_video.h"
#include "../z_zone.h"
#include "../m_misc.h"
#include "../m_cond.h"
#include "k_kart.h"
#include "../f_finale.h"
#include "../lua_hud.h"	// For Lua hud checks
#include "../lua_hook.h"	// For MobjDamage and ShouldDamage
#include "../d_main.h"		// found_extra_kart

#include "../i_video.h"

// sets k_boostpower, k_speedboost, and k_accelboost to whatever we need it to be
void K_GetKartBoostPower(player_t *player)
{
    player->kartspeed = 5; // here to test shit until proper char handling is added.
    player->kartweight = 5;
	fixed_t boostpower = FRACUNIT;
	fixed_t speedboost = 0, accelboost = 0;

	if (player->kartstuff[k_spinouttimer] && player->kartstuff[k_wipeoutslow] == 1) // Slow down after you've been bumped
	{
		player->kartstuff[k_boostpower] = player->kartstuff[k_speedboost] = player->kartstuff[k_accelboost] = 0;
		return;
	}

	// Offroad is separate, it's difficult to factor it in with a variable value anyway.
	if (!(player->kartstuff[k_invincibilitytimer] || player->kartstuff[k_hyudorotimer] || player->kartstuff[k_sneakertimer])
		&& player->kartstuff[k_offroad] >= 0)
		boostpower = FixedDiv(boostpower, player->kartstuff[k_offroad] + FRACUNIT);

	if (player->kartstuff[k_bananadrag] > TICRATE)
		boostpower = (4*boostpower)/5;

	// Banana drag/offroad dust
	if (boostpower < FRACUNIT
		&& player->mo && P_IsObjectOnGround(player->mo)
		&& player->speed > 0
		&& !player->spectator)
	{
		//K_SpawnWipeoutTrail(player->mo, true);
		if (leveltime % 6 == 0)
			S_StartSound(player->mo, sfx_cdfm70);
	}

	if (player->kartstuff[k_sneakertimer]) // Sneaker
	{
		/*switch (gamespeed)
		{
			case 0:
				speedboost = max(speedboost, 53740+768);
				break;
			case 2:
				speedboost = max(speedboost, 17294+768);
				break;
			default:
				speedboost = max(speedboost, 32768);
				break;
		}*/
		speedboost = max(speedboost, 32768);
		accelboost = max(accelboost, 8*FRACUNIT); // + 800%
	}

	if (player->kartstuff[k_invincibilitytimer]) // Invincibility
	{
		speedboost = max(speedboost, 3*FRACUNIT/8); // + 37.5%
		accelboost = max(accelboost, 3*FRACUNIT); // + 300%
	}

	if (player->kartstuff[k_growshrinktimer] > 0) // Grow
	{
		speedboost = max(speedboost, FRACUNIT/5); // + 20%
	}

	if (player->kartstuff[k_driftboost]) // Drift Boost
	{
		speedboost = max(speedboost, FRACUNIT/4); // + 25%
		accelboost = max(accelboost, 4*FRACUNIT); // + 400%
	}

	if (player->kartstuff[k_startboost]) // Startup Boost
	{
		speedboost = max(speedboost, FRACUNIT/4); // + 25%
		accelboost = max(accelboost, 6*FRACUNIT); // + 300%
	}

	// don't average them anymore, this would make a small boost and a high boost less useful
	// just take the highest we want instead

	player->kartstuff[k_boostpower] = boostpower;

	// value smoothing
	if (speedboost > player->kartstuff[k_speedboost])
		player->kartstuff[k_speedboost] = speedboost;
	else
		player->kartstuff[k_speedboost] += (speedboost - player->kartstuff[k_speedboost])/(TICRATE/2);

	player->kartstuff[k_accelboost] = accelboost;
}

fixed_t K_GetKartSpeed(player_t *player, boolean doboostpower)
{
	fixed_t k_speed = 150;
	fixed_t g_cc = FRACUNIT;
	fixed_t xspd = 3072;		// 4.6875 aka 3/64
	UINT8 kartspeed = player->kartspeed;
	fixed_t finalspeed;

	if (doboostpower && !player->kartstuff[k_pogospring] && !P_IsObjectOnGround(player->mo))
		return (75*mapobjectscale); // air speed cap

	/*switch (gamespeed)
	{
		case 0:
			g_cc = 53248 + xspd; //  50cc =  81.25 + 4.69 =  85.94%
			break;
		case 2:
			g_cc = 77824 + xspd; // 150cc = 118.75 + 4.69 = 123.44%
			break;
		default:
			g_cc = 65536 + xspd; // 100cc = 100.00 + 4.69 = 104.69%
			break;
	}*/
	
	g_cc = 65536 + xspd; // 100cc = 100.00 + 4.69 = 104.69%

	/*if (G_BattleGametype() && player->kartstuff[k_bumper] <= 0)
		kartspeed = 1;*/

	k_speed += kartspeed*3; // 153 - 177

	finalspeed = FixedMul(FixedMul(k_speed<<14, g_cc), player->mo->scale);

	if (doboostpower)
		return FixedMul(finalspeed, player->kartstuff[k_boostpower]+player->kartstuff[k_speedboost]);
	return finalspeed;
}

fixed_t K_GetKartAccel(player_t *player)
{
	fixed_t k_accel = 32; // 36;
	UINT8 kartspeed = player->kartspeed;

	/*if (G_BattleGametype() && player->kartstuff[k_bumper] <= 0)
		kartspeed = 1;*/

	//k_accel += 3 * (9 - kartspeed); // 36 - 60
	k_accel += 4 * (9 - kartspeed); // 32 - 64

	return FixedMul(k_accel, FRACUNIT+player->kartstuff[k_accelboost]);
}


fixed_t K_3dKartMovement(player_t *player, boolean onground, fixed_t forwardmove)
{
	fixed_t accelmax = 4000;
	fixed_t newspeed, oldspeed, finalspeed;
	fixed_t p_speed = K_GetKartSpeed(player, true);
	fixed_t p_accel = K_GetKartAccel(player);

	if (!onground) return 0; // If the player isn't on the ground, there is no change in speed

	// ACCELCODE!!!1!11!
	oldspeed = R_PointToDist2(0, 0, player->rmomx, player->rmomy); // FixedMul(P_AproxDistance(player->rmomx, player->rmomy), player->mo->scale);
	newspeed = FixedDiv(FixedDiv(FixedMul(oldspeed, accelmax - p_accel) + FixedMul(p_speed, p_accel), accelmax), ORIG_FRICTION);

	if (player->kartstuff[k_pogospring]) // Pogo Spring minimum/maximum thrust
	{
		const fixed_t hscale = mapobjectscale /*+ (mapobjectscale - player->mo->scale)*/;
		const fixed_t minspeed = 24*hscale;
		const fixed_t maxspeed = 28*hscale;

		if (newspeed > maxspeed && player->kartstuff[k_pogospring] == 2)
			newspeed = maxspeed;
		if (newspeed < minspeed)
			newspeed = minspeed;
	}

	finalspeed = newspeed - oldspeed;

	// forwardmove is:
	//  50 while accelerating,
	//  25 while clutching,
	//   0 with no gas, and
	// -25 when only braking.

	finalspeed *= forwardmove/25;
	finalspeed /= 2;

	if (forwardmove < 0 && finalspeed > mapobjectscale*2)
		return finalspeed/2;
	else if (forwardmove < 0)
		return -mapobjectscale/2;

	if (finalspeed < 0)
		finalspeed = 0;

	return finalspeed;
}

void K_MomentumToFacing(player_t *player)
{
	angle_t dangle = player->mo->angle - R_PointToAngle2(0, 0, player->mo->momx, player->mo->momy);

	if (dangle > ANGLE_180)
		dangle = InvAngle(dangle);

	// If you aren't on the ground or are moving in too different of a direction don't do this
	if (player->mo->eflags & MFE_JUSTHITFLOOR)
		; // Just hit floor ALWAYS redirects
	else if (!P_IsObjectOnGround(player->mo) || dangle > ANGLE_90)
		return;

	P_Thrust(player->mo, player->mo->angle, player->speed - FixedMul(player->speed, player->mo->friction));
	player->mo->momx = FixedMul(player->mo->momx - player->cmomx, player->mo->friction) + player->cmomx;
	player->mo->momy = FixedMul(player->mo->momy - player->cmomy, player->mo->friction) + player->cmomy;
}

// countersteer is how strong the controls are telling us we are turning
// turndir is the direction the controls are telling us to turn, -1 if turning right and 1 if turning left
static INT16 K_GetKartDriftValue(player_t *player, fixed_t countersteer)
{
	INT16 basedrift, driftangle;
	fixed_t driftweight = player->kartweight*14; // 12

	// If they aren't drifting or on the ground this doesn't apply
	if (player->kartstuff[k_drift] == 0 || !P_IsObjectOnGround(player->mo))
		return 0;

	if (player->kartstuff[k_driftend] != 0)
	{
		return -266*player->kartstuff[k_drift]; // Drift has ended and we are tweaking their angle back a bit
	}

	//basedrift = 90*player->kartstuff[k_drift]; // 450
	//basedrift = 93*player->kartstuff[k_drift] - driftweight*3*player->kartstuff[k_drift]/10; // 447 - 303
	basedrift = 83*player->kartstuff[k_drift] - (driftweight - 14)*player->kartstuff[k_drift]/5; // 415 - 303
	driftangle = abs((252 - driftweight)*player->kartstuff[k_drift]/5);

	return basedrift + FixedMul(driftangle, countersteer);
}

INT16 K_GetKartTurnValue(player_t *player, INT16 turnvalue)
{
	fixed_t p_topspeed = K_GetKartSpeed(player, false);
	fixed_t p_curspeed = min(player->speed, p_topspeed * 2);
	fixed_t p_maxspeed = p_topspeed * 3;
	fixed_t adjustangle = FixedDiv((p_maxspeed>>16) - (p_curspeed>>16), (p_maxspeed>>16) + player->kartweight);

	if (player->spectator)
		return turnvalue;

	if (player->kartstuff[k_drift] != 0 && P_IsObjectOnGround(player->mo))
	{
		// If we're drifting we have a completely different turning value
		if (player->kartstuff[k_driftend] == 0)
		{
			// 800 is the max set in g_game.c with angleturn
			fixed_t countersteer = FixedDiv(turnvalue*FRACUNIT, 800*FRACUNIT);
			turnvalue = K_GetKartDriftValue(player, countersteer);
		}
		else
			turnvalue = (INT16)(turnvalue + K_GetKartDriftValue(player, FRACUNIT));

		return turnvalue;
	}

	turnvalue = FixedMul(turnvalue, adjustangle); // Weight has a small effect on turning

	if (player->kartstuff[k_invincibilitytimer] || player->kartstuff[k_sneakertimer] || player->kartstuff[k_growshrinktimer] > 0)
		turnvalue = FixedMul(turnvalue, FixedDiv(5*FRACUNIT, 4*FRACUNIT));

	return turnvalue;
}

void K_KartPlayerThink(player_t *player, ticcmd_t *cmd)
{
    
    K_GetKartBoostPower(player);
    
    
    
    
    
    
    
    
    
}

