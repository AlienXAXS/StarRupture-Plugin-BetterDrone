#pragma once
#include <plugin_interface.h>
#include <cstdio>

namespace DroneConfig
{
    class Config
    {
    public:
        static void SetSelf(IPluginSelf* self) { s_self = self; }

        // Called once the CDO is available so defaultValue strings reflect real game values.
        static void InitializeWithCDODefaults(float speedPerSec, float maxRadius, float maxHeight)
        {
            snprintf(s_defSpeed,  sizeof(s_defSpeed),  "%.2f", speedPerSec);
            snprintf(s_defRadius, sizeof(s_defRadius), "%.2f", maxRadius);
            snprintf(s_defHeight, sizeof(s_defHeight), "%.2f", maxHeight);

            s_entries[0] = { "Drone", "SpeedPerSec", ConfigValueType::Float, s_defSpeed,  "Movement speed (cm/s)",         0.0f, 4000.0f    };
            s_entries[1] = { "Drone", "MaxRadius",   ConfigValueType::Float, s_defRadius, "Maximum horizontal range (cm)", 0.0f, 1000000.0f };
            s_entries[2] = { "Drone", "MaxHeight",   ConfigValueType::Float, s_defHeight, "Maximum vertical range (cm)",   0.0f, 100000.0f    };

            s_schema = { s_entries, 3 };

            if (s_self)
                s_self->config->InitializeFromSchema(s_self, &s_schema);
        }

        static float ReadSpeedPerSec() { return s_self ? s_self->config->ReadFloat(s_self, "Drone", "SpeedPerSec", 0.0f) : 0.0f; }
        static float ReadMaxRadius()   { return s_self ? s_self->config->ReadFloat(s_self, "Drone", "MaxRadius",   0.0f) : 0.0f; }
        static float ReadMaxHeight()   { return s_self ? s_self->config->ReadFloat(s_self, "Drone", "MaxHeight",   0.0f) : 0.0f; }

    private:
        static IPluginSelf* s_self;
        static char s_defSpeed[32];
        static char s_defRadius[32];
        static char s_defHeight[32];
        static ConfigEntry s_entries[3];
        static ConfigSchema s_schema;
    };
}
