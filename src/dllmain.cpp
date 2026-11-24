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

// Which sections PluginInit set up. A host rule can flip a section's On()
// flag at any time, so PluginShutdown must undo what was done, not what On()
// says now, and a section's tick must not run for a section whose Init never
// did. Set when the section's Init was called, not on its return value: a
// failed Init can leave a hook or keybind that its Shutdown has to remove.
static struct InitRan
{
    bool audio = false, laser = false, mapMarker = false, speedometer = false;
    bool interact = false, hotbar = false, map = false, mapFollow = false, fog = false;
    bool buildMenu = false, escPause = false;
} s_ran;

// What the drone limits are applied as: the drone settings carry the
// configured values while true, the game's own while false. Compared with the
// flag on every tick, so a flip either way is caught on the game thread.
static bool s_limitsApplied = false;

// Whether each section was on at the last tick, for the sections whose off
// state has to put something back. Only the latch moves when a section comes
// back on: its own tick applies its values again.
static bool s_wasOnAudio = true, s_wasOnAnnouncer = true, s_wasOnLaser = true, s_wasOnMapMarker = true;
static bool s_wasOnMapFollow = true, s_wasOnSpeedometer = true;

// Game thread, from the tick. Calls restore on the tick a section that ran its
// Init turns off; a section that did not run has nothing of ours to put back.
static void FollowOff(bool ran, bool on, bool& wasOn, void (*restore)())
{
    if (!ran || on == wasOn)
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

static PluginInfo s_info =
{
    "BetterDrone",
    MODLOADER_BUILD_TAG,
    "AlienX",
    "Adjust building drone limits, speed boost, and audio via plugin config.",
    PLUGIN_DECLARED_INTERFACE,
    PLUGIN_TARGET_CLIENT,
    PLUGIN_GAME_BUILT_CL,
    PLUGIN_MAP_MIGRATED
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
    TickDroneFloorGuard(deltaSeconds);
    TickDroneBodyAnchor();
    FollowOff(s_ran.audio, DroneFields::Features::Audio.On(), s_wasOnAudio, DroneAudio::ResetVolumes);
    FollowOff(true, DroneFields::Features::Announcer.On(), s_wasOnAnnouncer, DroneAudio::RestoreAnnouncer);
    FollowOff(s_ran.laser, DroneFields::Features::MiningLaser.On(), s_wasOnLaser, StopDroneLaser);
    FollowOff(s_ran.mapMarker, DroneFields::Features::MapMarker.On(), s_wasOnMapMarker, HideDroneMapMarker);
    FollowOff(s_ran.mapFollow, DroneFields::Features::MapFollow.On(), s_wasOnMapFollow, StopDroneMapFollow);
    FollowOff(s_ran.speedometer, DroneFields::Features::Speedometer.On(), s_wasOnSpeedometer, HideDroneSpeedometer);
    if (s_ran.audio && DroneFields::Features::Audio.On())
        DroneAudio::Tick(deltaSeconds);
    if (DroneFields::Features::Announcer.On())
        DroneAudio::TickAnnouncer();
    if (s_ran.laser && DroneFields::Features::MiningLaser.On())
        TickDroneLaser(deltaSeconds);
    TickDroneMenuClose();
    TickDroneMenuOpen();
    if (s_ran.mapMarker && DroneFields::Features::MapMarker.On())
        TickDroneMapMarker(deltaSeconds);
    if (s_ran.mapFollow && DroneFields::Features::MapFollow.On())
        TickDroneMapFollow();
    if (s_ran.speedometer && DroneFields::Features::Speedometer.On())
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
        if (!LimitKeysRegistered())
            return;

        // Re-register from scratch rather than rely on the loader's own
        // live-rebind patch: it matches by exact combo string, and Released
        // is registered under the bare base key, not the full combo.
        RebindBoostKey(g_self, newValue);
        LOG_DEBUG("OnConfigChanged: Boost Key rebound to '%s'", newValue ? newValue : "");
        return;
    }

    if (strcmp(section, "Controls") == 0 && strcmp(key, "Slow Key") == 0)
    {
        if (!LimitKeysRegistered())
            return;

        // Re-registered for the same reason as Boost Key: its Released is
        // registered under the bare base key.
        RebindSlowKey(g_self, newValue);
        LOG_DEBUG("OnConfigChanged: Slow Key rebound to '%s'", newValue ? newValue : "");
        return;
    }

    if (strcmp(section, "Controls") == 0 && strcmp(key, "Mining Laser Key") == 0)
    {
        if (!DroneFields::Features::MiningLaser.On())
            return;

        // Re-registered for the same reason as Boost Key: its Released is
        // registered under the bare base key.
        RebindDroneLaserKey(g_self, newValue);
        LOG_DEBUG("OnConfigChanged: Mining Laser Key rebound to '%s'", newValue ? newValue : "");
        return;
    }

    if (strcmp(section, "Interaction") == 0 && strcmp(key, "Drone Reveals Map") == 0
        && DroneFields::Features::MapReveal.On())
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
    if (DroneFields::Features::Audio.On())
    {
        s_ran.audio = true;
        DroneAudio::Initialize();
    }
    InitGameSessionTracking(self);
    if (DroneFields::Features::MiningLaser.On())
    {
        s_ran.laser = true;
        InitDroneLaser(self);
    }
    InitDroneUI(self);
    if (DroneFields::Features::MapMarker.On())
    {
        s_ran.mapMarker = true;
        InitDroneMapMarker(self);
    }
    if (DroneFields::Features::Speedometer.On())
    {
        s_ran.speedometer = true;
        InitDroneSpeedometer(self);
    }
    InitDronePresetKeys(self);

    self->hooks->Engine->RegisterOnInit(OnEngineInit);
    self->hooks->Engine->RegisterOnShutdown(OnEngineShutdown);
    self->hooks->Engine->RegisterOnTick(OnEngineTick);

    InitWavePatch(DroneFields::Features::AlwaysAllow.On(), DroneFields::Features::WaveShelter.On());
    if (DroneFields::Features::Interaction.On())
    {
        s_ran.interact = true;
        InitDroneInteract();
    }
    const bool hotbarPage = DroneFields::Features::HotbarPage.On();
    const bool hotbarSelect = DroneFields::Features::HotbarSelect.On();
    if (hotbarPage || hotbarSelect)
    {
        s_ran.hotbar = true;
        InitDroneHotbar(self, hotbarPage, hotbarSelect);
    }
    if (DroneFields::Features::MapOpen.On())
    {
        s_ran.map = true;
        InitDroneMap();
    }
    if (DroneFields::Features::MapFollow.On())
    {
        s_ran.mapFollow = true;
        InitDroneMapFollow(self);
    }
    if (DroneFields::Features::MapReveal.On())
    {
        s_ran.fog = true;
        InitDroneFog();
    }
    if (DroneFields::Features::BuildMenu.On())
    {
        s_ran.buildMenu = true;
        InitDroneBuildMenu();
    }
    if (DroneFields::Features::EscPause.On())
    {
        s_ran.escPause = true;
        InitDroneEscPause(self);
    }
    if (DroneFields::Features::Limits.On())
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
    if (s_ran.laser)
        ShutdownDroneLaser(g_self);
    if (s_ran.interact)
        ShutdownDroneInteract();
    if (s_ran.hotbar)
        ShutdownDroneHotbar(g_self);
    if (s_ran.map)
        ShutdownDroneMap();
    if (s_ran.fog)
        ShutdownDroneFog();
    if (s_ran.buildMenu)
        ShutdownDroneBuildMenu();
    if (s_ran.escPause)
        ShutdownDroneEscPause(g_self);
    ShutdownWavePatch();
    if (s_ran.audio)
        DroneAudio::Shutdown();
    DroneAudio::ShutdownAnnouncer();
    if (s_ran.mapFollow)
        ShutdownDroneMapFollow(g_self);
    if (s_ran.mapMarker)
        ShutdownDroneMapMarker(g_self);
    if (s_ran.speedometer)
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

    s_ran = InitRan{};
    s_limitsApplied = false;
    s_wasOnAudio = s_wasOnAnnouncer = s_wasOnLaser = s_wasOnMapMarker = true;
    s_wasOnMapFollow = s_wasOnSpeedometer = true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID)
{
    return TRUE;
}
