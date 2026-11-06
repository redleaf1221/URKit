#pragma once
#include <string>
#include <vector>

struct Config {
    bool showConsole = false;
    bool safeMode = false;
    int initDelayMs = 0;
    std::string runtime = "auto";
    std::string modsDir = "Mods";
    // [Logging] Unity: forward UnityEngine.Debug output into the URKit log.
    bool unityLogs = true;
    // [Unreal] DumpTypes: write reflected classes for the typed headers urk.exe generates.
    bool unrealDumpTypes = false;
    // Hooks also catch native functions Blueprint calls directly; off until proven in games.
    bool unrealNativeFromBlueprint = false;
    std::string configPath;
    std::vector<std::string> modPaths;
    std::vector<std::string> warnings;
};

Config Config_Load();
Config Config_Load(const std::string &iniPath);

// Process-wide configuration loaded by the loader at startup; safe to call
// before loading finishes (returns the defaults until then).
const Config &Config_Current();
