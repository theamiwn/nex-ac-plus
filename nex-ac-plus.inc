/*
 *  Nex-AC Plus v2.0.0  -  behavioural anti-cheat layer for SA-MP 0.3.7+
 *  License: LGPL-3.0 (same as Nex-AC). Can be used standalone or next to Nex-AC.
 *
 *  WHAT IS NEW COMPARED TO "ONE STRIKE = KICK" ANTI-CHEATS
 *  -------------------------------------------------------
 *  1. Violation SCORE system. Every suspicious event adds points to a per-player
 *     score that decays over time. Lag spikes give a few points that fade away;
 *     real cheats pile up points in seconds and get punished. Far fewer false kicks.
 *  2. Time/ping-aware movement analysis (speed, teleport, vertical fly) with
 *     automatic SET-BACK to the last safe position instead of only reporting.
 *  3. Shot validation: fake origin, impossible range, silent-aim / fake hit offsets,
 *     impossible damage per weapon, rapid fire, shooting a weapon you don't own.
 *  4. Server-authoritative state: health, armour, money, weapons, jetpack are
 *     tracked from your own script calls; client values that exceed them are
 *     flagged AND reverted.
 *  5. Vehicle checks: remote enter, teleport-into-vehicle, unoccupied vehicle
 *     teleport/speed, vehicle health regeneration.
 *  6. Fake kill / fake death sanitising (bad killer, out of range, double death).
 *  7. Callback flood limiter, connection-flood and RCON brute-force IP blocking.
 *  8. Hooks for punishment (OnACPlusPunish) and alerts (OnACPlusFlag).
 *
 *  INSTALL
 *      #include <a_samp>
 *      #include <nex-ac-plus>        // put it in the GAMEMODE, right after a_samp
 *
 *  IMPORTANT NOTES
 *  - Server-side checks can never be perfect. This raises the cost of cheating a
 *    lot, but it does not replace a client-side/plugin solution against
 *    aimbots, wallhacks or modified clients.
 *  - State checks (health/armour/money/weapons/jetpack/fake-spawn) rely on native
 *    hooks. They assume ONE script owns those calls (your gamemode). If you have
 *    filterscripts that change them, either call ACP_AllowTeleport / ACP_ResyncState
 *    from there, or define ACP_NO_NATIVE_HOOKS before including (then only the
 *    purely behavioural checks stay active and you call ACP_AllowTeleport
 *    yourself after server-side teleports).
 *  - Dialog id validation is opt-in:  #define ACP_DIALOG_CHECK  (before include)
 *  - Tune every threshold below with #define before the include.
 */

#if defined _nexac_plus_included
	#endinput
#endif
#define _nexac_plus_included

#include <a_samp>

#define ACP_VERSION "2.0.0"

// ---------------------------------------------------------------------------
//  Configuration (override with #define before including)
// ---------------------------------------------------------------------------
#if !defined ACP_SCORE_KICK
	#define ACP_SCORE_KICK          100     // score at which the player is punished
#endif
#if !defined ACP_SCORE_WARN
	#define ACP_SCORE_WARN          60      // score at which OnACPlusFlag gets warn=1
#endif
#if !defined ACP_DECAY_PER_SEC
	#define ACP_DECAY_PER_SEC       2       // points removed every second
#endif
#if !defined ACP_KICK_DELAY
	#define ACP_KICK_DELAY          700     // ms between punish and Kick()
#endif
#if !defined ACP_MAX_PING
	#define ACP_MAX_PING            800     // ping above this adds score every second
#endif
#if !defined ACP_FOOT_SPEED
	#define ACP_FOOT_SPEED          14.0    // max horizontal m/s on foot
#endif
#if !defined ACP_JETPACK_SPEED
	#define ACP_JETPACK_SPEED       45.0
#endif
#if !defined ACP_VEHICLE_SPEED
	#define ACP_VEHICLE_SPEED       120.0   // m/s (432 km/h), planes included
#endif
#if !defined ACP_TELEPORT_MIN_DIST
	#define ACP_TELEPORT_MIN_DIST   35.0    // jump below this is never a "teleport"
#endif
#if !defined ACP_SETBACK
	#define ACP_SETBACK             true    // move cheaters back to last safe position
#endif

#if !defined ACP_NO_NATIVE_HOOKS
	#define ACP_HOOKS
#endif

// ---------------------------------------------------------------------------
//  Check ids
// ---------------------------------------------------------------------------
enum
{
	ACP_SPEED_FOOT,
	ACP_SPEED_VEHICLE,
	ACP_TELEPORT,
	ACP_FLY,
	ACP_RAPIDFIRE,
	ACP_FAKESHOT,
	ACP_SILENTAIM,
	ACP_DAMAGE,
	ACP_HEALTH,
	ACP_ARMOUR,
	ACP_MONEY,
	ACP_WEAPON,
	ACP_JETPACK,
	ACP_VEHICLE_TELEPORT,
	ACP_VEHICLE_HEALTH,
	ACP_ENTER_VEHICLE,
	ACP_FAKESPAWN,
	ACP_FAKEKILL,
	ACP_DIALOG,
	ACP_FLOOD,
	ACP_PING,
	ACP_CHECK_COUNT
}

static const gACP_CheckName[ACP_CHECK_COUNT][] =
{
	"SpeedHack(foot)", "SpeedHack(vehicle)", "Teleport", "FlyHack", "RapidFire",
	"FakeShot", "SilentAim", "DamageHack", "HealthHack", "ArmourHack", "MoneyHack",
	"WeaponHack", "Jetpack", "VehicleTeleport", "VehicleHealth", "RemoteEnterVehicle",
	"FakeSpawn", "FakeKill", "DialogHack", "CallbackFlood", "HighPing"
};

// rate-limit slots
enum
{
	ACP_RL_TEXT, ACP_RL_CMD, ACP_RL_DIALOG, ACP_RL_ENTER, ACP_RL_EXIT,
	ACP_RL_SPAWNREQ, ACP_RL_CLASS, ACP_RL_PICKUP, ACP_RL_COUNT
}

// ---------------------------------------------------------------------------
//  State
// ---------------------------------------------------------------------------
#define ACP_IP_SLOTS 48

static
	bool:gACP_CheckOn[ACP_CHECK_COUNT],
	bool:gACP_HasFlagCB,
	bool:gACP_HasPunishCB,
	gACP_Timer = -1;

static
	gACP_Score[MAX_PLAYERS],
	bool:gACP_Punished[MAX_PLAYERS],
	bool:gACP_Exempt[MAX_PLAYERS],
	gACP_LastLog[MAX_PLAYERS][ACP_CHECK_COUNT],
	// movement
	gACP_LastTick[MAX_PLAYERS],
	gACP_Grace[MAX_PLAYERS],
	bool:gACP_HavePos[MAX_PLAYERS],
	Float:gACP_LastPos[MAX_PLAYERS][3],
	Float:gACP_SafePos[MAX_PLAYERS][3],
	bool:gACP_InModShop[MAX_PLAYERS],
	// shooting
	gACP_LastShot[MAX_PLAYERS],
	gACP_LastShotWeapon[MAX_PLAYERS],
	// authoritative state
	Float:gACP_Health[MAX_PLAYERS],
	Float:gACP_Armour[MAX_PLAYERS],
	gACP_HealthGrace[MAX_PLAYERS],
	gACP_Money[MAX_PLAYERS],
	bool:gACP_Weapon[MAX_PLAYERS][47],
	gACP_WeaponSnap[MAX_PLAYERS],
	bool:gACP_JetpackOK[MAX_PLAYERS],
	// life cycle
	bool:gACP_Dead[MAX_PLAYERS],
	bool:gACP_Requested[MAX_PLAYERS],
	bool:gACP_AllowSpawn[MAX_PLAYERS],
	gACP_Dialog[MAX_PLAYERS],
	// rate limits
	gACP_RLStart[MAX_PLAYERS][ACP_RL_COUNT],
	gACP_RLCount[MAX_PLAYERS][ACP_RL_COUNT];

