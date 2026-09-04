#pragma once
#include <cstdint>

// Live pointers into the UAuActorPlacementDeveloperSettings CDO.
// Writes take effect immediately on the next game tick that reads them.
//
// These are taken from the SDK's typed fields, never from hardcoded offsets.
// A previous version hardcoded them, the layout shifted by 0x10 in a game
// update, and the six writes below landed on GridMaterialInstance --- the
// TSoftObjectPtr that immediately follows MaxDroneRailLenght in the CDO.
// MaxRailLength overwrote its FSoftObjectPath::AssetPath::PackageName with
// the float bits of the configured value, so the first attempt to enter
// building placement mode crashed the game inside
// UAuBuildingGridSubsystem::GetDecalActor -> FSoftObjectPath::ResolveObjectInternal
// -> FName::AppendString, resolving a name id that does not exist.
struct DroneSettings
{
    float* speedPerSec    = nullptr;  // BuildingDroneSpeedPerSec
    float* maxRadius      = nullptr;  // BuildingDroneMaxRadius
    float* warningRadius  = nullptr;  // BuildingDroneWarningRadius
    float* maxHeight      = nullptr;  // BuildingDroneMaxHeight
    float* warningHeight  = nullptr;  // BuildingDroneWarningHeight
    float* maxRailLength  = nullptr;  // MaxDroneRailLenght
    bool   valid          = false;

    // Snapshot of CDO values captured at init, restored on shutdown.
    float origSpeedPerSec  = 0.f;
    float origMaxRadius    = 0.f;
    float origWarningRadius= 0.f;
    float origMaxHeight    = 0.f;
    float origWarningHeight= 0.f;
    float origMaxRailLength= 0.f;
};

extern DroneSettings g_drone;

bool InitDroneSettings();
void RestoreCDODefaults();
void UpdateActiveDrones();
