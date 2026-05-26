//
// Copyright (c) 2003-2009, by Yet Another POD-Bot Development Team.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include "../include/core.h"

constexpr int kMaxSemiclipClients = 32;
constexpr int kMaxPhysents = 600;
constexpr float kCrouchHeightDelta = 35.9f;
constexpr float kZombieUnstuckPairTime = 0.25f;
constexpr float kZombieUnstuckOverlapTime = 1.0f;
constexpr float kZombieUnstuckOverlapMaxGap = 0.5f;

/*
// Enables built-in semiclip
// 0 = Disabled
// 1 = All players can semiclip with each other
// 2 = Only bots can semiclip with other bots
// 3 = Bots can semiclip with bots and real players, but real players cannot semiclip with other real players
ebot_semiclip

// Specifies which teams use built-in semiclip
// 0 = All players can semiclip with each other
// 1 = Only Terrorists can semiclip with Terrorists
// 2 = Only Counter-Terrorists can semiclip with Counter-Terrorists
// 3 = Only players on the same team can semiclip
ebot_semiclip_team

// Specifies which team can be marked as ducking for duck boost
// 0 = All teams
// 1 = Terrorists only
// 2 = Counter-Terrorists only
ebot_duck_boost_team
*/



ConVar ebot_semiclip("ebot_semiclip", "0");
ConVar ebot_semiclip_team("ebot_semiclip_team", "3");
ConVar ebot_duck_boost("ebot_duck_boost", "1");
ConVar ebot_duck_boost_team("ebot_duck_boost_team", "0");
ConVar ebot_semiclip_entities("ebot_semiclip_entities", "0");

extern ConVar ebot_has_semiclip;
extern ConVar ebot_debug;

struct entity_state_s
{
	int entityType;
	int number;
	float msg_time;
	int messagenum;
	Vector origin;
	Vector angles;
	int modelindex;
	int sequence;
	float frame;
	int colormap;
	short skin;
	short solid;
	int effects;
	float scale;
	uint8_t eflags;
	int rendermode;
	int renderamt;
	color24 rendercolor;
	int renderfx;
};

struct physent_s
{
	char name[32];
	int player;
	Vector origin;
	void *model;
	void *studiomodel;
	Vector mins;
	Vector maxs;
	int info;
	Vector angles;
	int solid;
	int skin;
	int rendermode;
	float frame;
	int sequence;
	uint8_t controller[4];
	uint8_t blending[2];
	int movetype;
	int takedamage;
	int blooddecal;
	int team;
	int classnumber;
	int iuser1;
	int iuser2;
	int iuser3;
	int iuser4;
	float fuser1;
	float fuser2;
	float fuser3;
	float fuser4;
	Vector vuser1;
	Vector vuser2;
	Vector vuser3;
	Vector vuser4;
};

struct playermove_s
{
	int player_index;
	qboolean server;
	qboolean multiplayer;
	float time;
	float frametime;
	Vector forward;
	Vector right;
	Vector up;
	Vector origin;
	Vector angles;
	Vector oldangles;
	Vector velocity;
	Vector movedir;
	Vector basevelocity;
	Vector view_ofs;
	float flDuckTime;
	qboolean bInDuck;
	int flTimeStepSound;
	int iStepLeft;
	float flFallVelocity;
	Vector punchangle;
	float flSwimTime;
	float flNextPrimaryAttack;
	int effects;
	int flags;
	int usehull;
	float gravity;
	float friction;
	int oldbuttons;
	float waterjumptime;
	qboolean dead;
	int deadflag;
	int spectator;
	int movetype;
	int onground;
	int waterlevel;
	int watertype;
	int oldwaterlevel;
	char sztexturename[256];
	char chtexturetype;
	float maxspeed;
	float clientmaxspeed;
	int iuser1;
	int iuser2;
	int iuser3;
	int iuser4;
	float fuser1;
	float fuser2;
	float fuser3;
	float fuser4;
	Vector vuser1;
	Vector vuser2;
	Vector vuser3;
	Vector vuser4;
	int numphysent;
	physent_s physents[kMaxPhysents];
};

struct SemiclipPlayer
{
	uint32_t activeMask{0};
	bool boostSolid[kMaxSemiclipClients + 1];
	float distanceSq[kMaxSemiclipClients + 1];
};

static SemiclipPlayer g_semiclip[kMaxSemiclipClients + 1];
static float g_zombieUnstuckPairUntil[kMaxSemiclipClients + 1][kMaxSemiclipClients + 1];
static float g_zombieUnstuckOverlapFrom[kMaxSemiclipClients + 1][kMaxSemiclipClients + 1];
static float g_zombieUnstuckOverlapSeen[kMaxSemiclipClients + 1][kMaxSemiclipClients + 1];
static DLL_FUNCTIONS *g_semiclipPreHookTable = nullptr;
static DLL_FUNCTIONS *g_semiclipPostHookTable = nullptr;
static enginefuncs_t *g_semiclipEnginePostHookTable = nullptr;
static bool g_semiclipHooksEnabled = true;
static int g_semiclipMode = 1;
static int g_semiclipTeamMode = 3;
static bool g_semiclipDuckBoostEnabled = true;
static int g_semiclipDuckBoostTeam = 0;
static bool g_semiclipEntitiesEnabled = false;


void SemiclipTraceLine_Post(const float *v1, const float *v2, int fNoMonsters, edict_t *pentToSkip, TraceResult *ptr);

void SemiclipSetPreHookTable(DLL_FUNCTIONS *functionTable)
{
	g_semiclipPreHookTable = functionTable;

	if (g_semiclipHooksEnabled && g_semiclipPreHookTable)
		g_semiclipPreHookTable->pfnPM_Move = SemiclipPMMove;
}