static
	Float:gACP_VehHealth[MAX_VEHICLES],
	gACP_IPAddr[2][ACP_IP_SLOTS][16],
	gACP_IPStart[2][ACP_IP_SLOTS],
	gACP_IPCount[2][ACP_IP_SLOTS];

forward ACP_Tick();
forward ACP_DoKick(playerid);

// ---------------------------------------------------------------------------
//  Public API
// ---------------------------------------------------------------------------
stock ACP_EnableCheck(check, bool:enable)
{
	if(check < 0 || check >= ACP_CHECK_COUNT) return 0;
	gACP_CheckOn[check] = enable;
	return 1;
}

stock bool:ACP_IsCheckEnabled(check)
{
	return (check >= 0 && check < ACP_CHECK_COUNT) ? gACP_CheckOn[check] : false;
}

/** Exempt a player (e.g. RCON admin, NPC) from every check. */
stock ACP_SetPlayerExempt(playerid, bool:exempt)
{
	if(playerid < 0 || playerid >= MAX_PLAYERS) return 0;
	gACP_Exempt[playerid] = exempt;
	return 1;
}

stock bool:ACP_IsPlayerExempt(playerid)
{
	return (playerid >= 0 && playerid < MAX_PLAYERS) ? gACP_Exempt[playerid] : false;
}

stock ACP_GetScore(playerid)
{
	return (playerid >= 0 && playerid < MAX_PLAYERS) ? gACP_Score[playerid] : 0;
}

stock ACP_ResetScore(playerid)
{
	if(playerid >= 0 && playerid < MAX_PLAYERS) gACP_Score[playerid] = 0;
}

/** Call after ANY server-side teleport that does not go through SetPlayerPos
 *  (or always, when ACP_NO_NATIVE_HOOKS is defined). */
stock ACP_AllowTeleport(playerid, ms = 2000)
{
	if(playerid < 0 || playerid >= MAX_PLAYERS) return 0;
	gACP_Grace[playerid] = GetTickCount() + ms;
	gACP_HavePos[playerid] = false;
	return 1;
}

stock ACP_AllowJetpack(playerid, bool:allow)
{
	if(playerid < 0 || playerid >= MAX_PLAYERS) return 0;
	gACP_JetpackOK[playerid] = allow;
	return 1;
}

/** Re-learn health/armour/weapons from the current server values
 *  (use after a filterscript changed them). */
stock ACP_ResyncState(playerid)
{
	if(playerid < 0 || playerid >= MAX_PLAYERS || !IsPlayerConnected(playerid)) return 0;
	new Float:v, w, a;
	GetPlayerHealth(playerid, v); gACP_Health[playerid] = v;
	GetPlayerArmour(playerid, v); gACP_Armour[playerid] = v;
	gACP_Money[playerid] = GetPlayerMoney(playerid);
	gACP_HealthGrace[playerid] = GetTickCount() + 2500;
	for(new s; s < 13; s++)
	{
		GetPlayerWeaponData(playerid, s, w, a);
		if(w > 0 && w < 47) gACP_Weapon[playerid][w] = true;
	}
	return 1;
}

// ---------------------------------------------------------------------------
//  Internals
// ---------------------------------------------------------------------------
stock Float:ACP_Dist3D(Float:x1, Float:y1, Float:z1, Float:x2, Float:y2, Float:z2)
{
	new Float:dx = x1 - x2, Float:dy = y1 - y2, Float:dz = z1 - z2;
	return floatsqroot(dx * dx + dy * dy + dz * dz);
}

stock Float:ACP_WeaponRange(weaponid)
{
	switch(weaponid)
	{
		case 0..15: return 6.0;
		case 22, 23, 24: return 35.0;
		case 25, 26, 27: return 40.0;
		case 28, 29, 32: return 35.0;
		case 30, 31: return 90.0;
		case 33: return 100.0;
		case 34: return 320.0;
	}
	return 0.0;
}

stock ACP_MinFireInterval(weaponid)
{
	switch(weaponid)
	{
		case 22: return 90;
		case 23: return 70;
		case 24: return 150;
		case 25: return 500;
		case 26: return 200;
		case 27: return 250;
		case 28: return 30;
		case 29: return 45;
		case 30, 31: return 50;
		case 32: return 30;
		case 33, 34: return 600;
	}
	return 0;
}

stock Float:ACP_MaxDamage(weaponid)
{
	switch(weaponid)
	{
		case 22: return 8.25;
		case 23: return 13.2;
		case 24: return 46.2;
		case 25, 26: return 3.3;
		case 27: return 4.95;
		case 28, 32: return 6.6;
		case 29: return 8.25;
		case 30, 31: return 9.9;
		case 33: return 24.75;
		case 34: return 41.25;
	}
	return 0.0;
}

stock ACP_Punish(playerid, check)
{
	if(gACP_Punished[playerid]) return 0;
	gACP_Punished[playerid] = true;

	new bool:kick = true;
	if(gACP_HasPunishCB)
	{
		kick = (CallLocalFunction("OnACPlusPunish", "iii", playerid, check, gACP_Score[playerid]) != 0);
	}
	if(kick)
	{
		new name[MAX_PLAYER_NAME];
		GetPlayerName(playerid, name, sizeof name);
		printf("[ACPlus] %s(%d) punished: %s", name, playerid, gACP_CheckName[check]);
		SendClientMessage(playerid, 0xFF4040FF, "You have been kicked by the anti-cheat.");
		SetTimerEx("ACP_DoKick", ACP_KICK_DELAY, false, "i", playerid);
	}
	return 1;
}

public ACP_DoKick(playerid)
{
	if(IsPlayerConnected(playerid)) Kick(playerid);
	return 1;
}

/** Adds violation points. Returns 1 if the event was counted. */
stock ACP_Flag(playerid, check, points, const info[] = "")
{
	if(playerid < 0 || playerid >= MAX_PLAYERS) return 0;
	if(!gACP_CheckOn[check] || gACP_Exempt[playerid] || gACP_Punished[playerid]) return 0;
	if(!IsPlayerConnected(playerid) || IsPlayerNPC(playerid)) return 0;

	gACP_Score[playerid] += points;

	new now = GetTickCount();
	if(now - gACP_LastLog[playerid][check] > 1000 || now < gACP_LastLog[playerid][check])
	{
		gACP_LastLog[playerid][check] = now;
		new name[MAX_PLAYER_NAME];
		GetPlayerName(playerid, name, sizeof name);
		printf("[ACPlus] %s(%d) %s +%d (score %d) %s", name, playerid, gACP_CheckName[check], points, gACP_Score[playerid], info);
	}
	if(gACP_HasFlagCB)
	{
		CallLocalFunction("OnACPlusFlag", "iiiii", playerid, check, points, gACP_Score[playerid], (gACP_Score[playerid] >= ACP_SCORE_WARN) ? 1 : 0);
	}
	if(gACP_Score[playerid] >= ACP_SCORE_KICK) ACP_Punish(playerid, check);
	return 1;
}

/** Returns true when the player exceeded `limit` events in `window` ms. */
stock bool:ACP_RateLimit(playerid, slot, limit, window)
{
	new now = GetTickCount();
	if(now - gACP_RLStart[playerid][slot] > window || now < gACP_RLStart[playerid][slot])
	{
		gACP_RLStart[playerid][slot] = now;
		gACP_RLCount[playerid][slot] = 0;
	}
	if(++gACP_RLCount[playerid][slot] > limit)
	{
		ACP_Flag(playerid, ACP_FLOOD, 4, "callback flood");
		return true;
	}
	return false;
}

