#pragma once

#include "Creature.h"
#include "UnitScript.h"
#include "Unit.h"

class LevelSync : public UnitScript
{
public:
    LevelSync() : UnitScript("LevelSync") {}
    // Unit Hook
    void OnDamage(Unit* attacker, Unit* victim, uint32& damage);
    void OnUnitEnterCombat(Unit* unit, Unit* victim);
    void OnUnitExitCombat(Unit* unit);
    // Configuration loader
    static void LoadConfig();

private:
    // Core logic helpers
    static void DetermineCreature(Unit* attacker, Unit* victim); //From the hooks pick out the player and creature to handle the sync
    static void HandleSync(Creature* creature, Unit* target); //Get the creature's current target to determine whether stats should be updated
    static void UpdateAllStats(Creature* creature, CreatureTemplate const* cInfo, uint8 newLevel); //Generate the new stats
    //PlayerPet logical check
    static Unit* IsPlayerPet(Creature* creature);
    //Special stat generation helpers functions to add expansion scaling
    static uint32 GenerateExpansionHealth(CreatureBaseStats const* stats, CreatureTemplate const* info, uint32 expansion = 0); 
    static float GenerateBaseExpansionDamage(CreatureBaseStats const* stats, CreatureTemplate const* info, uint32 expansion = 0);
};