void SemiclipSetPostHookTable(DLL_FUNCTIONS *functionTable)
{
	g_semiclipPostHookTable = functionTable;

	if (g_semiclipHooksEnabled && g_semiclipPostHookTable)
		g_semiclipPostHookTable->pfnAddToFullPack = SemiclipAddToFullPack_Post;
}

void SemiclipSetEnginePostHookTable(enginefuncs_t *functionTable)
{
	g_semiclipEnginePostHookTable = functionTable;

	if (g_semiclipHooksEnabled && g_semiclipEnginePostHookTable)
		g_semiclipEnginePostHookTable->pfnTraceLine = SemiclipTraceLine_Post;
}

void SemiclipUpdateHooks(void)
{
	g_semiclipMode = !ebot_semiclip.m_eptr ? 1 : cclamp(ebot_semiclip.GetInt(), 0, 3);
	g_semiclipHooksEnabled = g_semiclipMode > 0;

	if (g_semiclipPreHookTable)
		g_semiclipPreHookTable->pfnPM_Move = g_semiclipHooksEnabled ? SemiclipPMMove : nullptr;

	if (g_semiclipPostHookTable)
		g_semiclipPostHookTable->pfnAddToFullPack = g_semiclipHooksEnabled ? SemiclipAddToFullPack_Post : nullptr;

	if (g_semiclipEnginePostHookTable)
		g_semiclipEnginePostHookTable->pfnTraceLine = g_semiclipHooksEnabled ? SemiclipTraceLine_Post : nullptr;
}

static void SemiclipCacheSettings(void)
{
	g_semiclipTeamMode = cclamp(ebot_semiclip_team.GetInt(), 0, 3);
	g_semiclipDuckBoostEnabled = cclamp(ebot_duck_boost.GetInt(), 0, 1) == 1;
	g_semiclipDuckBoostTeam = cclamp(ebot_duck_boost_team.GetInt(), 0, 2);
	g_semiclipEntitiesEnabled = cclamp(ebot_semiclip_entities.GetInt(), 0, 1) == 1;
}

void SemiclipUpdateSettings(void)
{
	SemiclipCacheSettings();
	SemiclipUpdateHooks();
}

bool SemiclipHooksEnabled(void)
{
	return g_semiclipHooksEnabled;
}

bool SemiclipDuckBoostEnabled(void)
{
	return g_semiclipDuckBoostEnabled;
}

bool SemiclipDuckBoostAllowedForTeam(const int team)
{
	if (!g_semiclipDuckBoostEnabled)
		return false;

	switch (g_semiclipDuckBoostTeam)
	{
	case 0:
		return team == Team::Terrorist || team == Team::Counter;

	case 1:
		return team == Team::Terrorist;

	case 2:
		return team == Team::Counter;

	default:
		return false;
	}
}

static bool IsSemiclipTeamAllowed(const int hostTeam, const int otherTeam)
{
	switch (g_semiclipTeamMode)
	{
	case 0:
		return true;

	case 1:
		return hostTeam == Team::Terrorist && otherTeam == Team::Terrorist;

	case 2:
		return hostTeam == Team::Counter && otherTeam == Team::Counter;

	case 3:
	default:
		return hostTeam == otherTeam;
	}
}

static bool IsSemiclipModeAllowed(const bool hostBot, const bool otherBot)
{
	switch (g_semiclipMode)
	{
	case 1:
		return true;

	case 2:
		return hostBot && otherBot;

	case 3:
		return hostBot || otherBot;

	default:
		return false;
	}
}

static bool IsValidPlayerIndex(const int index)
{
	return index > 0 && index <= g_maxClients;
}

static uint32_t GetSemiclipPlayerBit(const int index)
{
	if (index <= 0 || index > g_maxClients)
		return 0;

	return uint32_t(1) << (index - 1);
}

static bool HasSemiclipActive(const int hostIndex, const int otherIndex)
{
	return (g_semiclip[hostIndex].activeMask & GetSemiclipPlayerBit(otherIndex)) != 0;
}

static void SetSemiclipActive(const int hostIndex, const int otherIndex)
{
	g_semiclip[hostIndex].activeMask |= GetSemiclipPlayerBit(otherIndex);
}

static bool ArePlayerBoundsTouching(edict_t *first, edict_t *second)
{
	constexpr float touchEpsilon = 1.0f;

	return first->v.absmin.x <= second->v.absmax.x + touchEpsilon &&
		first->v.absmax.x + touchEpsilon >= second->v.absmin.x &&
		first->v.absmin.y <= second->v.absmax.y + touchEpsilon &&
		first->v.absmax.y + touchEpsilon >= second->v.absmin.y &&
		first->v.absmin.z <= second->v.absmax.z + touchEpsilon &&
		first->v.absmax.z + touchEpsilon >= second->v.absmin.z;
}

static bool ArePlayerBoundsOverlapping(edict_t *first, edict_t *second)
{
	constexpr float overlapMin = 2.0f;

	return first->v.absmin.x + overlapMin < second->v.absmax.x &&
		first->v.absmax.x - overlapMin > second->v.absmin.x &&
		first->v.absmin.y + overlapMin < second->v.absmax.y &&
		first->v.absmax.y - overlapMin > second->v.absmin.y &&
		first->v.absmin.z + overlapMin < second->v.absmax.z &&
		first->v.absmax.z - overlapMin > second->v.absmin.z;
}