/** Counts hits per IP inside a time window. bucket 0 = connections, 1 = rcon fails. */
stock ACP_IPHit(bucket, const ip[], window)
{
	new now = GetTickCount(), oldest = 0, oldestStart = 0x7FFFFFFF;
	for(new i; i < ACP_IP_SLOTS; i++)
	{
		if(gACP_IPCount[bucket][i] > 0 && !strcmp(gACP_IPAddr[bucket][i], ip))
		{
			if(now - gACP_IPStart[bucket][i] > window || now < gACP_IPStart[bucket][i])
			{
				gACP_IPStart[bucket][i] = now;
				gACP_IPCount[bucket][i] = 0;
			}
			return ++gACP_IPCount[bucket][i];
		}
		if(gACP_IPCount[bucket][i] == 0) { oldest = i; oldestStart = -1; }
		else if(oldestStart != -1 && gACP_IPStart[bucket][i] < oldestStart) { oldest = i; oldestStart = gACP_IPStart[bucket][i]; }
	}
	format(gACP_IPAddr[bucket][oldest], 16, "%s", ip);
	gACP_IPStart[bucket][oldest] = now;
	gACP_IPCount[bucket][oldest] = 1;
	return 1;
}

static ACP_ResetPlayer(playerid)
{
	new now = GetTickCount();
	gACP_Score[playerid] = 0;
	gACP_Punished[playerid] = false;
	gACP_Exempt[playerid] = false;
	gACP_LastTick[playerid] = now;
	gACP_Grace[playerid] = now + 3000;
	gACP_HavePos[playerid] = false;
	gACP_InModShop[playerid] = false;
	gACP_LastShot[playerid] = 0;
	gACP_LastShotWeapon[playerid] = 0;
	gACP_Health[playerid] = 100.0;
	gACP_Armour[playerid] = 0.0;
	gACP_HealthGrace[playerid] = now + 3000;
	gACP_Money[playerid] = 0;
	gACP_WeaponSnap[playerid] = now + 3000;
	gACP_JetpackOK[playerid] = false;
	gACP_Dead[playerid] = false;
	gACP_Requested[playerid] = false;
	gACP_AllowSpawn[playerid] = false;
	gACP_Dialog[playerid] = -1;
	for(new i; i < 47; i++) gACP_Weapon[playerid][i] = false;
	for(new i; i < ACP_CHECK_COUNT; i++) gACP_LastLog[playerid][i] = 0;
	for(new i; i < ACP_RL_COUNT; i++) { gACP_RLStart[playerid][i] = 0; gACP_RLCount[playerid][i] = 0; }
}

static ACP_Init()
{
	for(new i; i < ACP_CHECK_COUNT; i++) gACP_CheckOn[i] = true;
	for(new i; i < MAX_VEHICLES; i++) gACP_VehHealth[i] = -1.0;
	for(new b; b < 2; b++) for(new i; i < ACP_IP_SLOTS; i++) gACP_IPCount[b][i] = 0;
	gACP_HasFlagCB = (funcidx("OnACPlusFlag") != -1);
	gACP_HasPunishCB = (funcidx("OnACPlusPunish") != -1);
	for(new p; p < MAX_PLAYERS; p++) ACP_ResetPlayer(p);
	if(gACP_Timer == -1) gACP_Timer = SetTimer("ACP_Tick", 1000, true);
	printf("[ACPlus] Nex-AC Plus v" ACP_VERSION " loaded.");
}

// ---------------------------------------------------------------------------
//  Per-second audit: score decay, ping, weapons, money, health, armour, jetpack
// ---------------------------------------------------------------------------
public ACP_Tick()
{
	#if defined ACP_HOOKS
	new now = GetTickCount();
	#endif
	new pool = GetPlayerPoolSize();
	for(new i; i <= pool; i++)
	{
		if(!IsPlayerConnected(i) || IsPlayerNPC(i)) continue;
		if(gACP_Score[i] > 0)
		{
			gACP_Score[i] -= ACP_DECAY_PER_SEC;
			if(gACP_Score[i] < 0) gACP_Score[i] = 0;
		}
		if(gACP_Punished[i] || gACP_Exempt[i]) continue;

		if(GetPlayerPing(i) > ACP_MAX_PING) ACP_Flag(i, ACP_PING, 10, "ping");

		#if defined ACP_HOOKS
		new pstate = GetPlayerState(i);
		if(pstate == PLAYER_STATE_SPECTATING || pstate == PLAYER_STATE_WASTED || pstate == PLAYER_STATE_NONE) continue;

		// ---- health / armour -------------------------------------------------
		new Float:v;
		GetPlayerHealth(i, v);
		if(now < gACP_HealthGrace[i]) gACP_Health[i] = v;
		else if(v > gACP_Health[i] + 1.0)
		{
			ACP_Flag(i, ACP_HEALTH, 20, "health above server value");
			SetPlayerHealth(i, gACP_Health[i]);
		}
		else gACP_Health[i] = v;

		GetPlayerArmour(i, v);
		if(now < gACP_HealthGrace[i]) gACP_Armour[i] = v;
		else if(v > gACP_Armour[i] + 1.0)
		{
			ACP_Flag(i, ACP_ARMOUR, 20, "armour above server value");
			SetPlayerArmour(i, gACP_Armour[i]);
		}
		else gACP_Armour[i] = v;

		// ---- money ----------------------------------------------------------
		new m = GetPlayerMoney(i);
		if(m > gACP_Money[i])
		{
			ACP_Flag(i, ACP_MONEY, 25, "money above server value");
			ResetPlayerMoney(i);
			GivePlayerMoney(i, gACP_Money[i]);
		}

		// ---- jetpack --------------------------------------------------------
		if(GetPlayerSpecialAction(i) == SPECIAL_ACTION_USEJETPACK && !gACP_JetpackOK[i])
		{
			ACP_Flag(i, ACP_JETPACK, 30, "unauthorised jetpack");
			SetPlayerSpecialAction(i, SPECIAL_ACTION_NONE);
		}

		// ---- weapons --------------------------------------------------------
		new w, a, bool:illegal = false, ws[13], wa[13];
		for(new s; s < 13; s++)
		{
			GetPlayerWeaponData(i, s, w, a);
			ws[s] = w; wa[s] = a;
			if(w <= 0 || w >= 47 || w == 46) continue;
			if(now < gACP_WeaponSnap[i]) gACP_Weapon[i][w] = true;
			else if(!gACP_Weapon[i][w]) illegal = true;
		}
		if(illegal)
		{
			ACP_Flag(i, ACP_WEAPON, 25, "weapon not given by server");
			ResetPlayerWeapons(i);
			for(new s; s < 13; s++)
			{
				if(ws[s] > 0 && ws[s] < 47 && (gACP_Weapon[i][ws[s]] || ws[s] == 46)) GivePlayerWeapon(i, ws[s], wa[s]);
			}
		}
		#endif
	}
	return 1;
}

