#include "drone_settings.h"
#include "drone_fields.h"
#include "drone_config.h"
#include "drone_interact.h"
#include "drone_wave_patch.h"
#include "drone_audio.h"
#include "drone_esc_pause.h"
#include "drone_body_anchor.h"
#include "drone_floor_guard.h"
#include "drone_hotbar.h"
#include "drone_laser.h"
#include "drone_map_marker.h"
#include "drone_speedometer.h"
#include "plugin_helpers.h"
#include <plugin_interface.h>
#include <windows.h>
#include <atomic>
#include <cstdlib>
#include <cstring>

#include "drone_ui.h"

static IPluginSelf* g_self = nullptr;

IPluginSelf* GetSelf() { return g_self; }

// What the drone limits are applied as: the drone settings carry the
// configured values while true, the game's own while false. Compared with the
// flag on every tick, so a flip either way is caught on the game thread.
static bool s_limitsApplied = false;

// Whether each section was on at the last tick, for the sections whose off
// state has to put something back. Only the latch moves when a section comes
// back on: its own tick applies its values again.
static bool s_wasOnAudio = true, s_wasOnAnnouncer = true, s_wasOnLaser = true, s_wasOnMapMarker = true;
static bool s_wasOnMapFollow = true, s_wasOnSpeedometer = true;

static bool s_wasOnBodyArea = true;

static void ReleaseBodyArea()
{
    ResetDroneFloorGuard();
    ResetDroneBodyAnchor();
}

// Game thread, from the tick. Calls restore on the tick a section turns off.
static void FollowOff(bool on, bool& wasOn, void (*restore)())
{
    if (on == wasOn)
        return;

    wasOn = on;
    if (!on)
        restore();
}

static std::atomic<DWORD> g_gameThreadId{ 0 };

void NoteGameThread() { g_gameThreadId.store(GetCurrentThreadId(), std::memory_order_relaxed); }
bool OnGameThread()   { return GetCurrentThreadId() == g_gameThreadId.load(std::memory_order_relaxed); }

#ifndef MODLOADER_BUILD_TAG
#define MODLOADER_BUILD_TAG "dev"
#endif

PLUGIN_EXPORT_HOST_INFO()

// The loader refuses a migrated plugin that names no game build, on every build.
static_assert(PLUGIN_GAME_BUILT_CL != 0,
	"No game build: use Game-SDK headers with build_cl.txt, or pass /p:PluginGameBuiltCL=<CL>");

static PluginInfo s_info =
{
    "BetterDrone",
    MODLOADER_BUILD_TAG,
    "AlienX",
    "Adjust building drone limits, speed boost, and audio via plugin config.",
    PLUGIN_DECLARED_INTERFACE,
    PLUGIN_TARGET_CLIENT,
    PLUGIN_GAME_BUILT_CL,
    // Every detour checks its own feature and calls the original while that is off.
    PLUGIN_MAP_MIGRATED | PLUGIN_HANDLES_HOST_DENY
};

static void OnEngineInit()
{
    if (!DroneFields::Features::Limits.On())
        return;

    // Marked applied even when the game's values were not found, so the tick
    // does not retry the lookup every frame.
    s_limitsApplied = true;
    SetDroneLimitsActive(true);
}