static void ActivateZombieUnstuckPair(const int firstIndex, const int secondIndex, const float time)
{
	const float until = time + kZombieUnstuckPairTime;
	g_zombieUnstuckPairUntil[firstIndex][secondIndex] = until;
	g_zombieUnstuckPairUntil[secondIndex][firstIndex] = until;
}

static void ResetZombieUnstuckOverlapPair(const int firstIndex, const int secondIndex)
{
	g_zombieUnstuckOverlapFrom[firstIndex][secondIndex] = 0.0f;
	g_zombieUnstuckOverlapFrom[secondIndex][firstIndex] = 0.0f;
	g_zombieUnstuckOverlapSeen[firstIndex][secondIndex] = 0.0f;
	g_zombieUnstuckOverlapSeen[secondIndex][firstIndex] = 0.0f;
}

static bool UpdateZombieUnstuckOverlapPair(const int firstIndex, const int secondIndex, const float time)
{
	float &overlapFrom = g_zombieUnstuckOverlapFrom[firstIndex][secondIndex];
	float &overlapSeen = g_zombieUnstuckOverlapSeen[firstIndex][secondIndex];

	if (overlapFrom <= 0.0f || time - overlapSeen > kZombieUnstuckOverlapMaxGap)
		overlapFrom = time;

	overlapSeen = time;
	g_zombieUnstuckOverlapFrom[secondIndex][firstIndex] = overlapFrom;
	g_zombieUnstuckOverlapSeen[secondIndex][firstIndex] = overlapSeen;

	return time - overlapFrom >= kZombieUnstuckOverlapTime;
}

static bool HasZombieUnstuckPair(const int firstIndex, const int secondIndex, const float time)
{
	return g_zombieUnstuckPairUntil[firstIndex][secondIndex] > time;
}

static bool HasTouchingSemiclipPlayer(edict_t *player, const int playerIndex)
{
	for (int otherIndex = 1; otherIndex <= g_maxClients; ++otherIndex)
	{
		if (!HasSemiclipActive(playerIndex, otherIndex) || otherIndex == playerIndex)
			continue;

		edict_t *other = INDEXENT(otherIndex);
		if (!IsValidPlayer(other) || !IsAlive(other))
			continue;

		if (ArePlayerBoundsTouching(player, other))
			return true;
	}

	return false;
}

/// <summary>
/// check semiclip conditions before disabling it beetween players who were previously in semiclip
/// (due to a bug that causes the player to fly through the ceiling)
/// </summary>
/// <param name="host"></param>
/// <param name="hostIndex"></param>
/// <param name="hostTeam"></param>
static void CheckChangedSemiclipConditions(edict_t *host, const int hostIndex, const int hostTeam, const float time, const bool refreshExisting)
{
	uint32_t activeMask = g_semiclip[hostIndex].activeMask;
	while (activeMask)
	{
		const int otherIndex = cctz(activeMask) + 1;
		activeMask &= activeMask - 1;

		const bool hasUnstuckPair = HasZombieUnstuckPair(hostIndex, otherIndex, time);
		if (hasUnstuckPair && !refreshExisting)
			continue;

		edict_t *other = INDEXENT(otherIndex);
		if (!IsValidPlayer(other) || !IsAlive(other))
			continue;

		if (IsSemiclipTeamAllowed(hostTeam, GetCachedPlayerTeam(otherIndex)))
			continue;

		if (!ArePlayerBoundsTouching(host, other))
			continue;

		if (ebot_debug.GetBool() && !hasUnstuckPair)
		{
			char firstName[32];
			char secondName[32];
			cstrncpy(firstName, GetEntityName(host), sizeof(firstName));
			cstrncpy(secondName, GetEntityName(other), sizeof(secondName));

			ServerPrint("CheckChangedSemiclipConditions for %s and %s, enable semiclip for unstuck",
				firstName, secondName);
		}

		ActivateZombieUnstuckPair(hostIndex, otherIndex, time);
	}
}

void SemiclipCheckChangedConditions(edict_t *host, const int hostIndex, const int hostTeam, const float time)
{
	if (!g_semiclipHooksEnabled)
		return;

	if (!IsValidPlayerIndex(hostIndex) || FNullEnt(host))
		return;

	CheckChangedSemiclipConditions(host, hostIndex, hostTeam, time, false);
}

void SemiclipRefreshChangedConditions(edict_t *host, const int hostIndex, const int hostTeam, const float time)
{
	if (!g_semiclipHooksEnabled)
		return;

	if (!IsValidPlayerIndex(hostIndex) || FNullEnt(host))
		return;

	CheckChangedSemiclipConditions(host, hostIndex, hostTeam, time, true);
}

static bool IsAttackButtonActive(edict_t *player, const Bot *bot)
{
	const int attackButtons = IN_ATTACK | IN_ATTACK2;
	if (bot)
		return (bot->m_buttons & attackButtons) || (bot->m_oldButtons & attackButtons);

	return !FNullEnt(player) &&
		((player->v.buttons & attackButtons) || (player->v.oldbuttons & attackButtons));
}

static bool IsKnifeWeaponEquipped(edict_t *player, const Bot *bot)
{
	if (bot)
		return bot->m_currentWeapon == Weapon::Knife;

	const int playerIndex = ENTINDEX(player);
	return playerIndex > 0 && playerIndex < 33 &&
		g_playerCurrentWeapon[playerIndex] == Weapon::Knife;
}

