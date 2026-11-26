#include "drone_map_marker.h"
#include "drone_config.h"
#include "drone_interact.h"
#include "drone_marker_icon.h"
#include "object_ref.h"
#include "overlay_widget.h"
#include "plugin_helpers.h"
#include "drone_fields.h"
#include <plugin_interface.h>
#include <plugin_map_overlay.h>
#include <Chimera_classes.hpp>
#include <ChimeraUI_classes.hpp>
#include <Engine_classes.hpp>
#include <UMG_classes.hpp>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <windows.h>

namespace
{
    constexpr uint64_t kLogIntervalMs = 5000;

    // The player marker colour when the game's colour asset is not loaded: the
    // cyan of the player arrow on the map, as an ImGui colour (R 119, G 225, B 233).
    constexpr unsigned int kFallbackColour = 0xFFE9E177u;

    // What the render callback draws, as plain numbers.
    struct Snapshot
    {
        bool                      valid;
        PluginMapOverlay::Snapshot map;      // the map's transform, dpi and canvas
        float                     x, y;      // viewport pixels
        float                     yawDeg;    // the drone's world yaw
        unsigned int              colour;
    };

    std::mutex g_lock;
    Snapshot   g_snapshot = {};

    OverlayWidget g_overlay;

    // Tick only.
    uint64_t g_lastLogMs = 0;
    bool     g_failed    = false;
    bool     g_loggedColourFallback = false;
    bool     g_loggedWidgetMap = false;

    // ---- render thread --------------------------------------------------------

    void RenderMarker(IModLoaderImGui* ui)
    {
        Snapshot s;
        {
            std::lock_guard<std::mutex> guard(g_lock);
            s = g_snapshot;
        }
        if (!s.valid)
            return;

        PluginDrawList dl = ui->GetForegroundDrawList();
        PluginMapOverlay::PushClip(ui, dl, s.map);
        DroneMarkerIcon::Draw(ui, dl, s.x, s.y, s.yawDeg, s.map.dpi, s.colour);
        PluginMapOverlay::PopClip(ui, dl);
    }

    // ---- game thread: reads, each under SEH, into plain data ----------------------

    // The widget map is read at a layout computed from the running game; when it
    // cannot be this map's, the marker stays hidden. Said once.
    void LogWidgetMapOnce(const char* why)
    {
        if (g_loggedWidgetMap)
            return;
        g_loggedWidgetMap = true;
        LOG_WARN("DroneMapMarker: the map widgets cannot be read (%s); no marker will show", why);
    }

    // Everything the placement needs from the drone.
    struct DroneState
    {
        bool   ok;
        double x, y, z, yawDeg;
    };