static void OnEngineTick(float deltaSeconds)
{
    NoteGameThread();
    const bool limitsOn = DroneFields::Features::Limits.On();
    if (limitsOn != s_limitsApplied)
    {
        s_limitsApplied = limitsOn;
        SetDroneLimitsActive(limitsOn);
    }
    if (s_limitsApplied)
        OnDroneTick(deltaSeconds);
    const bool bodyAreaOn = DroneFields::Features::BodyArea.On();
    FollowOff(bodyAreaOn, s_wasOnBodyArea, ReleaseBodyArea);
    if (bodyAreaOn)
    {
        TickDroneFloorGuard(deltaSeconds);
        TickDroneBodyAnchor();
    }
    FollowOff(DroneFields::Features::Audio.On(), s_wasOnAudio, DroneAudio::ResetVolumes);
    FollowOff(DroneFields::Features::Announcer.On(), s_wasOnAnnouncer, DroneAudio::RestoreAnnouncer);
    FollowOff(DroneFields::Features::MiningLaser.On(), s_wasOnLaser, StopDroneLaser);
    FollowOff(DroneFields::Features::MapMarker.On(), s_wasOnMapMarker, HideDroneMapMarker);
    FollowOff(DroneFields::Features::MapFollow.On(), s_wasOnMapFollow, StopDroneMapFollow);
    FollowOff(DroneFields::Features::Speedometer.On(), s_wasOnSpeedometer, HideDroneSpeedometer);
    if (DroneFields::Features::Audio.On())
        DroneAudio::Tick(deltaSeconds);
    if (DroneFields::Features::Announcer.On())
        DroneAudio::TickAnnouncer();
    if (DroneFields::Features::MiningLaser.On())
        TickDroneLaser(deltaSeconds);
    TickDroneMenuClose();
    TickDroneMenuOpen();
    if (DroneFields::Features::MapMarker.On())
        TickDroneMapMarker(deltaSeconds);
    if (DroneFields::Features::MapFollow.On())
        TickDroneMapFollow();
    if (DroneFields::Features::Speedometer.On())
        TickDroneSpeedometer();
    TickDronePresetKeys();
}

// Fires only from the loader's own settings window, for the entries
// registered in DroneConfig::Config::Initialize. Keybind rebinds and the
// boolean entries are picked up live wherever they're consulted, except
// Drone Reveals Map and the two body-area entries, which the tick reads from
// a cache set here. This fires
// before the loader writes the INI, so the cache takes newValue.
static void OnConfigChanged(const char* section, const char* key, const char* newValue)
{
    if (!section || !key)
        return;

    if (strcmp(section, "Controls") == 0 && strcmp(key, "Boost Key") == 0)
    {
        // Re-register from scratch rather than rely on the loader's own
        // live-rebind patch: it matches by exact combo string, and Released
        // is registered under the bare base key, not the full combo.
        RebindBoostKey(g_self, newValue);
        LOG_DEBUG("OnConfigChanged: Boost Key rebound to '%s'", newValue ? newValue : "");
        return;
    }

    if (strcmp(section, "Controls") == 0 && strcmp(key, "Slow Key") == 0)
    {
        // Re-registered for the same reason as Boost Key: its Released is
        // registered under the bare base key.
        RebindSlowKey(g_self, newValue);
        LOG_DEBUG("OnConfigChanged: Slow Key rebound to '%s'", newValue ? newValue : "");
        return;
    }

    if (strcmp(section, "Controls") == 0 && strcmp(key, "Mining Laser Key") == 0)
    {
        // Re-registered for the same reason as Boost Key: its Released is
        // registered under the bare base key.
        RebindDroneLaserKey(g_self, newValue);
        LOG_DEBUG("OnConfigChanged: Mining Laser Key rebound to '%s'", newValue ? newValue : "");
        return;
    }

    if (strcmp(section, "Interaction") == 0 && strcmp(key, "Drone Reveals Map") == 0)
        SetDroneFogEnabled(newValue && (strcmp(newValue, "1") == 0 || _stricmp(newValue, "true") == 0));

    if (strcmp(section, "Drone") == 0 && strcmp(key, "Keep Area Around Body Loaded") == 0)
        SetDroneBodyAnchorEnabled(newValue && (strcmp(newValue, "1") == 0 || _stricmp(newValue, "true") == 0));

    if (strcmp(section, "Drone") == 0 && strcmp(key, "Body Area Radius") == 0 && newValue)
        SetDroneBodyAnchorRadiusMeters(static_cast<float>(atof(newValue)));

    LOG_DEBUG("OnConfigChanged: [%s] %s updated", section, key);
}

static void OnEngineShutdown()
{
    LOG_DEBUG("OnEngineShutdown: resetting drone settings");
    g_drone = DroneSettings{};
}

extern "C" __declspec(dllexport) PluginInfo* GetPluginInfo()
{
    return &s_info;
}

