#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "config/mod_config.h"
#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "sdk/runtime_api.h"

namespace ModUI::Localization {
using Dictionary = std::unordered_map<std::string, std::string>;

struct Replacement {
    const char *name;
    std::string value;
};

// Table for ModRenderHook::g_options.default_language, loaded from that language JSON file.
// Every language -- including the default -- comes from a locale file, and
// translate() resolves current language -> default language -> key.
inline Dictionary &fallback_table() {
    static Dictionary values;
    return values;
}
inline std::string &fallback_language() {
    static std::string value;
    return value;
}

// Hardcoded labels for projects generated without localization support.
// enableLocalization is a generation-time choice: when it is off there are no
// locale files at all, so this table is the only source of UI text.
inline const Dictionary &compiled_english() {
    static const Dictionary values = {
        {"about.title", "About"},
        {"about.default_description", "Small runtime menu for this mod."},
        {"about.author", "Author"},
        {"about.version", "Version"},
        {"about.url", "URL"},
        {"about.project_url", "Project URL"},
        {"about.social", "Social"},
        {"about.github", "GitHub"},
        {"menu.about", "About"},
        {"menu.config", "Config"},
        {"config.controls", "Controls"},
        {"config.show_menu", "Show menu"},
        {"config.toggle_key", "Toggle key"},
        {"config.localization", "Localization"},
        {"config.detached_viewports", "Detached windows"},
        {"config.enable_localization", "Enable localization"},
        {"config.language", "Language"},
        {"widget.enabled", "Enabled"},
        {"widget.disabled", "Disabled"},
        {"example.key_code", "Key code: {code}"},
        {"menu.hint", "{name}: press {key} for the menu"},
    };
    return values;
}

inline Dictionary &overrides() {
    static Dictionary values;
    return values;
}
inline std::vector<std::string> &languages() {
    static std::vector<std::string> values{"en"};
    return values;
}
inline std::string &current_language() {
    static std::string value = "en";
    return value;
}
inline std::string &last_error() {
    static std::string value;
    return value;
}
inline bool &initialized() {
    static bool value = false;
    return value;
}

inline void log_warning(const std::string &message) {
    const URK::ModContext *ctx = URK::context();
    if (ctx && ctx->Log)
        ctx->Log("[%s][localization] %s", ModRenderHook::g_options.display_name, message.c_str());
}

inline void report_error(const std::string &message) {
    last_error() = message;
    log_warning(message);
}

inline void skip_whitespace(const std::string &source, size_t &pos) {
    while (pos < source.size() && std::isspace(static_cast<unsigned char>(source[pos])))
        ++pos;
}

inline void skip_utf8_bom(const std::string &source, size_t &pos) {
    if (pos == 0 && source.size() >= 3 && static_cast<unsigned char>(source[0]) == 0xEF &&
        static_cast<unsigned char>(source[1]) == 0xBB && static_cast<unsigned char>(source[2]) == 0xBF) {
        pos = 3;
    }
}

inline bool append_unicode(std::string &output, unsigned value) {
    if (value <= 0x7f)
        output.push_back(static_cast<char>(value));
    else if (value <= 0x7ff) {
        output.push_back(static_cast<char>(0xc0 | (value >> 6)));
        output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    } else {
        output.push_back(static_cast<char>(0xe0 | (value >> 12)));
        output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    }
    return true;
}

inline bool parse_string(const std::string &source, size_t &pos, std::string &output, std::string &error) {
    if (pos >= source.size() || source[pos++] != '"') {
        error = "expected JSON string";
        return false;
    }
    output.clear();
    while (pos < source.size()) {
        const char ch = source[pos++];
        if (ch == '"')
            return true;
        if (static_cast<unsigned char>(ch) < 0x20) {
            error = "control character in JSON string";
            return false;
        }
        if (ch != '\\') {
            output.push_back(ch);
            continue;
        }
        if (pos >= source.size()) {
            error = "unterminated JSON escape";
            return false;
        }
        switch (source[pos++]) {
            case '"':
                output.push_back('"');
                break;
            case '\\':
                output.push_back('\\');
                break;
            case '/':
                output.push_back('/');
                break;
            case 'b':
                output.push_back('\b');
                break;
            case 'f':
                output.push_back('\f');
                break;
            case 'n':
                output.push_back('\n');
                break;
            case 'r':
                output.push_back('\r');
                break;
            case 't':
                output.push_back('\t');
                break;
            case 'u': {
                if (pos + 4 > source.size()) {
                    error = "truncated Unicode escape";
                    return false;
                }
                unsigned value = 0;
                for (int i = 0; i < 4; ++i) {
                    const char hex = source[pos++];
                    value <<= 4;
                    if (hex >= '0' && hex <= '9')
                        value |= static_cast<unsigned>(hex - '0');
                    else if (hex >= 'a' && hex <= 'f')
                        value |= static_cast<unsigned>(hex - 'a' + 10);
                    else if (hex >= 'A' && hex <= 'F')
                        value |= static_cast<unsigned>(hex - 'A' + 10);
                    else {
                        error = "invalid Unicode escape";
                        return false;
                    }
                }
                if (value >= 0xd800 && value <= 0xdfff) {
                    error = "Unicode surrogate escapes are unsupported";
                    return false;
                }
                append_unicode(output, value);
                break;
            }
            default:
                error = "invalid JSON escape";
                return false;
        }
    }
    error = "unterminated JSON string";
    return false;
}

inline bool parse_dictionary(const std::string &source, Dictionary &output, std::string &error) {
    size_t pos = 0;
    skip_utf8_bom(source, pos);
    skip_whitespace(source, pos);
    if (pos >= source.size() || source[pos++] != '{') {
        error = "root must be an object";
        return false;
    }
    output.clear();
    skip_whitespace(source, pos);
    while (pos < source.size() && source[pos] != '}') {
        std::string key;
        std::string value;
        if (!parse_string(source, pos, key, error))
            return false;
        skip_whitespace(source, pos);
        if (pos >= source.size() || source[pos++] != ':') {
            error = "expected ':' after JSON key";
            return false;
        }
        skip_whitespace(source, pos);
        if (!parse_string(source, pos, value, error)) {
            error.insert(0, "all localization values must be strings: ");
            return false;
        }
        output[std::move(key)] = std::move(value);
        skip_whitespace(source, pos);
        if (pos < source.size() && source[pos] == ',') {
            ++pos;
            skip_whitespace(source, pos);
            continue;
        }
        if (pos >= source.size() || source[pos] != '}') {
            error = "expected ',' or '}' in JSON object";
            return false;
        }
    }
    if (pos >= source.size() || source[pos++] != '}') {
        error = "unterminated JSON object";
        return false;
    }
    skip_whitespace(source, pos);
    if (pos != source.size()) {
        error = "unexpected content after JSON object";
        return false;
    }
    return true;
}

inline std::filesystem::path locale_directory() {
    HMODULE module = nullptr;
    constexpr DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
    if (!GetModuleHandleExW(flags, reinterpret_cast<LPCWSTR>(&locale_directory), &module)) {
        report_error("cannot resolve the mod DLL path");
        return {};
    }
    std::vector<wchar_t> path(MAX_PATH);
    const DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length == path.size()) {
        report_error("cannot read the mod DLL path");
        return {};
    }
    return std::filesystem::path(path.data(), path.data() + length).parent_path() / "locales" / ModRenderHook::g_options.mod_id;
}

