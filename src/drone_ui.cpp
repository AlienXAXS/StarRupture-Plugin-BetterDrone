#include "drone_ui.h"
#include "drone_settings.h"
#include <cstdio>

static IPluginSelf* s_self = nullptr;

static const char* k_section = "DroneSettings";

void InitDroneUI(IPluginSelf* self)
{
    s_self = self;
}

static void LoadConfig()
{
    if (!g_drone.valid || !s_self) return;
    auto* cfg = s_self->config;

    *g_drone.sphereRadius  = cfg->ReadFloat(s_self, k_section, "SphereRadius",   *g_drone.sphereRadius);
    *g_drone.speedPerSec   = cfg->ReadFloat(s_self, k_section, "SpeedPerSec",    *g_drone.speedPerSec);
    *g_drone.maxRadius     = cfg->ReadFloat(s_self, k_section, "MaxRadius",      *g_drone.maxRadius);
    *g_drone.maxHeight     = cfg->ReadFloat(s_self, k_section, "MaxHeight",      *g_drone.maxHeight);
    *g_drone.warningRadius = *g_drone.maxRadius * 0.95f;
    *g_drone.warningHeight = *g_drone.maxHeight * 0.95f;
}

static void SaveConfig()
{
    if (!g_drone.valid || !s_self) return;
    auto* cfg = s_self->config;

    cfg->WriteFloat(s_self, k_section, "SphereRadius",  *g_drone.sphereRadius);
    cfg->WriteFloat(s_self, k_section, "SpeedPerSec",   *g_drone.speedPerSec);
    cfg->WriteFloat(s_self, k_section, "MaxRadius",     *g_drone.maxRadius);
    cfg->WriteFloat(s_self, k_section, "MaxHeight",     *g_drone.maxHeight);
}

void RenderDronePanel(IModLoaderImGui* ui)
{
    if (!g_drone.valid)
    {
        ui->TextColored(1.f, 0.4f, 0.4f, 1.f, "Settings CDO not found — engine may still be initialising.");
        return;
    }

    ui->TextWrapped("Changes apply immediately. Values are in Unreal units (cm).");
    ui->Separator();
    ui->Spacing();

    ui->SeparatorText("Movement");
    ui->SetNextItemWidth(220.f);
    ui->InputFloat("Speed (cm/s)##speed",     g_drone.speedPerSec,   100.f, 1000.f, "%.0f");

    ui->Spacing();
    ui->SeparatorText("Horizontal Range");
    ui->SetNextItemWidth(220.f);
    ui->InputFloat("Max Radius (cm)##maxr",   g_drone.maxRadius,     100.f, 1000.f, "%.0f");
    *g_drone.warningRadius = *g_drone.maxRadius * 0.95f;

    ui->Spacing();
    ui->SeparatorText("Vertical Range");
    ui->SetNextItemWidth(220.f);
    ui->InputFloat("Max Height (cm)##maxh",   g_drone.maxHeight,     100.f, 1000.f, "%.0f");
    *g_drone.warningHeight = *g_drone.maxHeight * 0.95f;

    ui->Spacing();
    ui->SeparatorText("Collision");
    ui->SetNextItemWidth(220.f);
    ui->InputFloat("Sphere Radius (cm)##sr",  g_drone.sphereRadius,  10.f,  100.f,  "%.1f");

    ui->Spacing();
    ui->Separator();
    ui->Spacing();

    if (ui->Button("Save to Config"))
        SaveConfig();

    ui->SameLine(0.f, 8.f);

    if (ui->Button("Load from Config"))
        LoadConfig();
}
