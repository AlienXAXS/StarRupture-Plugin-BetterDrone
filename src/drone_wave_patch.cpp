#include "drone_wave_patch.h"
#include "drone_config.h"
#include "plugin_helpers.h"
#include <plugin_interface.h>

// ACrCharacterPlayerBase::CanBuildingDroneBeActive
// Returns false during EnviroWaveStage_PreWave / Moving / Fadeout, blocking drone use.
// We call the original; if it returns false we return true so waves never restrict the drone.
// Other checks in the original (interior, exclusion zones, sliding, being attacked) are
// preserved because we only override the false case, not any of the true paths.

typedef bool(__fastcall* CanBuildingDroneBeActive_t)(void* thisPtr);
static CanBuildingDroneBeActive_t g_original = nullptr;
static HookHandle                 g_hook      = nullptr;

static bool __fastcall Detour_CanBuildingDroneBeActive(void* thisPtr)
{
    if (DroneConfig::Config::ReadAlwaysAllowDrone())
        return true;

    return g_original ? g_original(thisPtr) : false;
}

bool InitWavePatch()
{
    auto* scanner = GetSelf()->scanner;

    uintptr_t addr = scanner->FindPatternInMainModule(
        "48 89 5C 24 ?? 55 56 57 48 83 EC ?? 48 8B D9 48 8B 89");

    if (!addr)
    {
        LOG_WARN("WavePatch: CanBuildingDroneBeActive pattern not found — wave restriction not patched");
        return false;
    }

    LOG_INFO("WavePatch: CanBuildingDroneBeActive found at 0x%llX", addr);

    g_hook = GetSelf()->hooks->Hooks->Install(
        addr,
        reinterpret_cast<void*>(&Detour_CanBuildingDroneBeActive),
        reinterpret_cast<void**>(&g_original));

    if (!g_hook)
    {
        LOG_WARN("WavePatch: hook installation failed");
        return false;
    }

    LOG_INFO("WavePatch: drone wave restriction removed");
    return true;
}

void ShutdownWavePatch()
{
    if (g_hook)
    {
        GetSelf()->hooks->Hooks->Remove(g_hook);
        g_hook      = nullptr;
        g_original  = nullptr;
        LOG_DEBUG("WavePatch: hook removed");
    }
}
