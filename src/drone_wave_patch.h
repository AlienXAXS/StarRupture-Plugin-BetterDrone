#pragma once

struct IPluginSelf;
struct IPluginHookScanner;

// Resolve the wave-patch AOBs. Callable only from OnPluginLoadHooks — the loader
// refuses scans made anywhere else.
void ResolveWavePatch(IPluginSelf* self, IPluginHookScanner* scanner);

// Installs both hooks: one lifts the wave restriction (Always Allow Drone),
// the other keeps the drone from counting toward the player's shelter. Each
// passes through while its part is off.
bool InitWavePatch();
void ShutdownWavePatch();
