#include "AutoWall.h"

#define    HITGROUP_GENERIC    0
#define    HITGROUP_HEAD        1
#define    HITGROUP_CHEST        2
#define    HITGROUP_STOMACH    3
#define HITGROUP_LEFTARM    4    
#define HITGROUP_RIGHTARM    5
#define HITGROUP_LEFTLEG    6
#define HITGROUP_RIGHTLEG    7
#define HITGROUP_GEAR        10

inline bool CGameTrace::DidHitWorld() const
{
	return m_pEnt->GetIndex() == 0;
}

inline bool CGameTrace::DidHitNonWorldEntity() const
{
	return m_pEnt != NULL && !DidHitWorld();
}

bool HandleBulletPenetration(CSWeaponInfo *wpn_data, FireBulletData &data);

float GetHitgroupDamageMult(int iHitGroup)
{
	switch (iHitGroup)
	{
	case HITGROUP_GENERIC:
		return 1.f;
	case HITGROUP_HEAD:
		return 4.f;
	case HITGROUP_CHEST:
		return 1.f;
	case HITGROUP_STOMACH:
		return 1.25f;
	case HITGROUP_LEFTARM:
		return 1.f;
	case HITGROUP_RIGHTARM:
		return 1.f;
	case HITGROUP_LEFTLEG:
		return 0.75f;
	case HITGROUP_RIGHTLEG:
		return 0.75f;
	case HITGROUP_GEAR:
		return 1.f;
	default:
		break;

	}

	return 1.0f;
}

void ScaleDamage(int hitgroup, IClientEntity *enemy, float weapon_armor_ratio, float &current_damage)
{
	
	auto ArmorValue = enemy->ArmorValue(); // DT_CSPlayer -> m_ArmorValue
		current_damage *= GetHitgroupDamageMult(hitgroup);


		switch (hitgroup)
		{
		case HITGROUP_HEAD:
			current_damage *= 4.f;
			break;
		case HITGROUP_STOMACH:
			current_damage *= 1.25f;
			break;
		case HITGROUP_LEFTLEG:
		case HITGROUP_RIGHTLEG:
			current_damage *= 0.75f;
			break;
		}
		if (enemy->ArmorValue() > 0)
		{
			float v47 = 1.f, ArmorBonusRatio = 0.5f, ArmorRatio = weapon_armor_ratio * 0.5f;

			auto NewDamage = current_damage * ArmorRatio;


			if (((current_damage - (current_damage * ArmorRatio)) * (v47 * ArmorBonusRatio)) > ArmorValue)
				NewDamage = current_damage - (ArmorValue / ArmorBonusRatio);

			current_damage = NewDamage;
		}
	
}

bool SimulateFireBullet(IClientEntity *local, C_BaseCombatWeapon *weapon, FireBulletData &data)
{
	data.penetrate_count = 4; 
	data.trace_length = 0.0f;
	auto *wpn_data = weapon->GetCSWpnData(); 

	data.current_damage = static_cast<float>(wpn_data->m_iDamage);

	while ((data.penetrate_count > 0) && (data.current_damage >= 1.0f))
	{
		data.trace_length_remaining = wpn_data->m_flRange - data.trace_length;

		Vector End_Point = data.src + data.direction * data.trace_length_remaining;

		UTIL_TraceLine(data.src, End_Point, 0x4600400B, local, 0, &data.enter_trace);
		UTIL_ClipTraceToPlayers(data.src, End_Point * 40.f, 0x4600400B, &data.filter, &data.enter_trace);

		if (data.enter_trace.fraction == 1.0f) break;
		if ((data.enter_trace.hitgroup <= 7) && (data.enter_trace.hitgroup > 0) && (local->GetTeamNum() != data.enter_trace.m_pEnt->GetTeamNum()))
		{
			data.trace_length += (float)(data.enter_trace.fraction * data.trace_length_remaining);
			data.current_damage *= (float)(pow(wpn_data->m_flRangeModifier, data.trace_length * 0.002));
			ScaleDamage(data.enter_trace.hitgroup, data.enter_trace.m_pEnt, wpn_data->m_flArmorRatio, data.current_damage);
			return true;

		}
		if (!HandleBulletPenetration(wpn_data, data)) break;
	}
	return false;

}