static bool IntersectTraceBounds(const Vector &start, const Vector &end, const Vector &mins, const Vector &maxs, float *fraction, Vector *normal)
{
	const Vector delta = end - start;
	float enterFraction = 0.0f;
	float exitFraction = 1.0f;
	Vector enterNormal = nullvec;

	for (int axis = 0; axis < 3; ++axis)
	{
		const float startValue = axis == 0 ? start.x : axis == 1 ? start.y : start.z;
		const float deltaValue = axis == 0 ? delta.x : axis == 1 ? delta.y : delta.z;
		const float minValue = axis == 0 ? mins.x : axis == 1 ? mins.y : mins.z;
		const float maxValue = axis == 0 ? maxs.x : axis == 1 ? maxs.y : maxs.z;

		if (cabsf(deltaValue) < 0.0001f)
		{
			if (startValue < minValue || startValue > maxValue)
				return false;

			continue;
		}

		float axisEnter;
		float axisExit;
		Vector axisNormal = nullvec;

		if (deltaValue > 0.0f)
		{
			axisEnter = (minValue - startValue) / deltaValue;
			axisExit = (maxValue - startValue) / deltaValue;
			if (axis == 0)
				axisNormal.x = -1.0f;
			else if (axis == 1)
				axisNormal.y = -1.0f;
			else
				axisNormal.z = -1.0f;
		}
		else
		{
			axisEnter = (maxValue - startValue) / deltaValue;
			axisExit = (minValue - startValue) / deltaValue;
			if (axis == 0)
				axisNormal.x = 1.0f;
			else if (axis == 1)
				axisNormal.y = 1.0f;
			else
				axisNormal.z = 1.0f;
		}

		if (axisEnter > enterFraction)
		{
			enterFraction = axisEnter;
			enterNormal = axisNormal;
		}

		if (axisExit < exitFraction)
			exitFraction = axisExit;

		if (enterFraction > exitFraction)
			return false;
	}

	if (exitFraction < 0.0f || enterFraction > 1.0f)
		return false;

	if (fraction)
		*fraction = cclampf(enterFraction, 0.0f, 1.0f);

	if (normal)
		*normal = enterNormal;

	return true;
}

static bool BuildKnifeTraceEntityResult(edict_t *target, const Vector &start, const Vector &end, TraceResult *trace, float *fraction = nullptr)
{
	if (FNullEnt(target))
		return false;

	Vector mins = target->v.absmin;
	Vector maxs = target->v.absmax;

	if (mins.IsNull() && maxs.IsNull())
	{
		const Vector center = GetEntityOrigin(target);
		mins = center - Vector(8.0f, 8.0f, 8.0f);
		maxs = center + Vector(8.0f, 8.0f, 8.0f);
	}

	constexpr float targetExpand = 4.0f;
	mins = mins - Vector(targetExpand, targetExpand, targetExpand);
	maxs = maxs + Vector(targetExpand, targetExpand, targetExpand);

	float hitFraction = 1.0f;
	Vector normal = nullvec;
	if (!IntersectTraceBounds(start, end, mins, maxs, &hitFraction, &normal))
		return false;

	trace->fAllSolid = false;
	trace->fStartSolid = false;
	trace->flFraction = hitFraction;
	trace->vecEndPos = start + (end - start) * hitFraction;
	trace->flPlaneDist = 0.0f;
	trace->vecPlaneNormal = normal;
	trace->pHit = target;
	trace->iHitgroup = 0;

	if (fraction)
		*fraction = hitFraction;

	return true;
}

static bool IsKnifeTraceBlockedBySemiclipPlayer(edict_t *attacker, const int attackerIndex, edict_t *hit)
{
	if (FNullEnt(attacker) || FNullEnt(hit) || attacker == hit || !IsValidPlayer(hit) || !IsAlive(hit))
		return false;

	const int hitIndex = ENTINDEX(hit);
	return IsValidPlayerIndex(attackerIndex) && IsValidPlayerIndex(hitIndex) &&
		HasSemiclipActive(attackerIndex, hitIndex) &&
		GetCachedPlayerTeam(attackerIndex) == GetCachedPlayerTeam(hitIndex);
}

static bool IsKnifeTraceReplacementPlayer(edict_t *attacker, edict_t *target, edict_t *blocker)
{
	if (FNullEnt(target) || target == attacker || target == blocker ||
		!IsValidPlayer(target) || !IsAlive(target))
		return false;

	const int attackerIndex = ENTINDEX(attacker);
	const int targetIndex = ENTINDEX(target);
	return IsValidPlayerIndex(attackerIndex) && IsValidPlayerIndex(targetIndex) &&
		GetCachedPlayerTeam(targetIndex) != GetCachedPlayerTeam(attackerIndex);
}

static bool FindKnifeTraceBlockingSemiclipPlayer(edict_t *attacker, const int attackerIndex, const Vector &start, const Vector &end,
	const float maxFraction, edict_t **blocker, float *blockerFraction)
{
	constexpr float blockerEpsilon = 0.001f;

	if (FNullEnt(attacker) || !blocker || !blockerFraction || !IsValidPlayerIndex(attackerIndex))
		return false;

	edict_t *bestBlocker = nullptr;
	float bestBlockerFraction = cclampf(maxFraction, 0.0f, 1.0f);
	if (bestBlockerFraction <= 0.0f)
		return false;

	for (int targetIndex = 1; targetIndex <= g_maxClients; ++targetIndex)
	{
		edict_t *target = INDEXENT(targetIndex);
		if (!IsKnifeTraceBlockedBySemiclipPlayer(attacker, attackerIndex, target))
			continue;

		TraceResult candidateTrace{};
		float candidateFraction = 1.0f;
		if (!BuildKnifeTraceEntityResult(target, start, end, &candidateTrace, &candidateFraction))
			continue;

		if (candidateFraction + blockerEpsilon >= bestBlockerFraction)
			continue;

		bestBlocker = target;
		bestBlockerFraction = candidateFraction;
	}

	if (FNullEnt(bestBlocker))
		return false;

	*blocker = bestBlocker;
	*blockerFraction = bestBlockerFraction;
	return true;
}

