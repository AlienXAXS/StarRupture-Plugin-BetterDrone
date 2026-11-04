#pragma once

// The drone's speed, in the panel's Speed Display Unit, under the drone HUD's
// distance readout in the readout's own font: two text blocks added to the
// HUD, the number lined up with the distance digits and the unit with their
// "m" label. With mph as the unit the distance reads in feet: the HUD's digits
// are hidden and the same two blocks show it. Where the HUD has no room for one, the speed is drawn instead
// just right of the crosshair, outside the mining laser's heat ring, on a
// transparent overlay widget of the loader's like the ring. Shown while the
// drone is out and the panel's Speedometer setting is on.

struct IPluginSelf;

void InitDroneSpeedometer(IPluginSelf* self);

// Takes the text block out of the HUD on the game thread; anywhere else it
// touches no game object.
void ShutdownDroneSpeedometer(IPluginSelf* self);

// Game thread only. Call once per engine tick. Holds no raw UObject pointer
// across ticks.
void TickDroneSpeedometer();