bool HandleBulletPenetration(CSWeaponInfo *wpn_data, FireBulletData &data)
{
	
	surfacedata_t *enter_surface_data = Interfaces::PhysProps->GetSurfaceData(data.enter_trace.surface.surfaceProps);
	int enter_material = enter_surface_data->game.material;
	float enter_surf_penetration_mod = enter_surface_data->game.flPenetrationModifier;


	data.trace_length += data.enter_trace.fraction * data.trace_length_remaining;
	data.current_damage *= pow(wpn_data->m_flRangeModifier, (data.trace_length * 0.002));

	if ((data.trace_length > 3000.f) || (enter_surf_penetration_mod < 0.1f))data.penetrate_count = 0;
	if (data.penetrate_count <= 0)return false;

	Vector dummy;
	trace_t trace_exit;

	if (!TraceToExit(dummy, data.enter_trace, data.enter_trace.endpos, data.direction, &trace_exit)) return false;
	surfacedata_t *exit_surface_data = Interfaces::PhysProps->GetSurfaceData(trace_exit.surface.surfaceProps);

	int exit_material = exit_surface_data->game.material;

	float exit_surf_penetration_mod = exit_surface_data->game.flPenetrationModifier;
	float final_damage_modifier = 0.16f;
	float combined_penetration_modifier = 0.0f;

	if (((data.enter_trace.contents & CONTENTS_GRATE) != 0) || (enter_material == 89) || (enter_material == 71))
	{
		combined_penetration_modifier = 3.0f;
		final_damage_modifier = 0.05f;
	}
	else
	{
		combined_penetration_modifier = (enter_surf_penetration_mod + exit_surf_penetration_mod) * 0.5f;
	}

	if (enter_material == exit_material)
	{
		if (exit_material == 87 || exit_material == 85)
			combined_penetration_modifier = 3.0f;
		else if (exit_material == 76)
			combined_penetration_modifier = 2.0f;
	}
	float v34 = fmaxf(0.f, 1.0f / combined_penetration_modifier);
	float v35 = (data.current_damage * final_damage_modifier) + v34 * 3.0f * fmaxf(0.0f, (3.0f / wpn_data->m_flPenetration) * 1.25f);
	float thickness = VectorLength(trace_exit.endpos - data.enter_trace.endpos);
	thickness *= thickness;
	thickness *= v34;
	thickness /= 24.0f;
	float lost_damage = fmaxf(0.0f, v35 + thickness);
	if (lost_damage > data.current_damage)return false;
	if (lost_damage >= 0.0f)data.current_damage -= lost_damage;
	if (data.current_damage < 1.0f) return false;
	data.src = trace_exit.endpos;
	data.penetrate_count--;

	return true;
}

bool CanHit(const Vector &point, float *damage_given)
{
	auto *local = Interfaces::EntList->GetClientEntity(Interfaces::Engine->GetLocalPlayer());
	auto data = FireBulletData(local->GetOrigin() + local->GetViewOffset());
	data.filter = CTraceFilter();
	data.filter.pSkip = local;

	Vector angles;
	CalcAngle(data.src, point, angles);
	AngleVectors(angles, &data.direction);
	VectorNormalize(data.direction);

	if (SimulateFireBullet(local, (C_BaseCombatWeapon*)Interfaces::EntList->GetClientEntityFromHandle(local->GetActiveWeaponHandle()), data))
	{
		*damage_given = data.current_damage;
		return true;
	}

	return false;
}
bool CanWallbang(float &dmg)
{
	auto m_local = Interfaces::EntList->GetClientEntity(Interfaces::Engine->GetLocalPlayer());

	if (m_local && m_local->IsAlive()) {
		FireBulletData data = FireBulletData(m_local->GetEyePosition());
		data.filter = CTraceFilter();
		data.filter.pSkip = m_local;
		Vector eye_angle; Interfaces::Engine->GetViewAngles(eye_angle);
		Vector dst, forward;
		AngleVectors(eye_angle, &forward);
		dst = data.src + (forward * 8196.f);
		Vector angles;
		CalcAngle(data.src, dst, angles);
		AngleVectors(angles, &data.direction);
		VectorNormalize(data.direction);

		auto m_weapon = m_local->GetWeapon2();

		if (m_weapon) 
		{
			data.penetrate_count = 1;
			data.trace_length = 0.0f;
			CSWeaponInfo *weapon_data = m_weapon->GetCSWpnData();
			if (weapon_data) {
				data.current_damage = (int)weapon_data->m_iDamage;

				data.trace_length_remaining = weapon_data->m_flRange  ; //-data.trace_length; <- i think this shit is useless tbh

				Vector end = data.src + data.direction * data.trace_length_remaining;
				UTIL_TraceLine(data.src, end, MASK_SHOT | CONTENTS_GRATE, m_local, 0, &data.enter_trace);
				if (data.enter_trace.fraction == 1.f) return false;

				if (HandleBulletPenetration(weapon_data, data)) {
					dmg = data.current_damage;
					return true;
				}
			}
		}
	}

	return false;
}