extern "C" __declspec(dllexport) void OnPluginLoadHooks(IPluginSelf* self, IPluginHookScanner* scanner)
{
    g_self = self;

    DroneFields::Features::Declare(self, scanner);

    ResolveWavePatch(self, scanner);
    ResolveDroneInteract(self, scanner);
    ResolveDroneMap(self, scanner);
    ResolveDroneMapFollow(self, scanner);
    ResolveDroneFog(self, scanner);
    ResolveDroneBuildMenu(self, scanner);
    ResolveDroneEscPause(self, scanner);
    ResolveDroneLaser(self, scanner);
    DroneFields::Resolve(self, scanner);
    DroneFields::ResolveHotbarSizes(self, scanner);
}

extern "C" __declspec(dllexport) bool PluginInit(IPluginSelf* self)
{
    // First, before any early return: off its game build the loader refuses a
    // plugin whose PluginInit did not take the globals.
    PluginSdkRuntime::Init(self->hooks);

    g_self = self;

    LOG_DEBUG("PluginInit: registering hooks");

    DroneConfig::Config::Initialize(self);
    InitDroneMenuState(self);
    SetDroneBodyAnchorEnabled(DroneConfig::Config::ReadKeepBodyLoaded());
    SetDroneBodyAnchorRadiusMeters(DroneConfig::Config::ReadBodyAreaRadius());
    // Every section is set up, whatever its On() says now: a section can be
    // handed back at any time. Each setup that needs an address checks it, and
    // everything that acts checks On().
    DroneAudio::Initialize();
    InitGameSessionTracking(self);
    InitDroneLaser(self);
    InitDroneUI(self);
    InitDroneMapMarker(self);
    InitDroneSpeedometer(self);
    InitDronePresetKeys(self);

    self->hooks->Engine->RegisterOnInit(OnEngineInit);
    self->hooks->Engine->RegisterOnShutdown(OnEngineShutdown);
    self->hooks->Engine->RegisterOnTick(OnEngineTick);

    InitWavePatch();
    InitDroneInteract();
    InitDroneHotbar(self);
    InitDroneMap();
    InitDroneMapFollow(self);
    InitDroneFog();
    InitDroneBuildMenu();
    InitDroneEscPause(self);
    RegisterLimitKeys(self);

    if (self->hooks->UI)
    {
        self->hooks->UI->RegisterOnConfigChanged(self, OnConfigChanged);
    }

    LOG_INFO("BetterDrone initialised");
    return true;
}

extern "C" __declspec(dllexport) void PluginShutdown()
{
    LOG_DEBUG("PluginShutdown: restoring CDO defaults and unregistering hooks");

    UnregisterLimitKeys(g_self);
    ShutdownDroneBodyAnchor();
    ShutdownDroneLaser(g_self);
    ShutdownDroneInteract();
    ShutdownDroneHotbar(g_self);
    ShutdownDroneMap();
    ShutdownDroneFog();
    ShutdownDroneBuildMenu();
    ShutdownDroneEscPause(g_self);
    ShutdownWavePatch();
    DroneAudio::Shutdown();
    DroneAudio::ShutdownAnnouncer();
    ShutdownDroneMapFollow(g_self);
    ShutdownDroneMapMarker(g_self);
    ShutdownDroneSpeedometer(g_self);
    ShutdownDronePresetKeys(g_self);
    ShutdownDroneUI(g_self);
    ShutdownGameSessionTracking(g_self);
    if (g_self)
        ShutdownDroneMenuState(g_self);
    RestoreCDODefaults();

    if (g_self)
    {
        g_self->hooks->Engine->UnregisterOnInit(OnEngineInit);
        g_self->hooks->Engine->UnregisterOnShutdown(OnEngineShutdown);
        g_self->hooks->Engine->UnregisterOnTick(OnEngineTick);

        if (g_self->hooks->UI)
        {
            g_self->hooks->UI->UnregisterOnConfigChanged(g_self, OnConfigChanged);
        }

        g_self = nullptr;
    }

    s_limitsApplied = false;
    s_wasOnAudio = s_wasOnAnnouncer = s_wasOnLaser = s_wasOnMapMarker = true;
    s_wasOnMapFollow = s_wasOnSpeedometer = true;
    s_wasOnBodyArea = true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID)
{
    return TRUE;
}
