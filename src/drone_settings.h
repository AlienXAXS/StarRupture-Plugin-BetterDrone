#pragma once
#include <cstdint>

// Live pointers into the UAuActorPlacementDeveloperSettings CDO.
// Writes take effect immediately on the next game tick that reads them.
struct DroneSettings
{
    float* speedPerSec    = nullptr;  // 0x00B4  BuildingDroneSpeedPerSec
    float* maxRadius      = nullptr;  // 0x00B8  BuildingDroneMaxRadius
    float* warningRadius  = nullptr;  // 0x00BC  BuildingDroneWarningRadius
    float* maxHeight      = nullptr;  // 0x00C0  BuildingDroneMaxHeight
    float* warningHeight  = nullptr;  // 0x00C4  BuildingDroneWarningHeight
    bool   valid          = false;

    // Snapshot of CDO values captured at init, restored on shutdown.
    float origSpeedPerSec  = 0.f;
    float origMaxRadius    = 0.f;
    float origWarningRadius= 0.f;
    float origMaxHeight    = 0.f;
    float origWarningHeight= 0.f;
};

extern DroneSettings g_drone;

bool InitDroneSettings();
void RestoreCDODefaults();
void UpdateActiveDrones();