static bool IsKnifeTraceReplacementEntity(edict_t *attacker, edict_t *target, edict_t *blocker)
{
	if (FNullEnt(target) || target == attacker || target == blocker)
		return false;

	if (IsValidPlayer(target))
		return IsKnifeTraceReplacementPlayer(attacker, target, blocker);

	return IsBreakableTarget(target);
}

static bool FindKnifeTraceReplacementTarget(edict_t *attacker, edict_t *blocker, const Vector &start, const Vector &end,
	const float blockerFraction, const float maxFraction, TraceResult *trace)
{
	constexpr float blockerEpsilon = 0.001f;

	edict_t *bestTarget = nullptr;
	float bestFraction = cclampf(maxFraction, 0.0f, 1.0f);
	if (bestFraction <= blockerFraction + blockerEpsilon)
		return false;

	for (int targetIndex = 1; targetIndex <= g_maxClients; ++targetIndex)
	{
		edict_t *target = INDEXENT(targetIndex);
		if (!IsKnifeTraceReplacementPlayer(attacker, target, blocker))
			continue;

		TraceResult candidateTrace{};
		float candidateFraction = 1.0f;
		if (!BuildKnifeTraceEntityResult(target, start, end, &candidateTrace, &candidateFraction))
			continue;

		if (candidateFraction <= blockerFraction + blockerEpsilon || candidateFraction >= bestFraction)
			continue;

		bestTarget = target;
		bestFraction = candidateFraction;
		*trace = candidateTrace;
	}

	const Vector center = (start + end) * 0.5f;
	const float radius = (end - start).GetLength() * 0.5f + 32.0f;
	edict_t *search = nullptr;
	while (!FNullEnt(search = FIND_ENTITY_IN_SPHERE(search, center, radius)))
	{
		if (!IsKnifeTraceReplacementEntity(attacker, search, blocker))
			continue;

		const int index = ENTINDEX(search);
		if (index > 0 && index <= g_maxClients)
			continue;

		TraceResult candidateTrace{};
		float candidateFraction = 1.0f;
		if (!BuildKnifeTraceEntityResult(search, start, end, &candidateTrace, &candidateFraction))
			continue;

		if (candidateFraction <= blockerFraction + blockerEpsilon || candidateFraction >= bestFraction)
			continue;

		bestTarget = search;
		bestFraction = candidateFraction;
		*trace = candidateTrace;
	}

	return !FNullEnt(bestTarget);
}

static bool IsHumanBreakableAttackTrace(edict_t *attacker, Bot *bot)
{
	if (FNullEnt(attacker) || !bot || bot->m_isZombieBot || !bot->m_isAlive)
		return false;

	if (bot->GetCurrentState() != Process::DestroyBreakable || !bot->DestroyBreakableReq())
		return false;

	if (!IsAttackButtonActive(attacker, bot))
		return false;

	return true;
}

static bool TraceHitsBreakableIgnoringPlayers(const Vector &start, const Vector &end, edict_t *attacker, edict_t *breakable, TraceResult *trace)
{
	if (FNullEnt(breakable) || !trace)
		return false;

	TraceResult candidateTrace{};
	g_engfuncs.pfnTraceLine(start, end, 1, attacker, &candidateTrace);

	if (FNullEnt(candidateTrace.pHit) || candidateTrace.pHit != breakable)
		return false;

	*trace = candidateTrace;
	return true;
}

static bool FindHumanBreakableAttackTarget(const Vector &start, const Vector &end, edict_t *attacker, Bot *bot, TraceResult *trace)
{
	if (TraceHitsBreakableIgnoringPlayers(start, end, attacker, bot->m_breakableEntity, trace))
		return true;

	if (bot->m_currentWeapon != Weapon::Knife)
		return false;

	return BuildKnifeTraceEntityResult(bot->m_breakableEntity, start, end, trace);
}

static bool RedirectHumanBreakableAttackTrace(edict_t *attacker, Bot *bot, const Vector &start, const Vector &end, TraceResult *trace)
{
	if (!IsHumanBreakableAttackTrace(attacker, bot))
		return false;

	if (FNullEnt(bot->m_breakableEntity) || !IsBreakableTarget(bot->m_breakableEntity))
		return false;

	if (!FNullEnt(trace->pHit) && trace->pHit == bot->m_breakableEntity)
		return false;

	const Vector eyePosition = attacker->v.origin + attacker->v.view_ofs;
	if ((start - eyePosition).GetLengthSquared() > squaredf(96.0f) &&
		(start - attacker->v.origin).GetLengthSquared() > squaredf(96.0f))
		return false;

	const float traceLengthSq = (end - start).GetLengthSquared();
	if (traceLengthSq < squaredf(32.0f))
		return false;

	const int attackerIndex = ENTINDEX(attacker);
	if (!IsValidPlayerIndex(attackerIndex) || !HasTouchingSemiclipPlayer(attacker, attackerIndex))
		return false;

	TraceResult breakableTrace{};
	if (!FindHumanBreakableAttackTarget(start, end, attacker, bot, &breakableTrace))
		return false;

	edict_t *blockedTeammate = nullptr;
	float blockedFraction = breakableTrace.flFraction;

	if (trace->flFraction < 1.0f && !FNullEnt(trace->pHit) &&
		IsKnifeTraceBlockedBySemiclipPlayer(attacker, attackerIndex, trace->pHit))
	{
		blockedTeammate = trace->pHit;
		blockedFraction = trace->flFraction;
	}
	else if (!FindKnifeTraceBlockingSemiclipPlayer(attacker, attackerIndex, start, end,
		breakableTrace.flFraction, &blockedTeammate, &blockedFraction))
		return false;

	if (FNullEnt(blockedTeammate) || blockedFraction + 0.001f >= breakableTrace.flFraction)
		return false;

	*trace = breakableTrace;
	return true;
}

