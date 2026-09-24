#include "drone_wave_patch.h"
#include "drone_config.h"
#include "plugin_helpers.h"
#include <plugin_interface.h>

// ACrCharacterPlayerBase::CanBuildingDroneBeActive
// Returns false during EnviroWaveStage_PreWave / Moving / Fadeout, blocking drone use.
// We call the original; if it returns false we return true so waves never restrict the drone.
// Other checks in the original (interior, exclusion zones, sliding, being attacked) are
// preserved because we only override the false case, not any of the true paths.

static constexpr const char* kCanBuildingDroneBeActivePattern =
    "48 89 5C 24 ?? 55 56 57 48 83 EC ?? 48 8B D9 48 8B 89";

typedef bool(__fastcall* CanBuildingDroneBeActive_t)(void* thisPtr);
static CanBuildingDroneBeActive_t g_original = nullptr;
static HookHandle                 g_hook      = nullptr;

// Resolved during OnPluginLoadHooks; 0 means the pattern missed on this build.
static uintptr_t                  g_addr      = 0;

static bool __fastcall Detour_CanBuildingDroneBeActive(void* thisPtr)
{
    if (DroneConfig::Config::ReadAlwaysAllowDrone())
        return true;

    return g_original ? g_original(thisPtr) : false;
}

void ResolveWavePatch(IPluginSelf* self, IPluginHookScanner* scanner)
{
    if (!self || !scanner)
        return;

    // Optional: a miss leaves the rest of BetterDrone working, which is what the
    // old scan-at-init path did. The loader still lists it for the user.
    //
    // FUNCTION_START because a detour is written over this address: the loader
    // checks the match is a real function entry with room for the 14-byte jump,
    // rather than taking the pattern's word for it.
    PluginScanRequest req = PLUGIN_SCAN_REQUEST_INIT;
    req.hookName = "ACrCharacterPlayerBase::CanBuildingDroneBeActive";
    req.pattern  = kCanBuildingDroneBeActivePattern;
    req.kind     = PLUGIN_SCAN_FUNCTION_START;
    req.flags    = PLUGIN_SCAN_FLAG_OPTIONAL;

    g_addr = scanner->Resolve(self, &req);
}

bool InitWavePatch()
{
    uintptr_t addr = g_addr;

    if (!addr)
    {
        LOG_WARN("WavePatch: CanBuildingDroneBeActive unresolved — wave restriction not patched");
        return false;
    }

    LOG_INFO("WavePatch: CanBuildingDroneBeActive at 0x%llX", addr);

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