// ---------------------------------------------------------------------------
//  Movement analysis
// ---------------------------------------------------------------------------
static bool:ACP_OnUpdate(playerid)
{
	if(gACP_Punished[playerid]) return false;
	if(gACP_Exempt[playerid] || IsPlayerNPC(playerid)) return true;

	new Float:x, Float:y, Float:z, now = GetTickCount();
	GetPlayerPos(playerid, x, y, z);

	if(x != x || y != y || z != z || floatabs(x) > 20000.0 || floatabs(y) > 20000.0 || floatabs(z) > 20000.0)
	{
		ACP_Flag(playerid, ACP_TELEPORT, ACP_SCORE_KICK, "invalid position data");
		return false;
	}

	new Float:lx = gACP_LastPos[playerid][0], Float:ly = gACP_LastPos[playerid][1], Float:lz = gACP_LastPos[playerid][2];
	new dt = now - gACP_LastTick[playerid], bool:have = gACP_HavePos[playerid];

	gACP_LastTick[playerid] = now;
	gACP_LastPos[playerid][0] = x; gACP_LastPos[playerid][1] = y; gACP_LastPos[playerid][2] = z;
	gACP_HavePos[playerid] = true;

	new pstate = GetPlayerState(playerid);
	new bool:skip = (!have || now < gACP_Grace[playerid] || dt <= 0 || dt > 2500 || gACP_InModShop[playerid]);
	if(skip || (pstate != PLAYER_STATE_ONFOOT && pstate != PLAYER_STATE_DRIVER))
	{
		gACP_SafePos[playerid][0] = x; gACP_SafePos[playerid][1] = y; gACP_SafePos[playerid][2] = z;
		return true;
	}

	new Float:dx = x - lx, Float:dy = y - ly, Float:dz = z - lz;
	new Float:hdist = floatsqroot(dx * dx + dy * dy);
	new Float:secs = float(dt) / 1000.0;
	new bool:ok = true;

	if(pstate == PLAYER_STATE_DRIVER)
	{
		new Float:d = floatsqroot(dx * dx + dy * dy + dz * dz);
		new Float:allowed = ACP_VEHICLE_SPEED * secs + 5.0;
		if(d > allowed)
		{
			ok = false;
			new Float:ratio = d / allowed;
			if(ratio > 3.5 && d > ACP_TELEPORT_MIN_DIST * 2.0)
				ACP_Flag(playerid, ACP_TELEPORT, 35, "vehicle teleport");
			else
				ACP_Flag(playerid, ACP_SPEED_VEHICLE, floatround(ratio * 3.0) + 2, "vehicle speed");
		}

		// vehicle health regeneration
		new veh = GetPlayerVehicleID(playerid);
		if(veh > 0 && veh < MAX_VEHICLES)
		{
			new Float:vh;
			GetVehicleHealth(veh, vh);
			new Float:k = gACP_VehHealth[veh];
			if(k < 0.0 || vh < k) gACP_VehHealth[veh] = vh;
			else if(vh > k + 5.0)
			{
				ACP_Flag(playerid, ACP_VEHICLE_HEALTH, 5, "vehicle health went up");
				gACP_VehHealth[veh] = vh;
			}
		}
	}
	else
	{
		if(GetPlayerSurfingVehicleID(playerid) != INVALID_VEHICLE_ID || GetPlayerSurfingObjectID(playerid) != INVALID_OBJECT_ID || GetPlayerWeapon(playerid) == 46)
		{
			gACP_SafePos[playerid][0] = x; gACP_SafePos[playerid][1] = y; gACP_SafePos[playerid][2] = z;
			return true;
		}
		new bool:jet = (GetPlayerSpecialAction(playerid) == SPECIAL_ACTION_USEJETPACK);
		new Float:limit = jet ? ACP_JETPACK_SPEED : ACP_FOOT_SPEED;
		new Float:allowed = limit * secs + 2.5;
		new Float:d3 = floatsqroot(dx * dx + dy * dy + dz * dz);

		if(d3 > allowed && hdist > allowed * 0.5)
		{
			ok = false;
			new Float:ratio = d3 / allowed;
			if(ratio > 3.5 && d3 > ACP_TELEPORT_MIN_DIST)
			{
				ACP_Flag(playerid, ACP_TELEPORT, 35, "teleport onfoot");
				#if ACP_SETBACK
				if(!gACP_Punished[playerid])
				{
					SetPlayerPos(playerid, gACP_SafePos[playerid][0], gACP_SafePos[playerid][1], gACP_SafePos[playerid][2]);
					gACP_Grace[playerid] = now + 1000;
					gACP_HavePos[playerid] = false;
				}
				#endif
			}
			else ACP_Flag(playerid, ACP_SPEED_FOOT, floatround(ratio * 3.0) + 2, "speed onfoot");
		}
		else if(!jet && dz > 9.0 * secs + 1.5)
		{
			ok = false;
			ACP_Flag(playerid, ACP_FLY, 10, "vertical speed onfoot");
		}
	}

	if(ok)
	{
		gACP_SafePos[playerid][0] = x; gACP_SafePos[playerid][1] = y; gACP_SafePos[playerid][2] = z;
	}
	return true;
}

// ---------------------------------------------------------------------------
//  Shot / damage / death validation
// ---------------------------------------------------------------------------
static bool:ACP_OnShot(playerid, weaponid, hittype, hitid, Float:fX, Float:fY, Float:fZ)
{
	if(gACP_Punished[playerid]) return false;
	if(gACP_Exempt[playerid] || IsPlayerNPC(playerid)) return true;

	new now = GetTickCount(), pstate = GetPlayerState(playerid);

	if(weaponid < 0 || weaponid > 46 || pstate == PLAYER_STATE_WASTED || pstate == PLAYER_STATE_SPECTATING)
	{
		ACP_Flag(playerid, ACP_FAKESHOT, 20, "shot in invalid state/weapon");
		return false;
	}

	// ---- rapid fire (only on foot, drive-by uses different timings) ----------
	new minInt = ACP_MinFireInterval(weaponid);
	if(minInt > 0 && pstate == PLAYER_STATE_ONFOOT && gACP_LastShotWeapon[playerid] == weaponid)
	{
		new gap = now - gACP_LastShot[playerid];
		if(gap >= 0 && gap < minInt) ACP_Flag(playerid, ACP_RAPIDFIRE, 6, "interval too short");
	}
	gACP_LastShot[playerid] = now;
	gACP_LastShotWeapon[playerid] = weaponid;

	#if defined ACP_HOOKS
	if(weaponid != 0 && weaponid != 38 && now > gACP_WeaponSnap[playerid] && !gACP_Weapon[playerid][weaponid])
	{
		ACP_Flag(playerid, ACP_WEAPON, 15, "shot with weapon not owned");
	}
	#endif

	new Float:ox, Float:oy, Float:oz, Float:hx, Float:hy, Float:hz, Float:px, Float:py, Float:pz;
	GetPlayerLastShotVectors(playerid, ox, oy, oz, hx, hy, hz);
	GetPlayerPos(playerid, px, py, pz);

	if(ACP_Dist3D(ox, oy, oz, px, py, pz) > 12.0)
	{
		ACP_Flag(playerid, ACP_FAKESHOT, 12, "bullet origin far from player");
		return false;
	}

	new Float:range = ACP_WeaponRange(weaponid);
	if(range > 0.0 && hittype != BULLET_HIT_TYPE_NONE && ACP_Dist3D(ox, oy, oz, hx, hy, hz) > range * 1.35 + 5.0)
	{
		ACP_Flag(playerid, ACP_FAKESHOT, 10, "hit beyond weapon range");
		return false;
	}

	switch(hittype)
	{
		case BULLET_HIT_TYPE_PLAYER:
		{
			if(hitid < 0 || hitid >= MAX_PLAYERS || !IsPlayerConnected(hitid) || hitid == playerid)
			{
				ACP_Flag(playerid, ACP_FAKESHOT, 20, "invalid hit player");
				return false;
			}
			if(VectorSize(fX, fY, fZ) > 3.0)
			{
				ACP_Flag(playerid, ACP_SILENTAIM, 15, "hit offset outside player body");
				return false;
			}
			new Float:tx, Float:ty, Float:tz;
			GetPlayerPos(hitid, tx, ty, tz);
			if(range > 0.0 && ACP_Dist3D(px, py, pz, tx, ty, tz) > range * 1.5 + 8.0)
			{
				ACP_Flag(playerid, ACP_SILENTAIM, 12, "target out of weapon range");
				return false;
			}
		}
		case BULLET_HIT_TYPE_VEHICLE:
		{
			if(hitid <= 0 || hitid >= MAX_VEHICLES || GetVehicleModel(hitid) == 0 || VectorSize(fX, fY, fZ) > 30.0)
			{
				ACP_Flag(playerid, ACP_FAKESHOT, 15, "invalid hit vehicle");
				return false;
			}
		}
	}
	return true;
}

