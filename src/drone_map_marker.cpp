#include "drone_map_marker.h"
#include "drone_interact.h"
#include "drone_marker_icon.h"
#include "overlay_widget.h"
#include "plugin_helpers.h"
#include "drone_fields.h"
#include <plugin_interface.h>
#include <Chimera_classes.hpp>
#include <ChimeraUI_classes.hpp>
#include <Engine_classes.hpp>
#include <UMG_classes.hpp>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <windows.h>

namespace
{
    // The map's own terrain widget lays out 6000 widget units for 600000 world
    // units, so one widget unit is 100 world units. With the widget's size on
    // screen, that gives pixels per world unit at any zoom and DPI scale.
    constexpr double kWorldPerLocalUnit = 100.0;

    // The placement is checked against the game's own building markers. One that
    // sits further than this from where the transform puts it means the
    // transform is wrong, and nothing is drawn.
    constexpr double kTolerancePx = 2.0;

    // A tick checks up to this many markers, looking at no more than kMaxScan
    // map widgets to find them, and shows the drone if at least one agrees. A
    // merged cluster marker sits at its cluster's centre and would disagree on
    // its own.
    constexpr int kMaxReferences = 3;
    constexpr int kMaxScan       = 40;

    // Guard against reading a torn array header as a huge count.
    constexpr int32_t kMaxSaneCount = 100000;

    constexpr uint64_t kLogIntervalMs = 5000;

    // The player marker colour when the game's colour asset is not loaded: the
    // cyan of the player arrow on the map, as an ImGui colour (R 119, G 225, B 233).
    constexpr unsigned int kFallbackColour = 0xFFE9E177u;

    // What the render callback draws, as plain numbers.
    struct Snapshot
    {
        bool         valid;
        float        x, y;       // viewport pixels
        float        yawDeg;     // the drone's world yaw
        float        dpi;        // pixels per widget unit
        float        clip[4];    // the map canvas, viewport pixels
        unsigned int colour;
    };

    std::mutex g_lock;
    Snapshot   g_snapshot = {};

    OverlayWidget g_overlay;

