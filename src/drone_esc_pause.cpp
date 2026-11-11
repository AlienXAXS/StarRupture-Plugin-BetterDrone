#include "drone_esc_pause.h"
#include "drone_config.h"
#include "drone_fields.h"
#include "drone_interact.h"
#include "plugin_helpers.h"
#include <Chimera_classes.hpp>
#include <windows.h>
#include <atomic>
#include <cstdint>

namespace
{
    // ACrPlayerControllerBase::NativeOnInputCloseInGameMenu(this, value): the
    // controller function behind the game's close-menu input action. Not
    // reflected. It puts a building move back if one can be restored, ends the
    // drone if it is out, then closes the actor widgets and gives the mouse
    // back to the game. The value is the input action's, passed through.
    // Hooked. Null means the key is missing on this build, which turns off
    // only this option (DroneFields::Features::EscPause).
    using CloseInGameMenu_t = void (__fastcall*)(void* controller, const void* value);

    // The first bytes of the function, up to the build-component test and the
    // widget-subsystem flag test. The member offsets and the calls are
    // wildcarded, so the same bytes match on every game build.
    constexpr const char* kCloseInGameMenuPattern =
        "48 89 74 24 18 57 48 83 EC 30 48 8B F9 48 8B F2 48 8B 89 ?? ?? ?? ?? 48 85 C9 74 ?? "
        "E8 ?? ?? ?? ?? 84 C0 0F 85 ?? ?? ?? ?? 80 3D ?? ?? ?? ?? 00 48 8B 87 ?? ?? ?? ?? "
        "48 89 44 24 40 48 89 5C 24 48 74 ??";

    CloseInGameMenu_t g_closeInGameMenu = nullptr;
    CloseInGameMenu_t g_origCloseInGameMenu = nullptr;
    HookHandle        g_hookCloseInGameMenu = nullptr;

    // When the Esc key was last pressed, GetTickCount64, or 0. Written by the
    // keybind callback, whose thread is not guaranteed, and read on the game
    // thread. The loader dispatches the key from the window procedure as the
    // game reads its messages, before the game's input runs, so the stamp
    // is there when the handler is.
    std::atomic<uint64_t> g_escPressedAt{ 0 };

    // How long after the key a handler still counts as that press. Generous
    // for a slow frame; a press that late is left to the game.
    constexpr uint64_t kEscFreshMs = 300;

    // The hook is in. The cancel-tool detour asks before it does anything.
    std::atomic<bool> g_active{ false };

    void OnEscKey(EModKey, EModKeyEvent event)
    {
        if (event == EModKeyEvent::Pressed)
            g_escPressedAt.store(GetTickCount64(), std::memory_order_relaxed);
    }

    // True when this handler call is the Esc key in the drone with the option
    // on. Game thread. The config is read last, so a Q press or a press on
    // foot never touches the INI.
    bool EscInDrone(const SDK::ACrPlayerControllerBase* controller)
    {
        const uint64_t at = g_escPressedAt.load(std::memory_order_relaxed);
        if (!at || GetTickCount64() - at > kEscFreshMs)
            return false;

        if (!controller || controller != LocalPlayerController() || !IsLocalPlayerInDrone())
            return false;

        return DroneConfig::Config::ReadEscPauseInDrone();
    }

    // The pause menu, the way Esc opens it on foot. Idempotent: a second call
    // for the same press finds it already open.
    void OpenPauseMenu(SDK::ACrPlayerControllerBase* controller)
    {
        controller->SetOpenGameMenu(true);
    }

    void __fastcall Detour_NativeOnInputCloseInGameMenu(void* controller, const void* value)
    {
        auto* pc = static_cast<SDK::ACrPlayerControllerBase*>(controller);
        if (EscInDrone(pc))
        {
            // A menu already up closes itself on this press. The same press
            // reaching the drone's handler as well must not recall the drone.
            if (IsGameMenuOpen())
            {
                LOG_DEBUG("DroneEscPause: Esc while a menu is up, drone kept");
                return;
            }

            LOG_DEBUG("DroneEscPause: Esc in the drone, pause menu opened (close-menu input)");
            OpenPauseMenu(pc);
            return;
        }

        g_origCloseInGameMenu(controller, value);
    }
}

void ResolveDroneEscPause(IPluginSelf* self, IPluginHookScanner* scanner)
{
    if (!self || !scanner)
        return;

    g_closeInGameMenu = PluginMap::FunctionAs<CloseInGameMenu_t>(scanner, self, DroneFields::Keys::CloseInGameMenu,
        PLUGIN_MAP_PATTERN(kCloseInGameMenuPattern), PLUGIN_SCAN_FUNCTION_START, PLUGIN_SCAN_FLAG_OPTIONAL);
}

bool InitDroneEscPause(IPluginSelf* self)
{
    if (!self || !g_closeInGameMenu || !self->hooks->Input || !self->hooks->Hooks)
    {
        LOG_WARN("DroneEscPause: no close-menu handler or input dispatch -- Esc will keep ending the drone");
        return false;
    }

    g_hookCloseInGameMenu = self->hooks->Hooks->Install(
        reinterpret_cast<uintptr_t>(g_closeInGameMenu),
        reinterpret_cast<void*>(&Detour_NativeOnInputCloseInGameMenu),
        reinterpret_cast<void**>(&g_origCloseInGameMenu));
    if (!g_hookCloseInGameMenu)
    {
        LOG_WARN("DroneEscPause: hook installation failed -- Esc will keep ending the drone");
        return false;
    }

    self->hooks->Input->RegisterKeybind(EModKey::Escape, EModKeyEvent::Pressed, OnEscKey);
    g_active.store(true, std::memory_order_relaxed);

    LOG_INFO("DroneEscPause: close-menu handler at 0x%llX", reinterpret_cast<uintptr_t>(g_closeInGameMenu));
    return true;
}

void ShutdownDroneEscPause(IPluginSelf* self)
{
    g_active.store(false, std::memory_order_relaxed);

    if (self && self->hooks->Input)
        self->hooks->Input->UnregisterKeybind(EModKey::Escape, EModKeyEvent::Pressed, OnEscKey);

    if (g_hookCloseInGameMenu && self)
        self->hooks->Hooks->Remove(g_hookCloseInGameMenu);

    g_hookCloseInGameMenu = nullptr;
    g_origCloseInGameMenu = nullptr;
    g_escPressedAt.store(0, std::memory_order_relaxed);
}

bool DroneEscPauseTakeCancel(void* character)
{
    if (!g_active.load(std::memory_order_relaxed) || !character || character != LocalPlayerCharacter())
        return false;

    SDK::ACrPlayerControllerBase* pc = LocalPlayerController();
    if (!EscInDrone(pc) || IsGameMenuOpen())
        return false;

    LOG_DEBUG("DroneEscPause: Esc in the drone, pause menu opened (cancel-tool input)");
    OpenPauseMenu(pc);
    return true;
}
