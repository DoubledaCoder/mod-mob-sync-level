#include "ScriptMgr.h"
#include "Creature.h"
#include "CreatureData.h"
#include "CreatureScript.h"
#include "Unit.h"
#include "LevelSync.h"
#include "Config.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "World.h"
#include <cmath>

// Default configs
static bool ENABLED = true;
static int32 SYNC_UP_THRESHOLD = 5;
static int32 DROP_THRESHOLD = 5;
static bool DEBUG_LOG = false;

void LevelSync::LoadConfig()
{
    ENABLED = sConfigMgr->GetOption<bool>("LevelSync.Enable", true);
    SYNC_UP_THRESHOLD = sConfigMgr->GetOption<int>("LevelSync.SyncUpThreshold", 5);
    DROP_THRESHOLD = sConfigMgr->GetOption<int>("LevelSync.DropThreshold", 5);
    DEBUG_LOG = sConfigMgr->GetOption<bool>("LevelSync.Debug", false);
}

void LevelSync::OnUnitEnterCombat(Unit* unit, Unit* victim){
    LevelSync::DetermineCreature(unit,victim);
}
// Hook override to update the situation when damage is taken
void LevelSync::OnDamage(Unit* attacker, Unit* victim, uint32& damage)
{
    
    LevelSync::DetermineCreature(attacker,victim);
}

void LevelSync::OnUnitStopCombat(Unit* unit){
    Creature* creature = unit->ToCreature();
    //Creature object
    if(!creature){
        return;
    }
    //needs to be alive
    if(!creature->IsAlive()){
        return;
    }
    //If the creature is a pet then cancel the sync
    if(LevelSync::IsPlayerPet(creature)){
        return;
    }
    //then must have a template
    CreatureTemplate const* cInfo = creature->GetCreatureTemplate();
    if(!cInfo){
        return;
    }
    uint8 minLevel = cInfo->minlevel;
    uint8 maxLevel = cInfo->maxlevel;
    uint8 current = creature->GetLevel();
    if(current < minLevel || current > maxLevel){
        LevelSync::UpdateAllStats(creature,cInfo,minLevel);
    }
}

void LevelSync::DetermineCreature(Unit* attacker, Unit* victim){
    //early escape from error types
    if(!attacker || !victim){
        return;
    }
    //From entering combat or damaging first check if the target is hostile
    if(!attacker->IsHostileTo(victim)){
        return;
    }
    TypeID AttackerType = attacker->GetTypeId();
    TypeID VictimType = victim->GetTypeId();
    Creature* AttackerCreature = attacker -> ToCreature();
    Creature* VictimCreature = victim -> ToCreature();
    //If a playerpet get the owner of the pet
    Unit* AttackerPlayerPetOwner = LevelSync::IsPlayerPet(AttackerCreature);
    Unit* VictimPlayerPetOwner = LevelSync::IsPlayerPet(VictimCreature);
    //Player attacks a creature
    if(VictimCreature && !VictimPlayerPetOwner && AttackerType == TYPEID_PLAYER){
        LevelSync::HandleSync(VictimCreature,attacker);
        return;
    }
    //Creature attacks a player
    if(AttackerCreature && !AttackerPlayerPetOwner && VictimType == TYPEID_PLAYER){
        LevelSync::HandleSync(AttackerCreature,victim);
        return;
    }
    //Player pet attacks a creature
    if(AttackerPlayerPetOwner && VictimCreature && !VictimPlayerPetOwner){
        LevelSync::HandleSync(VictimCreature,AttackerPlayerPetOwner);
        return;
    }
    //Player pet is attacked by a creature
    if(VictimPlayerPetOwner && AttackerCreature && !AttackerPlayerPetOwner){
        LevelSync::HandleSync(AttackerCreature,VictimPlayerPetOwner);
        return;
    }
}

Unit* LevelSync::IsPlayerPet(Creature* creature) {
    if (!creature) {
        return nullptr;
    }
    Unit* owner = creature->GetOwner();
    if (owner && owner->IsPlayer()){
        return owner;
    }
    return nullptr;
}


void LevelSync::HandleSync(Creature* creature, Unit* target)
{
    uint8 current = creature->GetLevel();
    CreatureTemplate const* cInfo = creature->GetCreatureTemplate();
    if (!cInfo)
        return;
    uint8 targetLevel = target->GetLevel();
    uint8 newLevel = current;
    uint8 resetLevel = cInfo->minlevel;
    //Check if the level needs to drop first 
    if ((int)targetLevel + DROP_THRESHOLD < (int)current){
        newLevel = resetLevel;}
    //If the creature is too low level, sync it up. However, if the resetLevel is too low then also sync it up.
    if ((int)current + SYNC_UP_THRESHOLD < (int)targetLevel || (int)resetLevel + SYNC_UP_THRESHOLD < (int)targetLevel){
        newLevel = targetLevel;
    }
    if (newLevel != current){
        LevelSync::UpdateAllStats(creature, cInfo, newLevel);}
}

