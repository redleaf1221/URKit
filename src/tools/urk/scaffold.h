#pragma once

#include <cctype>
#include <string>

namespace urk {

// C++ identifier, CMake target and directory name for a mod. Kept separate from
// main.cpp so the rules can be tested.
inline std::string SanitizeModName(const std::string &raw) {
    std::string name;
    for (char character : raw) {
        const auto value = static_cast<unsigned char>(character);
        if (std::isalnum(value))
            name.push_back(character);
        else if (character == '-' || character == '_' || character == ' ')
            name.push_back('_');
    }
    if (name.empty())
        name = "mod";
    if (std::isdigit(static_cast<unsigned char>(name.front())))
        name.insert(name.begin(), '_');
    return name;
}

// <name>_<8 hex>: stable enough to key deployed resources, and regenerated when
// a mod is scaffolded.
inline std::string MakeModId(const std::string &name, const std::string &uniqueSuffix) {
    return name + "_" + uniqueSuffix;
}

} // namespace urk
