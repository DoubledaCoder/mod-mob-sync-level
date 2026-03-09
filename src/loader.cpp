#include "ScriptMgr.h"
#include "LevelSync.h"

// Register the script (engine-style registration)
void AddSC_mod_levelsync()
{
    new LevelSync();
}

// Module loader expects this C++ symbol name; forward to the registration.
void Addmod_mob_sync_levelScripts()
{
    AddSC_mod_levelsync();
}