    bool ReadDrone(SDK::ACrPlayerControllerBase* pc, DroneState* out)
    {
        __try
        {
            memset(out, 0, sizeof(*out));
            SDK::ACrCharacterPlayerBase* character = DroneFields::ValueOr(DroneFields::CrChar, pc);
            SDK::ACrCharacterDroneBase*  drone     = DroneFields::ValueOr(DroneFields::BuildingDrone, character);
            if (!drone)
                return false;

            const SDK::FVector location = drone->K2_GetActorLocation();
            out->x      = location.X;
            out->y      = location.Y;
            out->z      = location.Z;
            out->yawDeg = drone->K2_GetActorRotation().Yaw;
            out->ok = true;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // The colour of the local player's arrow: MarkerPlayer in the map's marker
    // colour asset, which the map settings point to (assumed from the asset's
    // field names, not read from the arrow widget). The asset is read through
    // the settings' soft pointer, which holds it only once the game has loaded
    // it, so it is checked every tick and nothing is kept.
    bool ReadPlayerMarkerColour(SDK::UCrMapMenuDevSettings* settings, float* linearRgb)
    {
        __try
        {
            auto* soft = DroneFields::MarkerColorData.In(settings);
            SDK::UCrMapMenuMarkerDefaultColorData* colours = soft ? soft->Get() : nullptr;
            if (!colours || !colours->IsA(SDK::UCrMapMenuMarkerDefaultColorData::StaticClass()))
                return false;

            const auto* player = DroneFields::MarkerPlayerColour.In(colours);
            if (!player)
                return false;

            linearRgb[0] = player->R;
            linearRgb[1] = player->G;
            linearRgb[2] = player->B;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    unsigned int ToImGuiColour(const float* linear)
    {
        unsigned int c[3];
        for (int i = 0; i < 3; ++i)
        {
            const float v = linear[i] <= 0.0f ? 0.0f : (linear[i] >= 1.0f ? 1.0f : linear[i]);
            const float s = v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
            c[i] = static_cast<unsigned int>(s * 255.0f + 0.5f);
        }
        return 0xFF000000u | (c[2] << 16) | (c[1] << 8) | c[0];
    }

    // Publishes the snapshot, then shows or hides the widget to match.
    void Publish(const Snapshot& snapshot)
    {
        {
            std::lock_guard<std::mutex> guard(g_lock);
            g_snapshot = snapshot;
        }

        g_overlay.SetShown(snapshot.valid);
    }

    void Hide()
    {
        Snapshot none = {};
        Publish(none);
    }

    void LogDisagreement(int checked)
    {
        const uint64_t now = GetTickCount64();
        if (now - g_lastLogMs < kLogIntervalMs)
            return;

        g_lastLogMs = now;
        LOG_INFO("DroneMapMarker: hidden, none of %d map markers sits within %.0f px of where the "
                 "terrain transform puts it", checked, PluginMapOverlay::kTolerancePx);
    }

    // ---- following the drone ---------------------------------------------------
    //
    // The map's pan and zoom are kept in a matrix of the map area, which the
    // widget's own tick turns into the terrain's position every frame, so a
    // position written to the terrain from outside is overwritten the next
    // frame. Each tick this asks the map area to centre on the drone's world
    // location instead, the call behind the game's own "Center on player". It
    // is absolute against the matrix, so repeating it cannot overshoot.
    //
    // The matrix is read back after each call. At the start of the next tick it
    // should still be where that call left it; a different pan means the player
    // moved the map (mouse drag, arrow keys, gamepad), and follow stops until the
    // map is reopened or the Map Centre Key is pressed. A different scale is a zoom and is followed through.
    // When the drone is gone the map is centred on the player's body, as the
    // game opens it, and nothing else of the map was changed.
    //
    // The Map Centre Key (the game's Center on player key, C) switches follow
    // off and on without being consumed. The game still centres on the player
    // when it is pressed, and follow stays off, so the map stays on the body.
    // Pressed again, follow resumes on the drone. The game's key is a data
    // table row on the map widget; reading it needs version map rows this
    // plugin does not have, so the loader's keybind stands in for it. Only the
    // keyboard key toggles: the on-screen button and the gamepad still centre
    // the game's map and read as a hand.

    // UE::Math::TVector<float>, the location CenterOnLocation takes.
    struct MapPoint
    {
        float x, y, z;
    };

    typedef void (__fastcall* CenterOnLocation_t)(SDK::UCrUW_MapMenuMapArea* area, MapPoint* location);

    // Resolved during OnPluginLoadHooks; null when the loader has no row for
    // this build, which turns the feature off. The pattern is mapping input only.
    CenterOnLocation_t g_centerOnLocation = nullptr;

    constexpr const char* kCenterOnLocationPattern =
        "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 40 48 83 79 30 00 48 8B F2 0F 29 74 24 30 48 8B F9 74 ?? "
        "E8 ?? ?? ?? ?? 48 8B D0 48 8D 4F 30 E8 ?? ?? ?? ?? 84 C0 74 ?? 48 8B 4F 30 48 85 C9 74 ?? 48 8D 54 24 20";

    // A pan under this (widget units), summed over the ticks since the last zoom,
    // is the float noise of the matrix, not a hand.
    constexpr float kPanTolerance   = 0.25f;
    constexpr float kScaleTolerance = 1e-4f;

    // For this long after follow starts, a pan the game made itself while opening
    // the map (centring on the player, restoring a saved pan) is followed from
    // where it is instead of ending follow. Time, not ticks: the frame rate
    // decides how many ticks the game's opening takes.
    constexpr uint64_t kSettleMs = 500;

    struct Follow
    {
        bool  stopped;     // off until the map is reopened or the Map Centre Key is pressed
        bool  onBody;      // the player pressed the Map Centre Key: the map stays on the body
        bool  centred;     // the map has been moved this open
        bool  haveBase;    // scale, tx and ty hold the matrix as the last call left it
        float scale;
        float tx, ty;
        float driftX, driftY;   // pan since the last call, summed over the ticks
        uint64_t startMs;       // GetTickCount64 at the first call; 0 before it
    };

    Follow                                g_follow = {};
    ObjectRef<SDK::UCrUW_MapMenuMapArea>  g_followArea;
    bool                                  g_followFailed = false;

    char g_centreKeyName[64] = {};

    // Raised by the keybind callback, consumed on the game thread in FollowImpl.
    std::atomic<bool> g_centrePress{ false };

    void OnCentreKey(EModKey, EModKeyEvent)
    {
        g_centrePress.store(true, std::memory_order_relaxed);
    }

    struct MatrixRead
    {
        float scale;
        float tx, ty;   // the translation row, widget units
    };

    bool ReadMatrix(SDK::UCrUW_MapMenuMapArea* area, MatrixRead* out)
    {
        __try
        {
            const MapAreaMatrixData* matrix = DroneFields::MapAreaMatrix.In(area);
            if (!matrix)
                return false;

            out->scale = matrix->m[0][0];
            out->tx    = matrix->m[3][0];
            out->ty    = matrix->m[3][1];
            return std::isfinite(out->scale) && std::isfinite(out->tx) && std::isfinite(out->ty) && out->scale > 0.0f;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // Where the player's body stands: the map's own player marker sits there.
    bool ReadBody(SDK::ACrPlayerControllerBase* pc, MapPoint* out)
    {
        __try
        {
            SDK::ACrCharacterPlayerBase* character = DroneFields::ValueOr(DroneFields::CrChar, pc);
            if (!character)
                return false;

            const SDK::FVector location = character->K2_GetActorLocation();
            out->x = static_cast<float>(location.X);
            out->y = static_cast<float>(location.Y);
            out->z = static_cast<float>(location.Z);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // True once the map area has its size, so the call to centre on a location
    // has a half size to work from.
    bool AreaLaidOut(SDK::UCrUW_MapMenuMapArea* area)
    {
        SDK::UWidget* canvas = DroneFields::ValueOr(PluginMapOverlay::Fields::MapCanvasPanel, area);
        PluginMapOverlay::Geo geo;
        return canvas && PluginMapOverlay::SampleGeo(canvas, LocalPlayerController(), &geo) &&
               PluginMapOverlay::LaidOut(geo);
    }

    // Drops the state, touching no game object.
    void ForgetFollow()
    {
        g_follow = {};
        g_followArea.Reset();
    }

    void StopFollow(const char* why)
    {
        if (g_follow.stopped)
            return;

        g_follow.stopped = true;
        LOG_INFO("DroneMapFollow: map follow off until the map is reopened or the map centre key is pressed: %s", why);
    }

    // Ends follow wherever it stands (the drone was recalled or died, the map
    // closed or replaced, the world ended): the map goes back on the player's
    // body if it is still there and this code moved it. A stopped follow leaves
    // the map where the player put it, and one switched off by the Map Centre
    // Key leaves it on the body.
    void EndFollow()
    {
        SDK::UCrUW_MapMenuMapArea* area = g_followArea.Get();
        if (area && g_follow.centred && !g_follow.stopped && !g_follow.onBody)
        {
            MapPoint body;
            if (ReadBody(LocalPlayerController(), &body))
                g_centerOnLocation(area, &body);
        }
        ForgetFollow();
    }

    void FollowImpl()
    {
        // Taken first: a press with no map open in the drone is dropped.
        const bool centrePress = g_centrePress.exchange(false, std::memory_order_relaxed);

        if (!IsLocalPlayerInDrone())
        {
            EndFollow();
            return;
        }

        SDK::UCrUW_MapMenu*        map  = PluginMapOverlay::ActiveLocalMap();
        SDK::UCrUW_MapMenuMapArea* area = DroneFields::ValueOr(PluginMapOverlay::Fields::MapMenuMapArea, map);
        if (!area)
        {
            EndFollow();
            return;
        }

        // A new map widget is a new open.
        if (g_followArea.Get() != area)
        {
            EndFollow();
            g_followArea.Set(area);
        }

        Follow& f = g_follow;

        // Follow is off or on; the game centres on the player by itself. When
        // it goes back on, the map is centred on the drone at once and the
        // game's own centring is waited out like the one on opening the map,
        // so it does not read as a hand.
        if (centrePress)
        {
            f.onBody   = !f.onBody;
            f.stopped  = false;
            f.haveBase = false;
            f.driftX   = 0.0f;
            f.driftY   = 0.0f;
            LOG_DEBUG("DroneMapFollow: %s", f.onBody ? "map stays on the player" : "following the drone on the map");
        }

        if (f.stopped || f.onBody)
            return;

        DroneState drone;
        if (!ReadDrone(LocalPlayerController(), &drone))
            return;

        // Until the first call lands, a map that is not set up yet is waited for.
        // Once it has, a matrix that cannot be read ends follow.
        MatrixRead now;
        const bool readable = ReadMatrix(area, &now);
        if (!f.haveBase && !(readable && AreaLaidOut(area)))
            return;

        if (!readable)
        {
            StopFollow("the map's pan and zoom could not be read");
            return;
        }

        const uint64_t nowMs = GetTickCount64();
        if (f.haveBase)
        {
            const bool zoomed  = std::fabs(now.scale - f.scale) > kScaleTolerance;
            const bool settled = nowMs - f.startMs >= kSettleMs;
            if (zoomed || !settled)
            {
                f.driftX = 0.0f;
                f.driftY = 0.0f;
            }
            else
            {
                f.driftX += now.tx - f.tx;
                f.driftY += now.ty - f.ty;
                if (std::fabs(f.driftX) > kPanTolerance || std::fabs(f.driftY) > kPanTolerance)
                {
                    StopFollow("the map was moved by hand");
                    return;
                }
            }
        }

        MapPoint target = { static_cast<float>(drone.x), static_cast<float>(drone.y), static_cast<float>(drone.z) };
        g_centerOnLocation(area, &target);

        MatrixRead after;
        if (!ReadMatrix(area, &after))
        {
            StopFollow("the map's pan and zoom could not be read");
            return;
        }

        if (!f.centred)
            LOG_DEBUG("DroneMapFollow: following the drone on the map");

        if (!f.haveBase)
            f.startMs = nowMs;

        f.centred  = true;
        f.haveBase = true;
        f.scale    = after.scale;
        f.tx       = after.tx;
        f.ty       = after.ty;
    }

    // Apart from FollowImpl: a function with __try may not hold objects that need
    // unwinding, and the code it calls may.
    bool RunFollowGuarded()
    {
        __try
        {
            FollowImpl();
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // Apart from EndFollow, for the same reason: shutdown calls it outside
    // RunFollowGuarded.
    bool EndFollowGuarded()
    {
        __try
        {
            EndFollow();
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void TickImpl()
    {
        if (!IsLocalPlayerInDrone())
        {
            Hide();
            return;
        }

        PluginMapOverlay::Snapshot map;
        const PluginMapOverlay::Result result = PluginMapOverlay::Sample(&map);
        if (result.widgetMapFault)
            LogWidgetMapOnce(result.widgetMapFault);
        if (result.status == PluginMapOverlay::Status::Mismatch)
            LogDisagreement(result.checked);
        if (result.status != PluginMapOverlay::Status::Ready)
        {
            Hide();
            return;
        }

        // The drone is read only while the map is open and laid out.
        DroneState drone;
        if (!ReadDrone(LocalPlayerController(), &drone))
        {
            Hide();
            return;
        }

        Snapshot s = {};
        s.valid  = true;
        s.map    = map;
        PluginMapOverlay::ToPixel(map, drone.x, drone.y, &s.x, &s.y);
        s.yawDeg = static_cast<float>(drone.yawDeg);

        s.colour = PlayerMarkerColour();
        Publish(s);
    }

    // Apart from TickImpl: a function with __try may not hold objects that need
    // unwinding, and the code it calls may.
    bool RunGuarded()
    {
        __try
        {
            TickImpl();
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
}

unsigned int PlayerMarkerColour()
{
    float linear[3];
    SDK::UCrMapMenuDevSettings* settings = SDK::UCrMapMenuDevSettings::GetDefaultObj();
    if (settings && ReadPlayerMarkerColour(settings, linear))
        return ToImGuiColour(linear);

    if (!g_loggedColourFallback)
    {
        g_loggedColourFallback = true;
        LOG_INFO("DroneMapMarker: the game's player marker colour is not available, using the default cyan");
    }
    return kFallbackColour;
}

void InitDroneMapMarker(IPluginSelf* self)
{
    if (!self || !self->hooks || !self->hooks->UI)
        return;

    if (!g_overlay.Register(self, "BetterDrone Map Marker", &RenderMarker))
        LOG_WARN("DroneMapMarker: the loader would not register the overlay widget, no marker will show.");
}

void ShutdownDroneMapMarker(IPluginSelf* self)
{
    {
        std::lock_guard<std::mutex> guard(g_lock);
        g_snapshot = Snapshot{};
    }

    g_overlay.Unregister(self);
}

void TickDroneMapMarker(float)
{
    if (g_failed || !g_overlay.IsRegistered())
        return;

    // Fail closed: a fault while reading the map stops the marker for the rest
    // of the session instead of repeating every tick.
    if (!RunGuarded())
    {
        g_failed = true;
        Hide();
        LOG_ERROR("DroneMapMarker: access violation while reading the map; marker switched off until RELOAD");
    }
}

void ResolveDroneMapFollow(IPluginSelf* self, IPluginHookScanner* scanner)
{
    if (!self || !scanner)
        return;

    g_centerOnLocation = PluginMap::FunctionAs<CenterOnLocation_t>(scanner, self,
        DroneFields::Keys::MapCenterOnLocation, PLUGIN_MAP_PATTERN(kCenterOnLocationPattern),
        PLUGIN_SCAN_FUNCTION_START, PLUGIN_SCAN_FLAG_OPTIONAL);
}

void InitDroneMapFollow(IPluginSelf* self)
{
    if (!self || !self->hooks || !self->hooks->Input)
        return;

    DroneConfig::Config::ReadMapCentreKey(g_centreKeyName, sizeof(g_centreKeyName));
    self->hooks->Input->RegisterKeybindByName(g_centreKeyName, EModKeyEvent::Pressed, &OnCentreKey);

    LOG_INFO("DroneMapFollow: map centre toggle on '%s'", g_centreKeyName);
}

void ShutdownDroneMapFollow(IPluginSelf* self)
{
    if (self && self->hooks && self->hooks->Input && g_centreKeyName[0])
        self->hooks->Input->UnregisterKeybindByName(g_centreKeyName, EModKeyEvent::Pressed, &OnCentreKey);

    g_centrePress.store(false, std::memory_order_relaxed);

    // Game objects are touched only on the game thread, which is where shutdown
    // runs when it comes from RELOAD; anywhere else the map is left as it is.
    if (OnGameThread() && !g_followFailed)
    {
        if (!EndFollowGuarded())
        {
            g_followFailed = true;
            ForgetFollow();
            LOG_ERROR("DroneMapFollow: access violation while putting the map back at shutdown");
        }
    }
    else
        ForgetFollow();
}

void TickDroneMapFollow()
{
    if (g_followFailed || !g_centerOnLocation)
        return;

    // Fail closed: a fault while reading or moving the map stops following for
    // the rest of the session instead of repeating every tick.
    if (!RunFollowGuarded())
    {
        g_followFailed = true;
        ForgetFollow();
        LOG_ERROR("DroneMapFollow: access violation while moving the map; map follow switched off until RELOAD");
    }
}
