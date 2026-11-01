#pragma once

#include <plugin_interface.h>
#include <Chimera_classes.hpp>
#include <Chimera_structs.hpp>
#include <ChimeraUI_classes.hpp>
#include <AuActorPlacement_classes.hpp>
#include <AuItems_structs.hpp>
#include <AuWeapon_classes.hpp>
#include <AuWeapon_structs.hpp>
#include <Engine_classes.hpp>
#include <plugin_map.h>
#include "plugin_helpers.h"

#include <cstdint>

// Every member of a game class BetterDrone reads or writes, taken from the
// loader's map for the running game build (see plugin_map.h). Resolved once in
// OnPluginLoadHooks by DroneFields::Resolve; read on the game thread with
// In(object), which is null when the object is null. Structs that are only
// stepped through (an array's elements, a map's pairs) are covered by a size
// check instead of a field per member. Engine members (AActor, ACharacter,
// UWidget, FHitResult, ...) stay compiled in.

#define DRONE_FIELD(Class, Member) SR_FIELD(decltype(SDK::Class::Member), Class, Member, 0)

namespace DroneFields
{
	// The player character
	inline auto BuildingDrone     = DRONE_FIELD(ACrCharacterPlayerBase, BuildingDrone);
	inline auto Status            = DRONE_FIELD(ACrCharacterPlayerBase, Status);
	inline auto DroneCamera       = DRONE_FIELD(ACrCharacterPlayerBase, DroneCamera);
	inline auto DroneCameraColl   = DRONE_FIELD(ACrCharacterPlayerBase, DroneCameraColl);
	inline auto IsDead            = DRONE_FIELD(ACrCharacterPlayerBase, bDead);
	inline auto GemAttributes     = DRONE_FIELD(ACrCharacterPlayerBase, GemAttributes);
	inline auto MiningComponent   = DRONE_FIELD(ACrCharacterPlayerBase, MiningComponent);
	inline auto WeaponSystem      = DRONE_FIELD(ACrCharacterPlayerBase, WeaponSystem);
	inline auto WeaponAttributes  = DRONE_FIELD(ACrCharacterPlayerBase, WeaponAttributes);
	inline auto AbilitySystem     = DRONE_FIELD(ACrCharacterPlayerBase, AbilitySystem);

	// The drone
	inline auto SphereComponent             = DRONE_FIELD(ACrCharacterDroneBase, SphereComponent);
	inline auto PlacementDeveloperSettings  = DRONE_FIELD(ACrCharacterDroneBase, PlacementDeveloperSettings);
	inline auto IdleSound                   = DRONE_FIELD(ACrCharacterDroneBase, IdleSound);
	inline auto MovementSound               = DRONE_FIELD(ACrCharacterDroneBase, MovementSound);
	inline auto RotationSound               = DRONE_FIELD(ACrCharacterDroneBase, RotationSound);
	inline auto StateAudioComponents        = DRONE_FIELD(ACrBuildingActorBase, StateAudioComponents);

	// The player controller. PlayerControlState has no member in the Game-SDK
	// (it sits in padding), so it is a native field: the loader's row for the
	// running build, or none and the plugin is refused.
	inline auto CrChar                   = DRONE_FIELD(ACrPlayerControllerBase, CrChar);
	inline auto CurrentInteractableActor = DRONE_FIELD(ACrPlayerControllerBase, CurrentInteractableActor);
	inline auto PlayerControlState = PluginField<uint8_t>{ { "ACrPlayerControllerBase::PlayerControlState",
		"ACrPlayerControllerBase", "PlayerControlState", PLUGIN_FIELD_NATIVE, sizeof(uint8_t), 0xE88 } };
	inline auto BuildingMenuPlayer = DRONE_FIELD(UCrUW_BuildingMenu, Player);