static ACP_OnGiveDamage(playerid, damagedid, Float:amount, weaponid, bodypart)
{
	if(gACP_Exempt[playerid] || gACP_Punished[playerid]) return;
	if(damagedid != INVALID_PLAYER_ID && (damagedid < 0 || damagedid >= MAX_PLAYERS || !IsPlayerConnected(damagedid)))
	{
		ACP_Flag(playerid, ACP_DAMAGE, 15, "damage to invalid player");
		return;
	}
	if(bodypart < 3 || bodypart > 9) ACP_Flag(playerid, ACP_DAMAGE, 6, "invalid bodypart");
	new Float:maxd = ACP_MaxDamage(weaponid);
	if(maxd > 0.0 && amount > maxd + 0.5) ACP_Flag(playerid, ACP_DAMAGE, 12, "damage above weapon maximum");
}

static bool:ACP_OnDeath(playerid, &killerid, reason)
{
	if(gACP_Exempt[playerid]) return true;
	if(gACP_Dead[playerid])
	{
		ACP_Flag(playerid, ACP_FAKEKILL, 30, "died twice without spawning");
		return false;
	}
	gACP_Dead[playerid] = true;
	gACP_AllowSpawn[playerid] = true;

	if(reason < 0 || (reason > 54 && reason != 255))
	{
		ACP_Flag(playerid, ACP_FAKEKILL, 20, "invalid death reason");
		killerid = INVALID_PLAYER_ID;
		return true;
	}
	if(killerid != INVALID_PLAYER_ID)
	{
		if(killerid < 0 || killerid >= MAX_PLAYERS || !IsPlayerConnected(killerid) || killerid == playerid)
		{
			ACP_Flag(playerid, ACP_FAKEKILL, 20, "invalid killer");
			killerid = INVALID_PLAYER_ID;
			return true;
		}
		new Float:range = ACP_WeaponRange(reason);
		if(range > 0.0)
		{
			new Float:kx, Float:ky, Float:kz, Float:vx, Float:vy, Float:vz;
			GetPlayerPos(killerid, kx, ky, kz);
			GetPlayerPos(playerid, vx, vy, vz);
			if(ACP_Dist3D(kx, ky, kz, vx, vy, vz) > range * 1.6 + 10.0)
			{
				ACP_Flag(playerid, ACP_FAKEKILL, 20, "killer out of weapon range");
				killerid = INVALID_PLAYER_ID;
			}
		}
	}
	return true;
}

// ---------------------------------------------------------------------------
//  Native hooks (state tracking). Disabled with ACP_NO_NATIVE_HOOKS.
// ---------------------------------------------------------------------------
#if defined ACP_HOOKS

stock ACP_SetPlayerPos(playerid, Float:x, Float:y, Float:z)
{
	ACP_AllowTeleport(playerid, 2000);
	return SetPlayerPos(playerid, x, y, z);
}
stock ACP_SetPlayerPosFindZ(playerid, Float:x, Float:y, Float:z)
{
	ACP_AllowTeleport(playerid, 2000);
	return SetPlayerPosFindZ(playerid, x, y, z);
}
stock ACP_SetPlayerInterior(playerid, interiorid)
{
	ACP_AllowTeleport(playerid, 2000);
	return SetPlayerInterior(playerid, interiorid);
}
stock ACP_PutPlayerInVehicle(playerid, vehicleid, seatid)
{
	ACP_AllowTeleport(playerid, 2500);
	return PutPlayerInVehicle(playerid, vehicleid, seatid);
}
stock ACP_RemovePlayerFromVehicle(playerid)
{
	ACP_AllowTeleport(playerid, 1500);
	return RemovePlayerFromVehicle(playerid);
}
stock ACP_SetVehiclePos(vehicleid, Float:x, Float:y, Float:z)
{
	new pool = GetPlayerPoolSize();
	for(new i; i <= pool; i++) if(IsPlayerConnected(i) && GetPlayerVehicleID(i) == vehicleid) ACP_AllowTeleport(i, 2500);
	if(vehicleid > 0 && vehicleid < MAX_VEHICLES) gACP_VehHealth[vehicleid] = -1.0;
	return SetVehiclePos(vehicleid, x, y, z);
}
stock ACP_SpawnPlayer(playerid)
{
	gACP_AllowSpawn[playerid] = true;
	ACP_AllowTeleport(playerid, 3000);
	return SpawnPlayer(playerid);
}
stock ACP_TogglePlayerSpectating(playerid, toggle)
{
	if(!toggle) gACP_AllowSpawn[playerid] = true;
	ACP_AllowTeleport(playerid, 3000);
	return TogglePlayerSpectating(playerid, toggle);
}
stock ACP_SetPlayerHealth(playerid, Float:health)
{
	if(playerid >= 0 && playerid < MAX_PLAYERS) { gACP_Health[playerid] = health; gACP_HealthGrace[playerid] = GetTickCount() + 2500; }
	return SetPlayerHealth(playerid, health);
}
stock ACP_SetPlayerArmour(playerid, Float:armour)
{
	if(playerid >= 0 && playerid < MAX_PLAYERS) { gACP_Armour[playerid] = armour; gACP_HealthGrace[playerid] = GetTickCount() + 2500; }
	return SetPlayerArmour(playerid, armour);
}
stock ACP_SetVehicleHealth(vehicleid, Float:health)
{
	if(vehicleid > 0 && vehicleid < MAX_VEHICLES) gACP_VehHealth[vehicleid] = health;
	return SetVehicleHealth(vehicleid, health);
}
stock ACP_RepairVehicle(vehicleid)
{
	if(vehicleid > 0 && vehicleid < MAX_VEHICLES) gACP_VehHealth[vehicleid] = 1000.0;
	return RepairVehicle(vehicleid);
}
stock ACP_GivePlayerWeapon(playerid, weaponid, ammo)
{
	if(playerid >= 0 && playerid < MAX_PLAYERS && weaponid > 0 && weaponid < 47) gACP_Weapon[playerid][weaponid] = true;
	return GivePlayerWeapon(playerid, weaponid, ammo);
}
stock ACP_ResetPlayerWeapons(playerid)
{
	if(playerid >= 0 && playerid < MAX_PLAYERS) for(new i; i < 47; i++) gACP_Weapon[playerid][i] = false;
	return ResetPlayerWeapons(playerid);
}
stock ACP_GivePlayerMoney(playerid, money)
{
	if(playerid >= 0 && playerid < MAX_PLAYERS) gACP_Money[playerid] += money;
	return GivePlayerMoney(playerid, money);
}
stock ACP_ResetPlayerMoney(playerid)
{
	if(playerid >= 0 && playerid < MAX_PLAYERS) gACP_Money[playerid] = 0;
	return ResetPlayerMoney(playerid);
}
stock ACP_SetPlayerSpecialAction(playerid, actionid)
{
	if(playerid >= 0 && playerid < MAX_PLAYERS) gACP_JetpackOK[playerid] = (actionid == SPECIAL_ACTION_USEJETPACK);
	return SetPlayerSpecialAction(playerid, actionid);
}
stock ACP_ShowPlayerDialog(playerid, dialogid, style, const caption[], const info[], const button1[], const button2[])
{
	if(playerid >= 0 && playerid < MAX_PLAYERS) gACP_Dialog[playerid] = (dialogid >= 0) ? dialogid : -1;
	return ShowPlayerDialog(playerid, dialogid, style, caption, info, button1, button2);
}

#endif

// ---------------------------------------------------------------------------
//  Callback hooks (ALS pattern, chains with your own callbacks)
// ---------------------------------------------------------------------------
#if defined FILTERSCRIPT
public OnFilterScriptInit()
{
	ACP_Init();
	#if defined ACP_OnFilterScriptInit
		return ACP_OnFilterScriptInit();
	#else
		return 1;
	#endif
}
#if defined _ALS_OnFilterScriptInit
	#undef OnFilterScriptInit
#else
	#define _ALS_OnFilterScriptInit
#endif
#define OnFilterScriptInit ACP_OnFilterScriptInit
#if defined ACP_OnFilterScriptInit
	forward ACP_OnFilterScriptInit();
#endif
#else
public OnGameModeInit()
{
	ACP_Init();
	#if defined ACP_OnGameModeInit
		return ACP_OnGameModeInit();
	#else
		return 1;
	#endif
}
#if defined _ALS_OnGameModeInit
	#undef OnGameModeInit