void LevelSync::UpdateAllStats(Creature* creature, CreatureTemplate const* cInfo, uint8 newLevel)
{
    if (!creature || !cInfo)
        return;

    // 1. Set new level
    creature->SetLevel(newLevel);

    // 2. Determine expansion scaling
    uint32 expansion = 0;
    if (newLevel >= 58) expansion = 1;
    if (newLevel >= 68) expansion = 2;

    // 3. Get base stats for this level/class
    CreatureBaseStats const* stats = sObjectMgr->GetCreatureBaseStats(newLevel, cInfo->unit_class);
    if (!stats)
        return;

    // 4. Health scaling
    float healthRate = 1.0f;
    switch (cInfo->rank)
    {
    case CREATURE_ELITE_NORMAL:      healthRate = sWorld->getRate(RATE_CREATURE_NORMAL_HP); break;
    case CREATURE_ELITE_ELITE:       healthRate = sWorld->getRate(RATE_CREATURE_ELITE_ELITE_HP); break;
    case CREATURE_ELITE_RAREELITE:   healthRate = sWorld->getRate(RATE_CREATURE_ELITE_RAREELITE_HP); break;
    case CREATURE_ELITE_WORLDBOSS:   healthRate = sWorld->getRate(RATE_CREATURE_ELITE_WORLDBOSS_HP); break;
    case CREATURE_ELITE_RARE:        healthRate = sWorld->getRate(RATE_CREATURE_ELITE_RARE_HP); break;
    default:                         healthRate = 1.0f; break;
    }
    // Set the new health total with consideration to proportion of HP remaining.
    float missingRatio = 1.0f;
    if (creature->GetMaxHealth() > 0){
        missingRatio = creature->GetHealth() / float(creature->GetMaxHealth());}
    uint32 baseHP = LevelSync::GenerateExpansionHealth(stats, cInfo, expansion);
    uint32 maxHP = uint32(std::ceil(baseHP * healthRate));
    creature->SetCreateHealth(maxHP);
    creature->SetMaxHealth(maxHP);
    uint32 currentHealth = uint32(maxHP * (creature->IsInCombat() ? missingRatio : 1.0f));
    creature->SetHealth(currentHealth);

    // 5. Mana
    uint32 mana = stats->GenerateMana(cInfo);
    creature->SetCreateMana(mana);
    creature->SetMaxPower(POWER_MANA, mana);
    creature->SetPower(POWER_MANA, mana);

    // 6. Damage
    float baseDamage = LevelSync::GenerateBaseExpansionDamage(stats, cInfo, expansion);
    float minDmg = baseDamage;
    float maxDmg = baseDamage * 1.5f;

    creature->SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, minDmg);
    creature->SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, maxDmg);

    creature->SetBaseWeaponDamage(OFF_ATTACK, MINDAMAGE, minDmg);
    creature->SetBaseWeaponDamage(OFF_ATTACK, MAXDAMAGE, maxDmg);

    creature->SetBaseWeaponDamage(RANGED_ATTACK, MINDAMAGE, minDmg);
    creature->SetBaseWeaponDamage(RANGED_ATTACK, MAXDAMAGE, maxDmg);

    // 7. Attack power
    creature->SetStatFlatModifier(UNIT_MOD_ATTACK_POWER, BASE_VALUE, stats->AttackPower);
    creature->SetStatFlatModifier(UNIT_MOD_ATTACK_POWER_RANGED, BASE_VALUE, stats->RangedAttackPower);

    // 8. Armor
    float armor = stats->GenerateArmor(cInfo);
    creature->SetStatFlatModifier(UNIT_MOD_ARMOR, BASE_VALUE, armor);
}

uint32 LevelSync::GenerateExpansionHealth(CreatureBaseStats const* stats, CreatureTemplate const* info, uint32 expansion)
{
    uint32 idx = (expansion == 0 ? info->expansion : expansion);
    // stats->BaseHealth is expected to be an array indexed by expansion
    return uint32(std::ceil(stats->BaseHealth[idx] * info->ModHealth));
}

float LevelSync::GenerateBaseExpansionDamage(CreatureBaseStats const* stats, CreatureTemplate const* info, uint32 expansion)
{
    uint32 idx = (expansion == 0 ? info->expansion : expansion);
    // assume stats->BaseDamage exists and is indexed by expansion
    return stats->BaseDamage[idx];
}