	// Drone settings and the map's fog of war
	inline auto SpeedPerSec   = DRONE_FIELD(UAuActorPlacementDeveloperSettings, BuildingDroneSpeedPerSec);
	inline auto MaxRadius     = DRONE_FIELD(UAuActorPlacementDeveloperSettings, BuildingDroneMaxRadius);
	inline auto WarningRadius = DRONE_FIELD(UAuActorPlacementDeveloperSettings, BuildingDroneWarningRadius);
	inline auto MaxHeight     = DRONE_FIELD(UAuActorPlacementDeveloperSettings, BuildingDroneMaxHeight);
	inline auto WarningHeight = DRONE_FIELD(UAuActorPlacementDeveloperSettings, BuildingDroneWarningHeight);
	inline auto SphereRadius  = DRONE_FIELD(UAuActorPlacementDeveloperSettings, BuildingDroneSphereRadius);
	inline auto FogUncoverRadius   = DRONE_FIELD(UCrMapMenuDevSettings, FogOfWarPlayerUncoverRadius);
	inline auto MinFootstepDistance = DRONE_FIELD(UCrMapMenuDevSettings, MinFootstepDistance);
	inline auto FogRadiusMultiplier = DRONE_FIELD(UCrGemAttributeSet, FogOfWarRadiusMultiplier);

	// Safe interiors and input
	inline auto IsSafeInterior = DRONE_FIELD(UInteriorOverlapComponent, bIsSafeInterior);
	inline auto NativeInputActions = DRONE_FIELD(UCrInputConfig, NativeInputActionsWithHandlers);
	inline auto NativeInputAction  = DRONE_FIELD(FCrNativeInputAction, InputAction);
	inline auto NativeInputHandler = DRONE_FIELD(FCrNativeInputAction, NativeInputHandler);

	// The mining laser
	inline auto HarvesterMiningDamage = DRONE_FIELD(UCrGemAttributeSet, HarvesterMiningDamageMultiplier);
	inline auto FireRateMod           = DRONE_FIELD(UCrWeaponAttributeSet, FireRateModMultiplier);
	inline auto LastEquippedUnarmed   = DRONE_FIELD(UCrWeaponComponent, LastEquippedUnarmed);
	inline auto EquippedWeapon        = DRONE_FIELD(UAuWeaponComponent, EquippedWeapon);
	inline auto EquippedItemData      = DRONE_FIELD(FAuEquippedWeapon, ItemDataLoaded);
	inline auto MiningTypeDamage      = DRONE_FIELD(UCrWeaponItemDataBase, MiningTypeDamage);
	inline auto RoundsPerMinute       = DRONE_FIELD(UAuWeaponItemDataBase, RoundsPerMinute);
	inline auto BaseRange             = DRONE_FIELD(UAuWeaponItemDataBase, BaseRange);
	inline auto MaxStack              = DRONE_FIELD(UAuItemDataBase, MaxStack);
	inline auto CurrentlyMinedOre     = DRONE_FIELD(UCrMiningComponent, CurrentlyMinedOre);
	inline auto MinedOreActor         = DRONE_FIELD(FCrOreMiningState, OreActor);
	inline auto MinedVeinMaterial     = DRONE_FIELD(FCrOreMiningState, InfiniteOrePhysicalMaterial);
	inline auto MinedVeinData         = DRONE_FIELD(FCrOreMiningState, InfiniteOreData);
	inline auto VeinResource          = DRONE_FIELD(FCrInfiniteOreData, Resource);
	inline auto VeinGrantCount        = DRONE_FIELD(FCrInfiniteOreData, GrantingMomentResourceCount);
	inline auto OreData               = DRONE_FIELD(ACrOreActor, OreData);
	inline auto OreResource           = DRONE_FIELD(ACrOreActor, Resource);
	inline auto OreInitMaxCount       = DRONE_FIELD(ACrOreActor, InitMaxCount);
	inline auto OreWeakSpots          = DRONE_FIELD(ACrOreActor, WeakSpotsDataContainer);
	inline auto OreIsDepleted         = DRONE_FIELD(FCrOreRuntimeData, bIsDepleted);
	inline auto OreCurrentCount       = DRONE_FIELD(FCrOreRuntimeData, CurrentResourceCount);
	inline auto OreMaxCount           = DRONE_FIELD(FCrOreRuntimeData, MaxResourceCount);
	inline auto WeakSpotItems         = DRONE_FIELD(FCrWeakSpotsContainer, Items);
	inline auto WeakSpotHealth        = DRONE_FIELD(FCrWeakSpotRuntimeData, CurrentMiningHealth);
	inline auto WeakSpotCount         = DRONE_FIELD(FCrWeakSpotRuntimeData, ResourceCount);
	inline auto WeakSpotResource      = DRONE_FIELD(FCrWeakSpotRuntimeData, Resource);
	inline auto InventorySlots        = DRONE_FIELD(UCrInventoryComponent, Slots);
	inline auto SlotItemId            = DRONE_FIELD(FCrInventorySlot, ItemId);
	inline auto ItemIdHandle          = DRONE_FIELD(FAuItemId, Handle);
	inline auto OreMappingData        = DRONE_FIELD(UCrOreSubsystem, InfiniteOrePhysMatMappingData);
	inline auto PhysMatOreDataMap     = DRONE_FIELD(UCrInfiniteOrePhysMatData, PhysMatOreDataMap);