static bool ShouldKeepSolidForBoost(edict_t *host, edict_t *other, const int hostIndex, const int otherIndex)
{
	if (!g_semiclipDuckBoostEnabled)
		return false;

	if (!g_playerDucking[hostIndex] && !g_playerDucking[otherIndex])
		return false;

	// Slow StartFrame polling caches both DJUMP waypoint and long duck-only states.
	const bool hostLower = host->v.origin.z <= other->v.origin.z;
	const int lowerIndex = hostLower ? hostIndex : otherIndex;
	if (!g_playerDucking[lowerIndex])
		return false;

	// Measure vertical separation and identify which player is currently lower.
	const float zDelta = cabsf(host->v.origin.z - other->v.origin.z);

	// If players are nearly at the same height, they are not in a boost setup anymore.
	// Clear any remembered boost state and allow semiclip again.
	if (zDelta < kCrouchHeightDelta)
	{
		g_semiclip[hostIndex].boostSolid[otherIndex] = false;
		g_semiclip[otherIndex].boostSolid[hostIndex] = false;
		return false;
	}

	// A crouching lower player with another player above is enough to treat the pair as a boost setup.
	// Remember that state for both directions and keep them solid.
	edict_t* lower = hostLower ? host : other;
	const bool lowerDucking = (lower->v.buttons & IN_DUCK || lower->v.flags & FL_DUCKING);
	if (lowerDucking)
	{
		g_semiclip[hostIndex].boostSolid[otherIndex] = true;
		g_semiclip[otherIndex].boostSolid[hostIndex] = true;
		return true;
	}

	// Keep players solid while one is standing on the other, or while a previously detected
	// boost state is still active for this pair.
	return host->v.groundentity == other || other->v.groundentity == host ||
		g_semiclip[hostIndex].boostSolid[otherIndex] ||
		g_semiclip[otherIndex].boostSolid[hostIndex];
}

static bool ShouldSemiclipPlayer(edict_t *host, const int hostIndex, const int otherIndex, const int hostTeam, const bool hostBot, const float time)
{
	edict_t* other = INDEXENT(otherIndex);
	if (FNullEnt(other))
		return false;

	if (!other->pvPrivateData || other->v.deadflag || other->v.health <= 0.0f)  
		return false;

	const bool otherBot = IsValidBot(otherIndex-1);
	if (!IsSemiclipModeAllowed(hostBot, otherBot))
		return false;

	const bool zombieUnstuckPair = HasZombieUnstuckPair(hostIndex, otherIndex, time);

	const int otherTeam = GetCachedPlayerTeam(otherIndex);

	if (!zombieUnstuckPair && !IsSemiclipTeamAllowed(hostTeam, otherTeam))
		return false;

	constexpr float maxDistanceSq = 64.0f * 64.0f;
	const float dx = host->v.origin.x - other->v.origin.x;
	const float dy = host->v.origin.y - other->v.origin.y;
	const float distanceSq = dx * dx + dy * dy;
	
	if (distanceSq > maxDistanceSq)
		return false;

	g_semiclip[hostIndex].distanceSq[otherIndex] = distanceSq;

	if (!zombieUnstuckPair && ShouldKeepSolidForBoost(host, other, hostIndex, otherIndex))
		return false;

	return true;
}

static bool ShouldSemiclipEntity(edict_t *host, const physent_s &entity)
{
	if (entity.info == 0 || entity.info >= Const_MaxEntities)
		return false;

	const int targetMask = g_entityTargetMask[entity.info];
	if (!targetMask)
		return false;

	const int requiredTarget = IsZombieEntity(host) ? EnemyEntityTarget_ZombieBots : EnemyEntityTarget_HumanBots;
	return !(targetMask & requiredTarget);
}


void SemiclipUpdateUnstuckOverlapFromTouch(edict_t* first, edict_t* second)
{
	if (!IsValidPlayer(first) || !IsValidPlayer(second))
		return;

	const int firstIndex = ENTINDEX(first);
	const int secondIndex = ENTINDEX(second);

	if (!IsValidPlayerIndex(firstIndex) || !IsValidPlayerIndex(secondIndex))
		return;

	if (!IsAlive(first) || !IsAlive(second))
		return;

	if (!ArePlayerBoundsOverlapping(first, second))
	{
		ResetZombieUnstuckOverlapPair(firstIndex, secondIndex);
		return;
	}

	const int firstTeam = GetCachedPlayerTeam(firstIndex);
	const int secondTeam = GetCachedPlayerTeam(secondIndex);
	if (IsSemiclipTeamAllowed(firstTeam, secondTeam))
	{
		ResetZombieUnstuckOverlapPair(firstIndex, secondIndex);
		return;
	}

	const float time = engine->GetTime();
	if (UpdateZombieUnstuckOverlapPair(firstIndex, secondIndex, time))
	{
		ActivateZombieUnstuckPair(firstIndex, secondIndex, time);

			if (ebot_debug.GetBool())
			{
				char firstName[32];
				char secondName[32];
				cstrncpy(firstName, GetEntityName(first), sizeof(firstName));
				cstrncpy(secondName, GetEntityName(second), sizeof(secondName));

				ServerPrint("Semiclip touch unstuck activated for %s and %s after %.1fs overlap",
					firstName, secondName, kZombieUnstuckOverlapTime);
			}
		}
	}