inline bool read_language(const std::string &language, Dictionary &output, std::string &error) {
    const std::filesystem::path path = locale_directory() / (std::string(language) + ".json");
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "cannot open locale file: " + path.string();
        return false;
    }
    std::ostringstream content;
    content << input.rdbuf();
    if (!parse_dictionary(content.str(), output, error)) {
        error = "cannot parse locale file " + path.string() + ": " + error;
        return false;
    }
    return true;
}

inline bool load_language(const char *language) {
    if (!language || !language[0])
        return false;
    if (std::string(language) == fallback_language()) {
        overrides().clear(); // the default table already holds these strings
        current_language() = fallback_language();
        last_error().clear();
        return true;
    }
    Dictionary parsed;
    std::string error;
    if (!read_language(language, parsed, error)) {
        report_error(error);
        return false;
    }
    overrides() = std::move(parsed);
    current_language() = language;
    last_error().clear();
    return true;
}

inline void initialize() {
    if (initialized())
        return;
    initialized() = true;
    languages().clear();
    last_error().clear();

    if (!ModRenderHook::g_options.localization) {
        // No locale files were generated: every label is hardcoded English.
        fallback_language() = "en";
        fallback_table() = compiled_english();
        current_language() = "en";
        languages().emplace_back("en");
        return;
    }

    const char *configured = ModRenderHook::g_options.default_language && ModRenderHook::g_options.default_language[0]
                                 ? ModRenderHook::g_options.default_language
                                 : "en";
    fallback_language() = configured;

    const std::filesystem::path directory = locale_directory();
    if (directory.empty()) {
        languages().push_back(fallback_language());
        return;
    }

    std::error_code directory_error;
    if (!std::filesystem::is_directory(directory, directory_error)) {
        report_error("locale directory is unavailable: " + directory.string());
        languages().push_back(fallback_language());
        return;
    }
    const std::filesystem::directory_iterator entries(directory, directory_error);
    if (directory_error) {
        report_error("cannot enumerate locale directory: " + directory_error.message());
        languages().push_back(fallback_language());
        return;
    }
    for (const auto &entry : entries) {
        std::error_code entry_error;
        if (!entry.is_regular_file(entry_error)) {
            if (entry_error)
                report_error("cannot inspect locale entry: " + entry_error.message());
            continue;
        }
        if (entry.path().extension() != ".json")
            continue;
        const std::string language = entry.path().stem().string();
        if (language.empty())
            continue;
        Dictionary parsed;
        std::string load_error;
        if (!read_language(language, parsed, load_error)) {
            report_error(load_error);
            continue;
        }
        if (language == fallback_language())
            fallback_table() = std::move(parsed);
        languages().push_back(language);
    }
    std::ranges::sort(languages());
    languages().erase(std::ranges::unique(languages()).begin(), languages().end());
    if (languages().empty())
        languages().push_back(fallback_language());

    if (fallback_table().empty())
        report_error("default locale is missing: " + fallback_language() + ".json; labels fall back to keys");

    overrides().clear();
    current_language() = fallback_language();
}

inline const std::vector<std::string> &available_languages() {
    initialize();
    return languages();
}
inline const char *active_language() {
    initialize();
    return current_language().c_str();
}
inline const char *last_error_message() {
    initialize();
    return last_error().c_str();
}
inline bool set_language(const char *language) {
    initialize();
    return ModRenderHook::g_options.localization && load_language(language);
}

inline const char *translate(const char *key) {
    initialize();
    if (!key)
        return "";
    const auto override_it = overrides().find(key);
    if (override_it != overrides().end())
        return override_it->second.c_str();
    const auto english_it = fallback_table().find(key);
    return english_it != fallback_table().end() ? english_it->second.c_str() : key;
}

inline std::string format(const char *key, std::initializer_list<Replacement> replacements) {
    std::string result = translate(key);
    for (const Replacement &replacement : replacements) {
        if (!replacement.name || !replacement.name[0])
            continue;
        const std::string token = std::string("{") + replacement.name + "}";
        size_t pos = 0;
        while ((pos = result.find(token, pos)) != std::string::npos) {
            result.replace(pos, token.size(), replacement.value);
            pos += replacement.value.size();
        }
    }
    return result;
}
} // namespace ModUI::Localization