	// The map marker
	inline auto MapDataHelper      = DRONE_FIELD(ACrGameStateBase, MapMenuDataReplicationHelper);
	inline auto BuildingsMarkers   = DRONE_FIELD(ACrMapMenuDataReplicationHelper, BuildingsMarkerDataContainer);
	inline auto BuildingsMarkerData = DRONE_FIELD(FBuildingsMarkerDataContainer, BuildingsMarkerData);
	inline auto MarkerBuildingHandle = DRONE_FIELD(FBuildingMarkerDataFastArrayItem, BuildingHandle);
	inline auto MarkerLocation     = DRONE_FIELD(FBuildingMarkerDataFastArrayItem, Location);
	inline auto MapMenuMapArea     = DRONE_FIELD(UCrUW_MapMenu, MapMenuMapArea);
	inline auto MapMassMarkers     = DRONE_FIELD(UCrUW_MapMenuMapArea, MapMenuMassMarkers);
	inline auto MapTerrain         = DRONE_FIELD(UCrUW_MapMenuMapArea, MapMenuTerrain);
	inline auto MapCanvasPanel     = DRONE_FIELD(UCrUW_MapMenuMapArea, CanvasPanelMapArea);
	inline auto MarkerWidget       = DRONE_FIELD(FEntityMarkerUIRepresentation, Marker);
	inline auto MapPivotPoint      = DRONE_FIELD(UCrMapMenuDevSettings, MapAreaPivotPoint);
	inline auto MarkerColorData    = DRONE_FIELD(UCrMapMenuDevSettings, MarkerStatusesColorData);
	inline auto MarkerPlayerColour = DRONE_FIELD(UCrMapMenuMarkerDefaultColorData, MarkerPlayer);

	// The drone HUD
	inline auto HudPlayerCharacter = DRONE_FIELD(UCrUW_BuildingDroneHud, PlayerCharacter);
	inline auto HudDistanceT = DRONE_FIELD(UCrUW_BuildingDroneHud, DistanceValueT);
	inline auto HudDistanceH = DRONE_FIELD(UCrUW_BuildingDroneHud, DistanceValueH);
	inline auto HudDistanceD = DRONE_FIELD(UCrUW_BuildingDroneHud, DistanceValueD);
	inline auto HudDistanceU = DRONE_FIELD(UCrUW_BuildingDroneHud, DistanceValueU);
	inline auto HudDistanceM = DRONE_FIELD(UCrUW_BuildingDroneHud, DistanceValueM);

	// Structs stepped through as array or map elements
	inline auto NativeInputActionSize = SR_SIZE_CHECK(FCrNativeInputAction);
	inline auto InventorySlotSize     = SR_SIZE_CHECK(FCrInventorySlot);
	inline auto WeakSpotSize          = SR_SIZE_CHECK(FCrWeakSpotRuntimeData);
	inline auto MarkerItemSize        = SR_SIZE_CHECK(FBuildingMarkerDataFastArrayItem);
	inline auto MarkerRepresentationSize = SR_SIZE_CHECK(FEntityMarkerUIRepresentation);
	inline auto VeinDataSize          = SR_SIZE_CHECK(FCrInfiniteOreData);

	// Blueprint (_C) members have no offset in the Game-SDK: found by name on
	// first use. A member that is not there leaves its feature off.
	inline PluginLazyField<SDK::UStaticMeshComponent*> DroneMesh{ "SM_BuidingDrone" };
	inline PluginLazyField<SDK::UCapsuleComponent*>    DroneCapsule{ "Capsule" };
	inline PluginLazyField<SDK::FGameplayTag>          OverheatTag{ "OverheatTag" };
	inline PluginLazyField<double>                     SingleStackDuration{ "SingleStackDuration" };
	inline PluginLazyField<float>                      DefaultStartDelay{ "DefaultStartDelayDuration" };
	inline PluginLazyField<float>                      OverheatStartDelay{ "OverheatStartDelayDuration" };