void SemiclipReset(void)
{
	cmemset(g_semiclip, 0, sizeof(g_semiclip));
	cmemset(g_zombieUnstuckPairUntil, 0, sizeof(g_zombieUnstuckPairUntil));
	cmemset(g_zombieUnstuckOverlapFrom, 0, sizeof(g_zombieUnstuckOverlapFrom));
	cmemset(g_zombieUnstuckOverlapSeen, 0, sizeof(g_zombieUnstuckOverlapSeen));
	cmemset(g_playerDucking, 0, sizeof(g_playerDucking));
	cmemset(g_playerDuckingFrom, 0, sizeof(g_playerDuckingFrom));
	cmemset(g_entityTargetMask, 0, sizeof(g_entityTargetMask));

	SemiclipUpdateSettings();

	if (g_semiclipHooksEnabled)
		ebot_has_semiclip.SetInt(1);
}

void SemiclipPMMove(playermove_s *pmove, qboolean /*server*/)
{
	if (!g_semiclipHooksEnabled || !pmove || pmove->spectator || pmove->dead || pmove->deadflag != DEAD_NO)
		RETURN_META(MRES_IGNORED);

	const int hostIndex = pmove->player_index + 1;
	if(hostIndex > g_maxClients) 
		RETURN_META(MRES_IGNORED);
	edict_t* host = INDEXENT(hostIndex);
	if (FNullEnt(host))
		RETURN_META(MRES_IGNORED);

	const int hostTeam = GetCachedPlayerTeam(hostIndex);
	const float time = engine->GetTime();

	g_semiclip[hostIndex].activeMask = 0;

	const bool hostBot = IsValidBot(hostIndex - 1);
	const bool allowPlayerSemiclip = g_semiclipMode != 2 || hostBot;
	if (!allowPlayerSemiclip && !g_semiclipEntitiesEnabled)
		RETURN_META(MRES_IGNORED);
		
	bool hasAnySkip = false;
	int writeIndex = 0;

	//the engine sets numphysent at a distance of around 400 units
	for (int readIndex = 0; readIndex < pmove->numphysent && readIndex < kMaxPhysents; ++readIndex)
	{
		const int otherIndex = pmove->physents[readIndex].player; //if 0 then it is not a player

		if (allowPlayerSemiclip && otherIndex > 0 && otherIndex <= g_maxClients && otherIndex != hostIndex &&
			ShouldSemiclipPlayer(host, hostIndex, otherIndex, hostTeam, hostBot, time))
		{
			SetSemiclipActive(hostIndex, otherIndex);
			hasAnySkip = true;
			continue;
		}
		else if(otherIndex == 0 && g_semiclipEntitiesEnabled && ShouldSemiclipEntity(host, pmove->physents[readIndex]))
		{
			hasAnySkip = true;
			continue;
		}

		if (writeIndex != readIndex)
			pmove->physents[writeIndex] = pmove->physents[readIndex];

		++writeIndex;
	}

	if (!hasAnySkip)
		RETURN_META(MRES_IGNORED);

	pmove->numphysent = writeIndex;
	RETURN_META(MRES_IGNORED);
}

int SemiclipAddToFullPack_Post(entity_state_s *state, int e, edict_t *ent, edict_t *host, int /*hostflags*/, int player, uint8_t * /*pSet*/)
{
	if (!g_semiclipHooksEnabled || !state || !player || ent == host)
		RETURN_META_VALUE(MRES_IGNORED, 0);

	const int hostIndex = ENTINDEX(host);
	const int entIndex = e > 0 ? e : ENTINDEX(ent);

	if (hostIndex <= 0 || hostIndex > g_maxClients || entIndex <= 0 || entIndex > g_maxClients)
		RETURN_META_VALUE(MRES_IGNORED, 0);

	if (HasSemiclipActive(hostIndex, entIndex))
	{
		state->solid = SOLID_NOT;

		if (g_playerDucking[entIndex])
		{
			state->rendermode = kRenderNormal;
			state->renderfx = kRenderFxGlowShell;
			state->renderamt = 16;
			state->rendercolor.r = 255;
			state->rendercolor.g = 128;
			state->rendercolor.b = 0;
			RETURN_META_VALUE(MRES_IGNORED, 0);
		}

		state->rendermode = kRenderTransTexture;

		constexpr float invisDistSq = 10.0f * 10.0f;
		if(g_semiclip[hostIndex].distanceSq[entIndex] > invisDistSq)
			state->renderamt = 130; //A value lower than 130 makes some models completely invisible... (tested on client patch v43 and steam client 2026)
		else
			state->renderamt = 0;
	}
	else if(g_playerDucking[entIndex] && g_clients[hostIndex -1].team == g_clients[entIndex - 1].team)
	{		
		state->rendermode = kRenderNormal;
		state->renderfx = kRenderFxGlowShell;
		state->renderamt = 16;
		state->rendercolor.r = 255;
		state->rendercolor.g = 128;
		state->rendercolor.b = 0;
	}


	RETURN_META_VALUE(MRES_IGNORED, 0);
}