#else
	#define _ALS_OnGameModeInit
#endif
#define OnGameModeInit ACP_OnGameModeInit
#if defined ACP_OnGameModeInit
	forward ACP_OnGameModeInit();
#endif
#endif

public OnPlayerConnect(playerid)
{
	ACP_ResetPlayer(playerid);
	#if defined ACP_OnPlayerConnect
		return ACP_OnPlayerConnect(playerid);
	#else
		return 1;
	#endif
}
#if defined _ALS_OnPlayerConnect
	#undef OnPlayerConnect
#else
	#define _ALS_OnPlayerConnect
#endif
#define OnPlayerConnect ACP_OnPlayerConnect
#if defined ACP_OnPlayerConnect
	forward ACP_OnPlayerConnect(playerid);
#endif

public OnPlayerDisconnect(playerid, reason)
{
	ACP_ResetPlayer(playerid);
	#if defined ACP_OnPlayerDisconnect
		return ACP_OnPlayerDisconnect(playerid, reason);
	#else
		return 1;
	#endif
}
#if defined _ALS_OnPlayerDisconnect
	#undef OnPlayerDisconnect
#else
	#define _ALS_OnPlayerDisconnect
#endif
#define OnPlayerDisconnect ACP_OnPlayerDisconnect
#if defined ACP_OnPlayerDisconnect
	forward ACP_OnPlayerDisconnect(playerid, reason);
#endif

public OnPlayerSpawn(playerid)
{
	#if defined ACP_HOOKS
	new now = GetTickCount();
	if(!gACP_Requested[playerid] && !gACP_AllowSpawn[playerid]) ACP_Flag(playerid, ACP_FAKESPAWN, 20, "spawn without request");
	for(new i; i < 47; i++) gACP_Weapon[playerid][i] = false;
	gACP_WeaponSnap[playerid] = now + 2000;
	gACP_HealthGrace[playerid] = now + 3000;
	gACP_Money[playerid] = GetPlayerMoney(playerid);
	#endif
	gACP_Requested[playerid] = false;
	gACP_AllowSpawn[playerid] = false;
	gACP_Dead[playerid] = false;
	gACP_InModShop[playerid] = false;
	ACP_AllowTeleport(playerid, 3000);
	#if defined ACP_OnPlayerSpawn
		return ACP_OnPlayerSpawn(playerid);
	#else
		return 1;
	#endif
}
#if defined _ALS_OnPlayerSpawn
	#undef OnPlayerSpawn
#else
	#define _ALS_OnPlayerSpawn
#endif
#define OnPlayerSpawn ACP_OnPlayerSpawn
#if defined ACP_OnPlayerSpawn
	forward ACP_OnPlayerSpawn(playerid);
#endif

public OnPlayerDeath(playerid, killerid, reason)
{
	if(!ACP_OnDeath(playerid, killerid, reason)) return 1;
	#if defined ACP_OnPlayerDeath
		return ACP_OnPlayerDeath(playerid, killerid, reason);
	#else
		return 1;
	#endif
}
#if defined _ALS_OnPlayerDeath
	#undef OnPlayerDeath
#else
	#define _ALS_OnPlayerDeath
#endif
#define OnPlayerDeath ACP_OnPlayerDeath
#if defined ACP_OnPlayerDeath
	forward ACP_OnPlayerDeath(playerid, killerid, reason);
#endif

public OnPlayerUpdate(playerid)
{
	if(!ACP_OnUpdate(playerid)) return 0;
	#if defined ACP_OnPlayerUpdate
		return ACP_OnPlayerUpdate(playerid);
	#else
		return 1;
	#endif
}
#if defined _ALS_OnPlayerUpdate
	#undef OnPlayerUpdate
#else
	#define _ALS_OnPlayerUpdate
#endif
#define OnPlayerUpdate ACP_OnPlayerUpdate
#if defined ACP_OnPlayerUpdate
	forward ACP_OnPlayerUpdate(playerid);
#endif

public OnPlayerStateChange(playerid, newstate, oldstate)
{
	if(!gACP_Exempt[playerid] && (newstate == PLAYER_STATE_DRIVER || newstate == PLAYER_STATE_PASSENGER) && oldstate == PLAYER_STATE_ONFOOT)
	{
		new veh = GetPlayerVehicleID(playerid);
		if(veh > 0 && gACP_HavePos[playerid] && GetTickCount() > gACP_Grace[playerid])
		{
			new Float:vx, Float:vy, Float:vz;
			GetVehiclePos(veh, vx, vy, vz);
			if(ACP_Dist3D(vx, vy, vz, gACP_SafePos[playerid][0], gACP_SafePos[playerid][1], gACP_SafePos[playerid][2]) > 25.0)
				ACP_Flag(playerid, ACP_VEHICLE_TELEPORT, 30, "teleported into vehicle");
		}
	}
	ACP_AllowTeleport(playerid, 800);
	#if defined ACP_OnPlayerStateChange
		return ACP_OnPlayerStateChange(playerid, newstate, oldstate);
	#else
		return 1;
	#endif
}
#if defined _ALS_OnPlayerStateChange
	#undef OnPlayerStateChange
#else
	#define _ALS_OnPlayerStateChange
#endif
#define OnPlayerStateChange ACP_OnPlayerStateChange
#if defined ACP_OnPlayerStateChange
	forward ACP_OnPlayerStateChange(playerid, newstate, oldstate);
#endif

public OnPlayerWeaponShot(playerid, weaponid, hittype, hitid, Float:fX, Float:fY, Float:fZ)
{
	if(!ACP_OnShot(playerid, weaponid, hittype, hitid, fX, fY, fZ)) return 0;
	#if defined ACP_OnPlayerWeaponShot
		return ACP_OnPlayerWeaponShot(playerid, weaponid, hittype, hitid, fX, fY, fZ);
	#else
		return 1;
	#endif
}
#if defined _ALS_OnPlayerWeaponShot
	#undef OnPlayerWeaponShot
#else
	#define _ALS_OnPlayerWeaponShot
#endif
#define OnPlayerWeaponShot ACP_OnPlayerWeaponShot
#if defined ACP_OnPlayerWeaponShot
	forward ACP_OnPlayerWeaponShot(playerid, weaponid, hittype, hitid, Float:fX, Float:fY, Float:fZ);
#endif

public OnPlayerGiveDamage(playerid, damagedid, Float:amount, weaponid, bodypart)
{
	ACP_OnGiveDamage(playerid, damagedid, amount, weaponid, bodypart);
	#if defined ACP_OnPlayerGiveDamage
		return ACP_OnPlayerGiveDamage(playerid, damagedid, amount, weaponid, bodypart);
	#else
		return 1;
	#endif
}
#if defined _ALS_OnPlayerGiveDamage
	#undef OnPlayerGiveDamage
#else
	#define _ALS_OnPlayerGiveDamage
#endif
#define OnPlayerGiveDamage ACP_OnPlayerGiveDamage
#if defined ACP_OnPlayerGiveDamage
	forward ACP_OnPlayerGiveDamage(playerid, damagedid, Float:amount, weaponid, bodypart);
#endif

public OnPlayerEnterVehicle(playerid, vehicleid, ispassenger)
{
	if(!gACP_Exempt[playerid])
	{
		if(ACP_RateLimit(playerid, ACP_RL_ENTER, 6, 2000)) return 1;
		new Float:vx, Float:vy, Float:vz, Float:px, Float:py, Float:pz;
		GetVehiclePos(vehicleid, vx, vy, vz);
		GetPlayerPos(playerid, px, py, pz);
		if(GetVehicleModel(vehicleid) == 0 || ACP_Dist3D(vx, vy, vz, px, py, pz) > 30.0)
		{
			ACP_Flag(playerid, ACP_ENTER_VEHICLE, 15, "enter vehicle from far away");
			return 1;
		}
	}
	#if defined ACP_OnPlayerEnterVehicle
		return ACP_OnPlayerEnterVehicle(playerid, vehicleid, ispassenger);
	#else
		return 1;
	#endif
}
#if defined _ALS_OnPlayerEnterVehicle
	#undef OnPlayerEnterVehicle
