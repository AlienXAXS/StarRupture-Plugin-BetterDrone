#include "drone_settings.h"
#include "drone_config.h"
#include "plugin_helpers.h"
#include <plugin_interface.h>
#include <Engine_classes.hpp>
#include <windows.h>

static IPluginSelf* g_self = nullptr;

IPluginSelf* GetSelf() { return g_self; }

static PluginInfo s_info =
{
    "BetterDrone",
    "1.0.0",
    "AlienXAXS",
    "Adjust building drone limits via plugin config.",
    PLUGIN_INTERFACE_VERSION
};

static void OnEngineInit()
{
    LOG_DEBUG("OnEngineInit: acquiring drone settings CDO");

    if (!InitDroneSettings())
    {
        LOG_WARN("OnEngineInit: CDO not found — drone settings unavailable");
        return;
    }

    LOG_DEBUG("OnEngineInit: CDO defaults — speed=%.0f maxRadius=%.0f maxHeight=%.0f",
        *g_drone.speedPerSec, *g_drone.maxRadius, *g_drone.maxHeight);

    DroneConfig::Config::InitializeWithCDODefaults(
        *g_drone.speedPerSec,
        *g_drone.maxRadius,
        *g_drone.maxHeight);

    *g_drone.speedPerSec   = DroneConfig::Config::ReadSpeedPerSec();
    *g_drone.maxRadius     = DroneConfig::Config::ReadMaxRadius();
    *g_drone.maxHeight     = DroneConfig::Config::ReadMaxHeight();
    *g_drone.warningRadius = *g_drone.maxRadius * 0.95f;
    *g_drone.warningHeight = *g_drone.maxHeight * 0.95f;

    LOG_DEBUG("OnEngineInit: applied — speed=%.0f maxRadius=%.0f warningRadius=%.0f maxHeight=%.0f warningHeight=%.0f",
        *g_drone.speedPerSec, *g_drone.maxRadius, *g_drone.warningRadius,
        *g_drone.maxHeight, *g_drone.warningHeight);

    UpdateActiveDrones();
}

static void OnConfigChanged(const char* section, const char* key, const char* newValue)
{
    if (!g_drone.valid || strcmp(section, "Drone") != 0)
        return;

    const float val = strtof(newValue, nullptr);
    LOG_DEBUG("OnConfigChanged: [%s] %s = %s", section, key, newValue);

    if (strcmp(key, "SpeedPerSec") == 0)
    {
        *g_drone.speedPerSec = val;
    }
    else if (strcmp(key, "MaxRadius") == 0)
    {
        *g_drone.maxRadius     = val;
        *g_drone.warningRadius = val * 0.95f;
    }
    else if (strcmp(key, "MaxHeight") == 0)
    {
        *g_drone.maxHeight     = val;
        *g_drone.warningHeight = val * 0.95f;
    }
    else
    {
        return;
    }

    UpdateActiveDrones();
}

static void OnWorldBeginPlay(SDK::UWorld*)
{
    LOG_DEBUG("OnWorldBeginPlay: updating active drones");
    UpdateActiveDrones();
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

extern "C" __declspec(dllexport) bool PluginInit(IPluginSelf* self)
{
    g_self = self;

    LOG_DEBUG("PluginInit: registering hooks");

    DroneConfig::Config::SetSelf(self);

    self->hooks->Engine->RegisterOnInit(OnEngineInit);
    self->hooks->Engine->RegisterOnShutdown(OnEngineShutdown);
    self->hooks->World->RegisterOnWorldBeginPlay(OnWorldBeginPlay);

    if (self->hooks->UI)
        self->hooks->UI->RegisterOnConfigChanged(OnConfigChanged);

    LOG_INFO("BetterDrone initialised");
    return true;
}

extern "C" __declspec(dllexport) void PluginShutdown()
{
    LOG_DEBUG("PluginShutdown: restoring CDO defaults and unregistering hooks");

    RestoreCDODefaults();

    if (g_self)
    {
        g_self->hooks->Engine->UnregisterOnInit(OnEngineInit);
        g_self->hooks->Engine->UnregisterOnShutdown(OnEngineShutdown);
        g_self->hooks->World->UnregisterOnWorldBeginPlay(OnWorldBeginPlay);

        if (g_self->hooks->UI)
            g_self->hooks->UI->UnregisterOnConfigChanged(OnConfigChanged);

        g_self = nullptr;
    }
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID)
{
    return TRUE;
}
