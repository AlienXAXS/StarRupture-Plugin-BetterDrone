#include "drone_settings.h"
#include "plugin_helpers.h"
#include <AuActorPlacement_classes.hpp>
#include <BP_FloatingDrone_classes.hpp>
#include <Basic.hpp>

DroneSettings g_drone;

bool InitDroneSettings()
{
    LOG_DEBUG("InitDroneSettings: looking up UAuActorPlacementDeveloperSettings CDO");

    auto* cdo = SDK::UAuActorPlacementDeveloperSettings::GetDefaultObj();
    if (!cdo)
    {
        LOG_WARN("InitDroneSettings: CDO is null");
        g_drone.valid = false;
        return false;
    }

    auto* base = reinterpret_cast<uint8_t*>(cdo);
    g_drone.speedPerSec   = reinterpret_cast<float*>(base + 0x00B4);
    g_drone.maxRadius     = reinterpret_cast<float*>(base + 0x00B8);
    g_drone.warningRadius = reinterpret_cast<float*>(base + 0x00BC);
    g_drone.maxHeight     = reinterpret_cast<float*>(base + 0x00C0);
    g_drone.warningHeight = reinterpret_cast<float*>(base + 0x00C4);
    g_drone.maxRailLength = reinterpret_cast<float*>(base + 0x00C8);
    g_drone.origSpeedPerSec   = *g_drone.speedPerSec;
    g_drone.origMaxRadius     = *g_drone.maxRadius;
    g_drone.origWarningRadius = *g_drone.warningRadius;
    g_drone.origMaxHeight     = *g_drone.maxHeight;
    g_drone.origWarningHeight = *g_drone.warningHeight;
    g_drone.origMaxRailLength = *g_drone.maxRailLength;
    g_drone.valid             = true;

    LOG_DEBUG("InitDroneSettings: CDO found at %p", static_cast<void*>(cdo));
    return true;
}

void RestoreCDODefaults()
{
    if (!g_drone.valid)
        return;

    *g_drone.speedPerSec   = g_drone.origSpeedPerSec;
    *g_drone.maxRadius     = g_drone.origMaxRadius;
    *g_drone.warningRadius = g_drone.origWarningRadius;
    *g_drone.maxHeight     = g_drone.origMaxHeight;
    *g_drone.warningHeight = g_drone.origWarningHeight;
    *g_drone.maxRailLength = g_drone.origMaxRailLength;

    LOG_DEBUG("RestoreCDODefaults: speed=%.0f maxRadius=%.0f maxHeight=%.0f railLength=%.0f",
        g_drone.origSpeedPerSec, g_drone.origMaxRadius, g_drone.origMaxHeight, g_drone.origMaxRailLength);
}

void UpdateActiveDrones()
{
    if (!g_drone.valid)
        return;

    auto* objects = SDK::UObject::GObjects.GetTypedPtr();
    if (!objects)
    {
        LOG_WARN("UpdateActiveDrones: GObjects unavailable");
        return;
    }

    const SDK::UClass* droneClass = SDK::ABP_FloatingDrone_C::StaticClass();
    if (!droneClass)
    {
        LOG_WARN("UpdateActiveDrones: ABP_FloatingDrone_C class not found");
        return;
    }

    int32_t updated = 0;
    const int32_t count = objects->Num();
    for (int32_t i = 0; i < count; ++i)
    {
        SDK::UObject* obj = objects->GetByIndex(i);
        if (!obj || obj->IsDefaultObject() || !obj->IsA(droneClass))
            continue;

        auto* base = reinterpret_cast<uint8_t*>(obj);
        auto* settings = *reinterpret_cast<SDK::UAuActorPlacementDeveloperSettings**>(base + 0x03E0);
        if (!settings)
        {
            LOG_DEBUG("UpdateActiveDrones: drone instance has null PlacementDeveloperSettings, skipping");
            continue;
        }

        auto* sb = reinterpret_cast<uint8_t*>(settings);
        *reinterpret_cast<float*>(sb + 0x00B4) = *g_drone.speedPerSec;
        *reinterpret_cast<float*>(sb + 0x00B8) = *g_drone.maxRadius;
        *reinterpret_cast<float*>(sb + 0x00BC) = *g_drone.warningRadius;
        *reinterpret_cast<float*>(sb + 0x00C0) = *g_drone.maxHeight;
        *reinterpret_cast<float*>(sb + 0x00C4) = *g_drone.warningHeight;
        ++updated;
    }

    LOG_DEBUG("UpdateActiveDrones: updated %d active drone(s)", updated);
}
