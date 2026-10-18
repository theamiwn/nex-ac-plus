#include <a_samp>
#include <nex-ac-plus>

main() {}

public OnGameModeInit()
{
	AddPlayerClass(0, 1958.33, 1343.12, 15.36, 269.15, 0, 0, 0, 0, 0, 0);
	return 1;
}

// Optional: alert admins on every violation (warn = 1 once score >= ACP_SCORE_WARN)
forward OnACPlusFlag(playerid, check, points, score, warn);
public OnACPlusFlag(playerid, check, points, score, warn)
{
	if(warn) SendClientMessageToAll(0xFFAA00FF, "[AC] A player is being flagged by the anti-cheat.");
	return 1;
}

// Optional: decide the punishment yourself. Return 0 to skip the default kick.
forward OnACPlusPunish(playerid, check, score);
public OnACPlusPunish(playerid, check, score)
{
	// e.g. write to a ban database here, then:
	return 1; // 1 = default delayed kick
}

public OnPlayerRequestSpawn(playerid)
{
	SetPlayerPos(playerid, 1958.33, 1343.12, 15.36); // goes through the hook automatically
	GivePlayerWeapon(playerid, WEAPON_DEAGLE, 100);
	return 1;
}
