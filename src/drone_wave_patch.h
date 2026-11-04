#pragma once

struct IPluginSelf;
struct IPluginHookScanner;

// Resolve the wave-patch AOBs. Callable only from OnPluginLoadHooks — the loader
// refuses scans made anywhere else.
void ResolveWavePatch(IPluginSelf* self, IPluginHookScanner* scanner);

// Installs the hook of each part that is on: allowDrone lifts the wave
// restriction (Always Allow Drone), shelter keeps the drone from counting
// toward the player's shelter.
bool InitWavePatch(bool allowDrone, bool shelter);
void ShutdownWavePatch();