#else
	#define _ALS_OnPlayerEnterVehicle
#endif
#define OnPlayerEnterVehicle ACP_OnPlayerEnterVehicle
#if defined ACP_OnPlayerEnterVehicle
	forward ACP_OnPlayerEnterVehicle(playerid, vehicleid, ispassenger);
#endif

public OnPlayerExitVehicle(playerid, vehicleid)
{
	if(!gACP_Exempt[playerid] && ACP_RateLimit(playerid, ACP_RL_EXIT, 8, 2000)) return 1;
	#if defined ACP_OnPlayerExitVehicle
		return ACP_OnPlayerExitVehicle(playerid, vehicleid);
	#else
		return 1;
	#endif
}
#if defined _ALS_OnPlayerExitVehicle
	#undef OnPlayerExitVehicle
#else
	#define _ALS_OnPlayerExitVehicle
#endif
#define OnPlayerExitVehicle ACP_OnPlayerExitVehicle
#if defined ACP_OnPlayerExitVehicle
	forward ACP_OnPlayerExitVehicle(playerid, vehicleid);
#endif

public OnUnoccupiedVehicleUpdate(vehicleid, playerid, passenger_seat, Float:new_x, Float:new_y, Float:new_z, Float:vel_x, Float:vel_y, Float:vel_z)
{
	if(!gACP_Exempt[playerid])
	{
		new Float:px, Float:py, Float:pz;
		GetPlayerPos(playerid, px, py, pz);
		if(ACP_Dist3D(new_x, new_y, new_z, px, py, pz) > 40.0 || VectorSize(vel_x, vel_y, vel_z) > 3.0)
		{
			ACP_Flag(playerid, ACP_VEHICLE_TELEPORT, 15, "unoccupied vehicle sync");
			return 0;
		}
	}
	#if defined ACP_OnUnoccupiedVehicleUpdate
		return ACP_OnUnoccupiedVehicleUpdate(vehicleid, playerid, passenger_seat, new_x, new_y, new_z, vel_x, vel_y, vel_z);
	#else
		return 1;
	#endif
}
#if defined _ALS_OnUnoccupiedVehicleUpd
	#undef OnUnoccupiedVehicleUpdate
#else
	#define _ALS_OnUnoccupiedVehicleUpd
#endif
#define OnUnoccupiedVehicleUpdate ACP_OnUnoccupiedVehicleUpdate
#if defined ACP_OnUnoccupiedVehicleUpdate
	forward ACP_OnUnoccupiedVehicleUpdate(vehicleid, playerid, passenger_seat, Float:new_x, Float:new_y, Float:new_z, Float:vel_x, Float:vel_y, Float:vel_z);
#endif

public OnVehicleSpawn(vehicleid)
{
	if(vehicleid > 0 && vehicleid < MAX_VEHICLES) gACP_VehHealth[vehicleid] = -1.0;
	#if defined ACP_OnVehicleSpawn
		return ACP_OnVehicleSpawn(vehicleid);
	#else
		return 1;
	#endif
}
#if defined _ALS_OnVehicleSpawn
	#undef OnVehicleSpawn
#else
	#define _ALS_OnVehicleSpawn
#endif
#define OnVehicleSpawn ACP_OnVehicleSpawn
#if defined ACP_OnVehicleSpawn
	forward ACP_OnVehicleSpawn(vehicleid);
#endif

public OnVehicleDeath(vehicleid, killerid)
{
	if(vehicleid > 0 && vehicleid < MAX_VEHICLES) gACP_VehHealth[vehicleid] = -1.0;
	#if defined ACP_OnVehicleDeath
		return ACP_OnVehicleDeath(vehicleid, killerid);
	#else
		return 1;
	#endif
}
#if defined _ALS_OnVehicleDeath
	#undef OnVehicleDeath
#else
	#define _ALS_OnVehicleDeath
#endif
#define OnVehicleDeath ACP_OnVehicleDeath
#if defined ACP_OnVehicleDeath
	forward ACP_OnVehicleDeath(vehicleid, killerid);
#endif

public OnEnterExitModShop(playerid, enterexit, interiorid)
{
	gACP_InModShop[playerid] = (enterexit != 0);
	ACP_AllowTeleport(playerid, 3000);
	new veh = GetPlayerVehicleID(playerid);
	if(veh > 0 && veh < MAX_VEHICLES) gACP_VehHealth[veh] = -1.0;
	#if defined ACP_OnEnterExitModShop
		return ACP_OnEnterExitModShop(playerid, enterexit, interiorid);
	#else
		return 1;
	#endif
}
#if defined _ALS_OnEnterExitModShop
	#undef OnEnterExitModShop
#else
	#define _ALS_OnEnterExitModShop
#endif
#define OnEnterExitModShop ACP_OnEnterExitModShop
#if defined ACP_OnEnterExitModShop
	forward ACP_OnEnterExitModShop(playerid, enterexit, interiorid);
#endif

public OnDialogResponse(playerid, dialogid, response, listitem, inputtext[])
{
	if(!gACP_Exempt[playerid])
	{
		if(ACP_RateLimit(playerid, ACP_RL_DIALOG, 10, 2000)) return 1;
		#if defined ACP_DIALOG_CHECK && defined ACP_HOOKS
		if(gACP_Dialog[playerid] == -1 || gACP_Dialog[playerid] != dialogid)
		{
			ACP_Flag(playerid, ACP_DIALOG, 25, "response to a dialog that was not shown");
			return 1;
		}
		gACP_Dialog[playerid] = -1;
		#endif
	}
	// neutralise '%' so unsafe format() calls in scripts cannot be abused
	new safe[129];
	for(new i; i < 128; i++)
	{
		safe[i] = (inputtext[i] == '%') ? '#' : inputtext[i];
		if(!inputtext[i]) break;
	}
	#if defined ACP_OnDialogResponse
		return ACP_OnDialogResponse(playerid, dialogid, response, listitem, safe);
	#else
		return 0;
	#endif
}
#if defined _ALS_OnDialogResponse
	#undef OnDialogResponse
#else
	#define _ALS_OnDialogResponse
#endif
#define OnDialogResponse ACP_OnDialogResponse
#if defined ACP_OnDialogResponse
	forward ACP_OnDialogResponse(playerid, dialogid, response, listitem, inputtext[]);
#endif

public OnPlayerText(playerid, text[])
{
	if(!gACP_Exempt[playerid] && ACP_RateLimit(playerid, ACP_RL_TEXT, 6, 3000)) return 0;
	#if defined ACP_OnPlayerText
		return ACP_OnPlayerText(playerid, text);
	#else
		return 1;
	#endif
}
#if defined _ALS_OnPlayerText
	#undef OnPlayerText
#else
	#define _ALS_OnPlayerText
#endif
#define OnPlayerText ACP_OnPlayerText
#if defined ACP_OnPlayerText
	forward ACP_OnPlayerText(playerid, text[]);
#endif

public OnPlayerCommandText(playerid, cmdtext[])
{
	if(!gACP_Exempt[playerid] && ACP_RateLimit(playerid, ACP_RL_CMD, 8, 3000)) return 1;
	#if defined ACP_OnPlayerCommandText
		return ACP_OnPlayerCommandText(playerid, cmdtext);
	#else
		return 0;
	#endif
}
#if defined _ALS_OnPlayerCommandText
	#undef OnPlayerCommandText
#else
	#define _ALS_OnPlayerCommandText
#endif
#define OnPlayerCommandText ACP_OnPlayerCommandText
#if defined ACP_OnPlayerCommandText
	forward ACP_OnPlayerCommandText(playerid, cmdtext[]);
#endif

