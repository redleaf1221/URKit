#pragma once
#include <string>
#include <vector>

struct Config {
    bool showConsole = false;
    bool safeMode = false;
    int initDelayMs = 0;
    std::string runtime = "auto";
    std::string modsDir = "Mods";
    // [Loader] Splash: show the progress window while the loader starts up.
    bool splash = true;
    // [Logging] Unity: forward UnityEngine.Debug output into the URKit log.
    bool unityLogs = true;
    std::string configPath;
    std::vector<std::string> modPaths;
    std::vector<std::string> warnings;
};

Config Config_Load();
Config Config_Load(const std::string &iniPath);

// Process-wide configuration loaded by the loader at startup; safe to call
// before loading finishes (returns the defaults until then).
const Config &Config_Current();