    // Tick only. A marker that agreed last tick is tried first. Handle numbers,
    // not pointers: the widget is looked up again each tick.
    int32_t  g_refIndex  = -1;
    int32_t  g_refSerial = 0;
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
        ui->DL_PushClipRect(dl, s.clip[0], s.clip[1], s.clip[2], s.clip[3], true);
        DroneMarkerIcon::Draw(ui, dl, s.x, s.y, s.yawDeg, s.dpi, s.colour);
        ui->DL_PopClipRect(dl);
    }

    // ---- game thread: reads, each under SEH, into plain data ----------------------

    struct Geo
    {
        bool   visible;
        double topLeft[2];   // viewport pixels
        double absSize[2];
        double localSize[2];
    };

    bool SampleGeo(SDK::UWidget* w, SDK::UObject* worldContext, Geo* out)
    {
        __try
        {
            memset(out, 0, sizeof(*out));
            out->visible = w->IsVisible();

            const SDK::FGeometry geo       = w->GetCachedGeometry();
            const SDK::FVector2D localSize = SDK::USlateBlueprintLibrary::GetLocalSize(geo);
            const SDK::FVector2D absSize   = SDK::USlateBlueprintLibrary::GetAbsoluteSize(geo);
            out->localSize[0] = localSize.X;
            out->localSize[1] = localSize.Y;
            out->absSize[0]   = absSize.X;
            out->absSize[1]   = absSize.Y;

            SDK::FVector2D origin;
            origin.X = 0.0;
            origin.Y = 0.0;
            SDK::FVector2D pixel;
            SDK::FVector2D viewport;
            SDK::USlateBlueprintLibrary::LocalToViewport(worldContext, geo, origin, &pixel, &viewport);
            out->topLeft[0] = pixel.X;
            out->topLeft[1] = pixel.Y;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool LaidOut(const Geo& g)
    {
        return g.absSize[0] > 0.0 && g.absSize[1] > 0.0 && g.localSize[0] > 0.0 && g.localSize[1] > 0.0;
    }

    // World to viewport pixels.
    struct Transform
    {
        double originX, originY; // pixel of the world origin
        double scale;            // pixels per world unit
    };

    // The terrain widget's top-left corner is the dev settings' pivot point, and
    // its on-screen size per widget unit is the scale.
    Transform MakeTransform(const Geo& terrain, double pivotX, double pivotY)
    {
        Transform t;
        t.scale   = terrain.absSize[0] / terrain.localSize[0] / kWorldPerLocalUnit;
        t.originX = terrain.topLeft[0] - pivotX * t.scale;
        t.originY = terrain.topLeft[1] - pivotY * t.scale;
        return t;
    }

    // A building marker's widget sits with its top-left corner on the building's
    // world point. -1 when it is hidden or not laid out, else 1 when it is where
    // the transform says and 0 when it is not.
    int CompareReference(SDK::UCrUW_MapMenuMarker* marker, double worldX, double worldY, SDK::UObject* ctx,
                         const Transform& t)
    {
        Geo g;
        if (!marker || !SampleGeo(marker, ctx, &g) || !g.visible || !LaidOut(g))
            return -1;

        const double dx = t.originX + worldX * t.scale - g.topLeft[0];
        const double dy = t.originY + worldY * t.scale - g.topLeft[1];
        return std::fabs(dx) <= kTolerancePx && std::fabs(dy) <= kTolerancePx ? 1 : 0;
    }

    // The widget map is read at a layout computed from the running game; when it
    // cannot be this map's, the marker stays hidden. Said once.
    void LogWidgetMapOnce(const char* why)
    {
        if (g_loggedWidgetMap)
            return;
        g_loggedWidgetMap = true;
        LOG_WARN("DroneMapMarker: the map widgets cannot be read (%s); no marker will show", why);
    }

    // Looks at up to kMaxReferences building markers the game shows and counts
    // how many sit where the transform puts them.
    void CheckReferences(SDK::UCrUW_MapMenuMapArea* area, SDK::ACrMapMenuDataReplicationHelper* helper,
                         SDK::UObject* ctx, const Transform& t, int* checked, int* agreed)
    {
        *checked = 0;
        *agreed  = 0;
        __try
        {
            auto* widgetsAt   = DroneFields::MapMassMarkers.In(area);
            auto* containerAt = DroneFields::BuildingsMarkers.In(helper);
            auto* dataAt      = containerAt ? DroneFields::BuildingsMarkerData.In(containerAt) : nullptr;
            if (!widgetsAt || !dataAt)
                return;

            // The widget map is walked at the stride of the running game's
            // pair layout (key and value sizes), not the compiled one.
            const PluginMap::ScriptMapView widgets(widgetsAt,
                PluginMap::MapLayout(sizeof(SDK::FMassEntityHandle), alignof(SDK::FMassEntityHandle),
                    DroneFields::MarkerRepresentationSize.resolvedSize, alignof(SDK::FEntityMarkerUIRepresentation)));
            if (!widgets)
            {
                LogWidgetMapOnce(widgets.ReasonText());
                return;
            }

            auto& data = *dataAt;
            const int32_t dataNum = data.Num();
            if (widgets.Num() > kMaxSaneCount || dataNum < 0 || dataNum > kMaxSaneCount)
                return;

            int scanned = 0;
            bool done = false;
            for (int pass = 0; pass < 2 && !done; ++pass)
            {
                widgets.ForEach([&](uint8_t* keyAt, uint8_t* valueAt)
                {
                    const auto& handle = *reinterpret_cast<const SDK::FMassEntityHandle*>(keyAt);
                    const bool isLast = handle.Index == g_refIndex && handle.SerialNumber == g_refSerial;
                    if (isLast != (pass == 0))
                        return true;
                    if (pass == 1 && ++scanned > kMaxScan)
                        return false;

                    for (int32_t d = 0; d < dataNum; ++d)
                    {
                        const uint8_t* b = PluginMap::ElementAt(data, d, DroneFields::MarkerItemSize);
                        const auto* bHandle  = DroneFields::MarkerBuildingHandle.In(b);
                        const auto* location = DroneFields::MarkerLocation.In(b);
                        auto*       marker   = DroneFields::MarkerWidget.In(valueAt);
                        if (!bHandle || !location || !marker)
                            continue;
                        if (bHandle->Index != handle.Index || bHandle->SerialNumber != handle.SerialNumber)
                            continue;

                        const int r = CompareReference(*marker, location->X, location->Y, ctx, t);
                        if (r >= 0)
                        {
                            ++*checked;
                            if (r == 1)
                            {
                                ++*agreed;
                                g_refIndex  = handle.Index;
                                g_refSerial = handle.SerialNumber;
                            }
                            else if (isLast)
                                g_refIndex = -1;
                        }
                        break;
                    }

                    if (*checked >= kMaxReferences)
                    {
                        done = true;
                        return false;
                    }
                    return true;
                });
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            *checked = 0;
            *agreed  = 0;
        }
    }

    // Everything the placement needs from the drone.
    struct DroneState
    {
        bool   ok;
        double x, y, yawDeg;
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
            out->yawDeg = drone->K2_GetActorRotation().Yaw;
            out->ok = true;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    SDK::ACrMapMenuDataReplicationHelper* ReplicationHelper()
    {
        SDK::UWorld* world = PluginSdkRuntime::GetWorld();
        if (!world || !world->GameState || !world->GameState->IsA(SDK::ACrGameStateBase::StaticClass()))
            return nullptr;

        return DroneFields::ValueOr(DroneFields::MapDataHelper, static_cast<SDK::ACrGameStateBase*>(world->GameState));
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
                 "terrain transform puts it", checked, kTolerancePx);
    }

    void TickImpl()
    {
        if (!IsLocalPlayerInDrone())
        {
            Hide();
            return;
        }

        SDK::UCrUW_MapMenu* map = ActiveLocalMap();
        SDK::UCrUW_MapMenuMapArea* area = DroneFields::ValueOr(DroneFields::MapMenuMapArea, map);
        SDK::UCrMapMenuDevSettings* settings = SDK::UCrMapMenuDevSettings::GetDefaultObj();
        auto* terrainWidget = DroneFields::ValueOr(DroneFields::MapTerrain, area);
        auto* canvasWidget  = DroneFields::ValueOr(DroneFields::MapCanvasPanel, area);
        const auto* pivot   = DroneFields::MapPivotPoint.In(settings);
        if (!area || !terrainWidget || !canvasWidget || !pivot)
        {
            Hide();
            return;
        }

        SDK::ACrPlayerControllerBase* pc = LocalPlayerController();
        DroneState drone;
        Geo terrain, canvas;
        if (!ReadDrone(pc, &drone) || !SampleGeo(terrainWidget, pc, &terrain) ||
            !SampleGeo(canvasWidget, pc, &canvas) || !LaidOut(terrain) || !LaidOut(canvas))
        {
            Hide();
            return;
        }

        const Transform t = MakeTransform(terrain, pivot->X, pivot->Y);

        // Hidden when markers the game placed are not where the transform says;
        // with none to compare against, the transform stands.
        SDK::ACrMapMenuDataReplicationHelper* helper = ReplicationHelper();
        int checked = 0;
        int agreed  = 0;
        if (helper)
            CheckReferences(area, helper, pc, t, &checked, &agreed);
        if (checked > 0 && agreed == 0)
        {
            LogDisagreement(checked);
            Hide();
            return;
        }

        Snapshot s = {};
        s.valid     = true;
        s.x         = static_cast<float>(t.originX + drone.x * t.scale);
        s.y         = static_cast<float>(t.originY + drone.y * t.scale);
        s.yawDeg    = static_cast<float>(drone.yawDeg);
        s.dpi       = static_cast<float>(canvas.absSize[0] / canvas.localSize[0]);
        s.clip[0]   = static_cast<float>(canvas.topLeft[0]);
        s.clip[1]   = static_cast<float>(canvas.topLeft[1]);
        s.clip[2]   = static_cast<float>(canvas.topLeft[0] + canvas.absSize[0]);
        s.clip[3]   = static_cast<float>(canvas.topLeft[1] + canvas.absSize[1]);

        s.colour    = PlayerMarkerColour();
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
    // May run on the render thread: no game object is touched here.
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
