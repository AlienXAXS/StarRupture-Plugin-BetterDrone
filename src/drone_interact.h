#pragma once

// Lets the player open a building's UI while flying the building drone, the
// same way walking up to it and pressing the interact key does on foot.
//
// Two separate things stop this in the stock game:
//
//   * ACrPlayerControllerBase::OnInteractableTargetsChanged returns early --
//     after clearing CurrentInteractableActor -- whenever
//     ACrCharacterPlayerBase::IsBuildingDroneActive is true, so nothing is ever
//     targeted while the drone is out. The same function also measures the
//     interaction range from the character's root component, which stays parked
//     wherever the player left their body.
//
//   * The interact key lives in the BasePlayerAlive input config, which
//     ActivateBuildingDrone unbinds in favour of the Drone config, so the key
//     may never reach ACrPlayerControllerBase::NativeOnInputInteract at all.
//
// InitDroneInteract hooks around the first and supplies the second from the
// modloader's own keybind dispatch.

bool InitDroneInteract();
void ShutdownDroneInteract();