public OnPlayerRequestSpawn(playerid)
{
	if(!gACP_Exempt[playerid] && ACP_RateLimit(playerid, ACP_RL_SPAWNREQ, 4, 3000)) return 0;
	gACP_Requested[playerid] = true;
	#if defined ACP_OnPlayerRequestSpawn
		return ACP_OnPlayerRequestSpawn(playerid);
	#else
		return 1;
	#endif
}
#if defined _ALS_OnPlayerRequestSpawn
	#undef OnPlayerRequestSpawn
#else
	#define _ALS_OnPlayerRequestSpawn
#endif
#define OnPlayerRequestSpawn ACP_OnPlayerRequestSpawn
#if defined ACP_OnPlayerRequestSpawn
	forward ACP_OnPlayerRequestSpawn(playerid);
#endif

public OnPlayerRequestClass(playerid, classid)
{
	if(!gACP_Exempt[playerid] && ACP_RateLimit(playerid, ACP_RL_CLASS, 15, 2000)) return 1;
	#if defined ACP_OnPlayerRequestClass
		return ACP_OnPlayerRequestClass(playerid, classid);
	#else
		return 1;
	#endif
}
#if defined _ALS_OnPlayerRequestClass
	#undef OnPlayerRequestClass
#else
	#define _ALS_OnPlayerRequestClass
#endif
#define OnPlayerRequestClass ACP_OnPlayerRequestClass
#if defined ACP_OnPlayerRequestClass
	forward ACP_OnPlayerRequestClass(playerid, classid);
#endif

public OnPlayerPickUpPickup(playerid, pickupid)
{
	if(!gACP_Exempt[playerid] && ACP_RateLimit(playerid, ACP_RL_PICKUP, 25, 1000)) return 1;
	#if defined ACP_HOOKS
	gACP_WeaponSnap[playerid] = GetTickCount() + 1500;   // game-given pickup weapons are re-learned
	#endif
	#if defined ACP_OnPlayerPickUpPickup
		return ACP_OnPlayerPickUpPickup(playerid, pickupid);
	#else
		return 1;
	#endif
}
#if defined _ALS_OnPlayerPickUpPickup
	#undef OnPlayerPickUpPickup
#else
	#define _ALS_OnPlayerPickUpPickup
#endif
#define OnPlayerPickUpPickup ACP_OnPlayerPickUpPickup
#if defined ACP_OnPlayerPickUpPickup
	forward ACP_OnPlayerPickUpPickup(playerid, pickupid);
#endif

public OnIncomingConnection(playerid, ip_address[], port)
{
	if(ACP_IPHit(0, ip_address, 5000) > 6)
	{
		printf("[ACPlus] connection flood from %s, blocking for 2 minutes", ip_address);
		BlockIpAddress(ip_address, 120000);
	}
	#if defined ACP_OnIncomingConnection
		return ACP_OnIncomingConnection(playerid, ip_address, port);
	#else
		return 1;
	#endif
}
#if defined _ALS_OnIncomingConnection
	#undef OnIncomingConnection
#else
	#define _ALS_OnIncomingConnection
#endif
#define OnIncomingConnection ACP_OnIncomingConnection
#if defined ACP_OnIncomingConnection
	forward ACP_OnIncomingConnection(playerid, ip_address[], port);
#endif

public OnRconLoginAttempt(ip[], password[], success)
{
	if(!success && ACP_IPHit(1, ip, 300000) >= 4)
	{
		printf("[ACPlus] RCON brute force from %s, blocking for 1 hour", ip);
		BlockIpAddress(ip, 3600000);
	}
	#if defined ACP_OnRconLoginAttempt
		return ACP_OnRconLoginAttempt(ip, password, success);
	#else
		return 1;
	#endif
}
#if defined _ALS_OnRconLoginAttempt
	#undef OnRconLoginAttempt
#else
	#define _ALS_OnRconLoginAttempt
#endif
#define OnRconLoginAttempt ACP_OnRconLoginAttempt
#if defined ACP_OnRconLoginAttempt
	forward ACP_OnRconLoginAttempt(ip[], password[], success);
#endif

// ---------------------------------------------------------------------------
//  Native hook macros - MUST stay at the very end of the file
// ---------------------------------------------------------------------------
#if defined ACP_HOOKS

#if defined _ALS_SetPlayerPos
	#undef SetPlayerPos
#else
	#define _ALS_SetPlayerPos
#endif
#define SetPlayerPos ACP_SetPlayerPos

#if defined _ALS_SetPlayerPosFindZ
	#undef SetPlayerPosFindZ
#else
	#define _ALS_SetPlayerPosFindZ
#endif
#define SetPlayerPosFindZ ACP_SetPlayerPosFindZ

#if defined _ALS_SetPlayerInterior
	#undef SetPlayerInterior
#else
	#define _ALS_SetPlayerInterior
#endif
#define SetPlayerInterior ACP_SetPlayerInterior

#if defined _ALS_PutPlayerInVehicle
	#undef PutPlayerInVehicle
#else
	#define _ALS_PutPlayerInVehicle
#endif
#define PutPlayerInVehicle ACP_PutPlayerInVehicle

#if defined _ALS_RemovePlayerFromVehicle
	#undef RemovePlayerFromVehicle
#else
	#define _ALS_RemovePlayerFromVehicle
#endif
#define RemovePlayerFromVehicle ACP_RemovePlayerFromVehicle

#if defined _ALS_SetVehiclePos
	#undef SetVehiclePos
#else
	#define _ALS_SetVehiclePos
#endif
#define SetVehiclePos ACP_SetVehiclePos

#if defined _ALS_SpawnPlayer
	#undef SpawnPlayer
#else
	#define _ALS_SpawnPlayer
#endif
#define SpawnPlayer ACP_SpawnPlayer

#if defined _ALS_TogglePlayerSpectating
	#undef TogglePlayerSpectating
#else
	#define _ALS_TogglePlayerSpectating
#endif
#define TogglePlayerSpectating ACP_TogglePlayerSpectating

#if defined _ALS_SetPlayerHealth
	#undef SetPlayerHealth
#else
	#define _ALS_SetPlayerHealth
#endif
#define SetPlayerHealth ACP_SetPlayerHealth

#if defined _ALS_SetPlayerArmour
	#undef SetPlayerArmour
#else
	#define _ALS_SetPlayerArmour
#endif
#define SetPlayerArmour ACP_SetPlayerArmour

#if defined _ALS_SetVehicleHealth
	#undef SetVehicleHealth
#else
	#define _ALS_SetVehicleHealth
#endif
#define SetVehicleHealth ACP_SetVehicleHealth

#if defined _ALS_RepairVehicle
	#undef RepairVehicle
#else
	#define _ALS_RepairVehicle
#endif
#define RepairVehicle ACP_RepairVehicle

#if defined _ALS_GivePlayerWeapon
	#undef GivePlayerWeapon
#else
	#define _ALS_GivePlayerWeapon
#endif
#define GivePlayerWeapon ACP_GivePlayerWeapon

#if defined _ALS_ResetPlayerWeapons
	#undef ResetPlayerWeapons
#else
	#define _ALS_ResetPlayerWeapons
#endif
#define ResetPlayerWeapons ACP_ResetPlayerWeapons

#if defined _ALS_GivePlayerMoney
	#undef GivePlayerMoney
#else
	#define _ALS_GivePlayerMoney
#endif
#define GivePlayerMoney ACP_GivePlayerMoney

#if defined _ALS_ResetPlayerMoney
	#undef ResetPlayerMoney
#else
	#define _ALS_ResetPlayerMoney
#endif
#define ResetPlayerMoney ACP_ResetPlayerMoney

#if defined _ALS_SetPlayerSpecialAction
	#undef SetPlayerSpecialAction
#else
	#define _ALS_SetPlayerSpecialAction
#endif
#define SetPlayerSpecialAction ACP_SetPlayerSpecialAction

#if defined _ALS_ShowPlayerDialog
	#undef ShowPlayerDialog
#else
	#define _ALS_ShowPlayerDialog
#endif
#define ShowPlayerDialog ACP_ShowPlayerDialog

#endif
