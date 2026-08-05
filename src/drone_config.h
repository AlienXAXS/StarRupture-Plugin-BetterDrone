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
        static void InitializeWithCDODefaults(float speedPerSec, float maxRadius, float maxHeight, float maxRailLength)
        {
            snprintf(s_defSpeed,      sizeof(s_defSpeed),      "%.2f", speedPerSec);
            snprintf(s_defRadius,     sizeof(s_defRadius),     "%.2f", maxRadius);
            snprintf(s_defHeight,     sizeof(s_defHeight),     "%.2f", maxHeight);
            snprintf(s_defRailLength, sizeof(s_defRailLength), "%.2f", maxRailLength);

            s_entries[0] = { "Drone", "SpeedPerSec",      ConfigValueType::Float, s_defSpeed,      "Movement speed (cm/s)",                         0.0f, 4000.0f    };
            s_entries[1] = { "Drone", "MaxRadius",        ConfigValueType::Float, s_defRadius,     "Maximum horizontal range (cm)",                 0.0f, 1000000.0f };
            s_entries[2] = { "Drone", "MaxHeight",        ConfigValueType::Float, s_defHeight,     "Maximum vertical range (cm)",                   0.0f, 100000.0f  };
            s_entries[3] = { "Drone", "MaxRailLength",    ConfigValueType::Float, s_defRailLength, "Maximum rail length (cm)",                      0.0f, 100000.0f  };
            s_entries[4] = { "Drone", "Always Allow Drone", ConfigValueType::Boolean,  "false",         "Allow the building drone to be out in places it should not be, including during environmental wave events.",  0.0f, 1.0f       };

            s_entries[5] = { "Interaction", "Interact In Drone Mode", ConfigValueType::Boolean, "true", "Let the drone open building UIs, the same as walking up to them on foot.", 0.0f, 1.0f };
            s_entries[6] = { "Interaction", "Interact Key",           ConfigValueType::Keybind, "E",    "Key that interacts while the drone is out. Set this to match the game's own interact key.", 0.0f, 0.0f };

            s_schema = { s_entries, 7 };

            if (s_self)
                s_self->config->InitializeFromSchema(s_self, &s_schema);
        }

        static float ReadSpeedPerSec()     { return s_self ? s_self->config->ReadFloat(s_self, "Drone", "SpeedPerSec",      0.0f)  : 0.0f;  }
        static float ReadMaxRadius()       { return s_self ? s_self->config->ReadFloat(s_self, "Drone", "MaxRadius",        0.0f)  : 0.0f;  }
        static float ReadMaxHeight()       { return s_self ? s_self->config->ReadFloat(s_self, "Drone", "MaxHeight",        0.0f)  : 0.0f;  }
        static float ReadMaxRailLength()   { return s_self ? s_self->config->ReadFloat(s_self, "Drone", "MaxRailLength",    0.0f)  : 0.0f;  }
        static bool  ReadAlwaysAllowDrone(){ return s_self ? s_self->config->ReadBool (s_self, "Drone", "Always Allow Drone", false) : false; }

        static bool ReadInteractInDroneMode()
        {
            return s_self ? s_self->config->ReadBool(s_self, "Interaction", "Interact In Drone Mode", true) : false;
        }

        // Copies the configured interact key name into outBuffer, falling back to "E".
        static void ReadInteractKey(char* outBuffer, int bufferSize)
        {
            if (!outBuffer || bufferSize <= 0)
                return;

            outBuffer[0] = '\0';

            if (!s_self ||
                !s_self->config->ReadString(s_self, "Interaction", "Interact Key", outBuffer, bufferSize, "E") ||
                outBuffer[0] == '\0')
            {
                snprintf(outBuffer, static_cast<size_t>(bufferSize), "E");
            }
        }

    private:
        static IPluginSelf* s_self;
        static char s_defSpeed[32];
        static char s_defRadius[32];
        static char s_defHeight[32];
        static char s_defRailLength[32];
        static ConfigEntry s_entries[7];
        static ConfigSchema s_schema;
    };
}
