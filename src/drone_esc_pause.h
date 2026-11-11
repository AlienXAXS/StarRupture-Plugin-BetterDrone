#pragma once
#include <plugin_interface.h>

// Esc opens the game's pause menu while the drone is out, instead of ending
// the drone. Off by default (Interaction > Esc Opens Pause Menu In Drone Mode).
//
// In drone mode the game binds Esc, and Q with it, to the same handlers that
// end the drone: ACrPlayerControllerBase::NativeOnInputCloseInGameMenu
// recalls the drone before it closes the open widgets. The pause menu itself
// belongs to the on-foot config, which is not bound while the drone is out.
//
// A hook on that handler takes the press when the option is on, the drone is
// out, no menu is up and the Esc key itself was pressed a moment ago (the
// loader's keybind dispatch stamps the time), and opens the pause menu
// through the controller's reflected SetOpenGameMenu(true), the call the
// game's own Esc makes on foot. Any other press, Q included, goes to the
// game's handler as before. The pause menu closing is the game's own: it
// calls SetOpenGameMenu(false) from the widget, and the drone was never
// touched.

struct IPluginSelf;
struct IPluginHookScanner;

// Resolves the game's close-menu handler. Call in OnPluginLoadHooks.
void ResolveDroneEscPause(IPluginSelf* self, IPluginHookScanner* scanner);

// Install the hook and start listening for Esc. Call from PluginInit and
// PluginShutdown when DroneFields::Features::EscPause is on.
bool InitDroneEscPause(IPluginSelf* self);
void ShutdownDroneEscPause(IPluginSelf* self);

// Game thread only. For the cancel-tool detour: true when this cancel is the
// Esc key in the drone with the option on, and the pause menu was opened in
// its place. The caller then skips the game's cancel.
bool DroneEscPauseTakeCancel(void* character);