	// Asks the loader for every row above. False when any did not resolve; the
	// loader has then recorded the failure and refuses the plugin.
	inline bool Resolve(IPluginSelf* self, IPluginHookScanner* scanner)
	{
		return PluginMap::ResolveFields(scanner, self,
			BuildingDrone, Status, DroneCamera, DroneCameraColl, IsDead, GemAttributes, MiningComponent,
			WeaponSystem, WeaponAttributes, AbilitySystem,
			SphereComponent, PlacementDeveloperSettings, IdleSound, MovementSound, RotationSound, StateAudioComponents,
			CrChar, CurrentInteractableActor, PlayerControlState, BuildingMenuPlayer,
			SpeedPerSec, MaxRadius, WarningRadius, MaxHeight, WarningHeight, SphereRadius,
			FogUncoverRadius, MinFootstepDistance, FogRadiusMultiplier,
			IsSafeInterior, NativeInputActions, NativeInputAction, NativeInputHandler,
			HarvesterMiningDamage, FireRateMod, LastEquippedUnarmed, EquippedWeapon, EquippedItemData,
			MiningTypeDamage, RoundsPerMinute, BaseRange, MaxStack, CurrentlyMinedOre, MinedOreActor,
			MinedVeinMaterial, MinedVeinData, VeinResource, VeinGrantCount, OreData, OreResource, OreInitMaxCount,
			OreWeakSpots, OreIsDepleted, OreCurrentCount, OreMaxCount, WeakSpotItems, WeakSpotHealth, WeakSpotCount,
			WeakSpotResource, InventorySlots, SlotItemId, ItemIdHandle, OreMappingData, PhysMatOreDataMap,
			MapDataHelper, BuildingsMarkers, BuildingsMarkerData, MarkerBuildingHandle, MarkerLocation,
			MapMenuMapArea, MapMassMarkers, MapTerrain, MapCanvasPanel, MarkerWidget, MapPivotPoint,
			MarkerColorData, MarkerPlayerColour,
			HudPlayerCharacter, HudDistanceT, HudDistanceH, HudDistanceD, HudDistanceU, HudDistanceM,
			NativeInputActionSize, InventorySlotSize, WeakSpotSize, MarkerItemSize, MarkerRepresentationSize,
			VeinDataSize);
	}

	// A game function by its map key, with this plugin's own pattern as the
	// fallback. Every address BetterDrone resolves is a function entry, so each
	// request declares PLUGIN_SCAN_FUNCTION_START: some get a detour written
	// over them and the rest are called directly. The loader then checks the
	// match against the executable's exception directory instead of trusting
	// that the bytes lined up -- a pattern that drifted into the middle of some
	// other function is refused rather than used. Optional: a miss leaves the
	// rest of BetterDrone working, and the loader still lists it for the user.
	inline uintptr_t ResolveFunction(IPluginSelf* self, IPluginHookScanner* scanner,
	                                 const char* key, const char* pattern)
	{
		return PluginMap::Function(scanner, self, key, pattern, PLUGIN_SCAN_FUNCTION_START, PLUGIN_SCAN_FLAG_OPTIONAL);
	}

	// The member's value, or a fallback when the object is null.
	template <class T, class Object>
	T ValueOr(const PluginField<T>& field, const Object* object, T fallback = T{})
	{
		const T* at = field.In(static_cast<const void*>(object));
		return at ? *at : fallback;
	}

	// The loader's property reader for Blueprint members, or null.
	inline const IPluginObjectProperties* ObjectProperties()
	{
		IPluginSelf* self = GetSelf();
		return self && self->hooks ? self->hooks->ObjectProperties : nullptr;
	}

	// True when the character is flying the building drone.
	inline bool InDrone(const SDK::ACrCharacterPlayerBase* character)
	{
		return ValueOr(Status, character, SDK::EPlayerCharacterStatus::None) == SDK::EPlayerCharacterStatus::BuildingDrone;
	}

	inline bool Dead(const SDK::ACrCharacterPlayerBase* character)
	{
		return ValueOr(IsDead, character, false);
	}

	// The drone HUD's speed text (the "U" digit), or null.
	inline SDK::UTextBlock* DistanceU(const SDK::UCrUW_BuildingDroneHud* hud)
	{
		return ValueOr(HudDistanceU, hud);
	}

	// The character the HUD belongs to, or null.
	inline SDK::ACrCharacterPlayerBase* HudPilot(const SDK::UCrUW_BuildingDroneHud* hud)
	{
		return ValueOr(HudPlayerCharacter, hud);
	}
}