/// <summary>
/// Fixes attacks during semiclip when an attacking player is inside another player.
/// Zombie knife traces can be redirected to enemies/breakables behind teammates.
/// Human bot weapon traces are only redirected to their current DestroyBreakable target.
/// </summary>
void SemiclipTraceLine_Post(const float* v1, const float* v2, int /*fNoMonsters*/, edict_t* pentToSkip, TraceResult* ptr)
{
	// Only handle live players while semiclip is enabled and the engine returned a valid trace.
	if (!g_semiclipHooksEnabled || !ptr || FNullEnt(pentToSkip) || !IsValidPlayer(pentToSkip) || !IsAlive(pentToSkip))
		RETURN_META(MRES_IGNORED);

	const Vector start(v1[0], v1[1], v1[2]);
	const Vector end(v2[0], v2[1], v2[2]);

	// Bots must also still be alive from the bot manager point of view.
	Bot *bot = g_botManager->GetBot(pentToSkip);
	if (bot && !bot->m_isAlive)
		RETURN_META(MRES_IGNORED);

	if (RedirectHumanBreakableAttackTrace(pentToSkip, bot, start, end, ptr))
	{
		/*
		if (ebot_debug.GetBool() && !FNullEnt(ptr->pHit))
		{
			char attackerName[32];
			char redirectedName[32];
			cstrncpy(attackerName, GetEntityName(pentToSkip), sizeof(attackerName));
			cstrncpy(redirectedName, GetEntityName(ptr->pHit), sizeof(redirectedName));

			ServerPrint("%s breakable attack trace redirected through semiclip teammate to %s",
				attackerName, redirectedName);
		}
		*/

		RETURN_META(MRES_IGNORED);
	}

	if (!IsZombieEntity(pentToSkip))
		RETURN_META(MRES_IGNORED);

	// This fix is only relevant for knife attacks.
	if (!IsKnifeWeaponEquipped(pentToSkip, bot))
		RETURN_META(MRES_IGNORED);

	// Ignore traces that are unrelated to an actual attack input.
	if (!IsAttackButtonActive(pentToSkip, bot))
		RETURN_META(MRES_IGNORED);

	const Vector eyePosition = pentToSkip->v.origin + pentToSkip->v.view_ofs;

	// Only consider short traces that originate near the attacker.
	if ((start - eyePosition).GetLengthSquared() > squaredf(64.0f) &&
		(start - pentToSkip->v.origin).GetLengthSquared() > squaredf(64.0f))
		RETURN_META(MRES_IGNORED);

	const float traceLengthSq = (end - start).GetLengthSquared();
	if (traceLengthSq < squaredf(8.0f) || traceLengthSq > squaredf(96.0f))
		RETURN_META(MRES_IGNORED);

	// The redirect only matters when the attacker is currently intersecting a semiclip teammate.
	const int attackerIndex = ENTINDEX(pentToSkip);
	if (!IsValidPlayerIndex(attackerIndex) || !HasTouchingSemiclipPlayer(pentToSkip, attackerIndex))
		RETURN_META(MRES_IGNORED);

	edict_t *blockedTeammate = nullptr;
	float blockedFraction = ptr->flFraction;
	float replacementMaxFraction = 1.0f;
	bool redirectedFromMiss = false;

	// If the engine trace directly hit a semiclip teammate, use that player as the blocker.
	if (ptr->flFraction < 1.0f && !FNullEnt(ptr->pHit) &&
		IsKnifeTraceBlockedBySemiclipPlayer(pentToSkip, attackerIndex, ptr->pHit))
	{
		blockedTeammate = ptr->pHit;
	}
	else
	{
		// If the trace already hit a valid enemy or breakable entity, leave the original result intact.
		if (ptr->flFraction < 1.0f && !FNullEnt(ptr->pHit) &&
			IsKnifeTraceReplacementEntity(pentToSkip, ptr->pHit, nullptr))
			RETURN_META(MRES_IGNORED);

		// Otherwise treat the original trace as a miss and search for the closest semiclip teammate
		// intersected by the knife segment before the original trace endpoint.
		replacementMaxFraction = cclampf(ptr->flFraction, 0.0f, 1.0f);
		if (!FindKnifeTraceBlockingSemiclipPlayer(pentToSkip, attackerIndex, start, end,
			replacementMaxFraction, &blockedTeammate, &blockedFraction))
			RETURN_META(MRES_IGNORED);

		redirectedFromMiss = true;
	}

	// Nothing to redirect through if no blocking teammate was found.
	if (FNullEnt(blockedTeammate))
		RETURN_META(MRES_IGNORED);

	// Search for the first valid target behind the blocking teammate and overwrite the trace result.
	if (FindKnifeTraceReplacementTarget(pentToSkip, blockedTeammate, start, end, blockedFraction, replacementMaxFraction, ptr))
	{
		/*
		if (ebot_debug.GetBool() && !FNullEnt(ptr->pHit) && !bot)
		{
		// Debug output distinguishes between a direct teammate hit redirect and a corrected miss.
		char attackerName[32];
		char blockedName[32];
		char redirectedName[32];
		cstrncpy(attackerName, GetEntityName(pentToSkip), sizeof(attackerName));
		cstrncpy(blockedName, GetEntityName(blockedTeammate), sizeof(blockedName));
		cstrncpy(redirectedName, GetEntityName(ptr->pHit), sizeof(redirectedName));

		if (redirectedFromMiss)
		{
			ServerPrint("%s knife miss corrected through teammate %s to %s",
				attackerName, blockedName, redirectedName);
		}
		else
		{
			ServerPrint("%s knife trace redirected from teammate %s to %s",
				attackerName, blockedName, redirectedName);
		}
		}
		*/
	}

	RETURN_META(MRES_IGNORED);
}
