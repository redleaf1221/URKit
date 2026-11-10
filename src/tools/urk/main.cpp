// urk: the small command line the repository needs.
//
//   urk new <name> [--no-imgui] [--mods-dir <dir>]
//   urk deploy <name> --game <dir> [--build <dir>]
//   urk dev list | status | tests | test <test-name> [--pid <pid>] [--json]
//
// Everything a mod needs is ordinary source under mods/<name>; this tool only
// scaffolds that directory, copies the built DLL and talks to URKitDevBridge.
#include <windows.h>

#include "scaffold.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Json = nlohmann::json;

namespace {

int Fail(const std::string &message) {
    std::fprintf(stderr, "urk: %s\n", message.c_str());
    return 1;
}

std::string RandomHex8() {
    std::random_device device;
    std::mt19937 generator(device());
    std::uniform_int_distribution<int> digit(0, 15);
    std::string text;
    for (int i = 0; i < 8; ++i)
        text.push_back("0123456789abcdef"[digit(generator)]);
    return text;
}

bool ReplaceAllInFile(const fs::path &path, const std::string &from, const std::string &to, std::string *error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        *error = "cannot read " + path.string();
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    bool changed = false;
    for (std::size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) {
        text.replace(at, from.size(), to);
        changed = true;
    }
    if (!changed)
        return true;
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        *error = "cannot write " + path.string();
        return false;
    }
    output << text;
    return static_cast<bool>(output);
}

int CommandNew(const std::vector<std::string> &args) {
    std::string rawName;
    std::string modsDirectory = "src/mods";
    bool imgui = true;
    bool vendorOverlay = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--no-imgui")
            imgui = false;
        else if (args[i] == "--vendor-overlay")
            vendorOverlay = true;
        else if (args[i] == "--mods-dir" && i + 1 < args.size())
            modsDirectory = args[++i];
        else if (!args[i].empty() && args[i][0] != '-')
            rawName = args[i];
    }
    if (rawName.empty())
        return Fail("usage: urk new <name> [--no-imgui] [--vendor-overlay] [--mods-dir <dir>]");

    const std::string name = urk::SanitizeModName(rawName);
    const fs::path templateDirectory = fs::path(modsDirectory) / "_template";
    const fs::path targetDirectory = fs::path(modsDirectory) / name;
    if (!fs::is_directory(templateDirectory))
        return Fail("template not found: " + templateDirectory.string() + " (run urk from the repository root)");
    if (fs::exists(targetDirectory))
        return Fail("mod already exists: " + targetDirectory.string());

    std::error_code ec;
    fs::copy(templateDirectory, targetDirectory, fs::copy_options::recursive, ec);
    if (ec)
        return Fail("cannot copy template: " + ec.message());

    // The scaffold carries the template's placeholder values; rewrite them once.
    const std::string modId = urk::MakeModId(name, RandomHex8());
    std::string error;
    for (const fs::directory_entry &entry : fs::recursive_directory_iterator(targetDirectory)) {
        if (!entry.is_regular_file())
            continue;
        const std::string extension = entry.path().extension().string();
        if (extension != ".h" && extension != ".cpp" && extension != ".md" && extension != ".txt")
            continue;
        if (!ReplaceAllInFile(entry.path(), "ExampleMod_5d8628ce", modId, &error) ||
            !ReplaceAllInFile(entry.path(), "ExampleMod", name, &error))
            return Fail(error);
    }

    if (vendorOverlay) {
        // Copy the shared overlay sources so this mod can change them freely.
        std::error_code copyError;
        const fs::path overlaySource = fs::path(modsDirectory).parent_path() / "overlay";
        if (!fs::is_directory(overlaySource))
            return Fail("overlay sources not found: " + overlaySource.string());
        fs::copy(overlaySource, targetDirectory / "overlay", fs::copy_options::recursive, copyError);
        if (copyError)
            return Fail("cannot copy overlay sources: " + copyError.message());
    }

    {
        std::ofstream switches(targetDirectory / "mod_target.cmake", std::ios::binary | std::ios::trunc);
        if (!switches)
            return Fail("cannot write mod_target.cmake");
        switches << "# Per-mod build switches. `urk new` writes these; the repository defaults\n"
                    "# stay in the URK_WITH_IMGUI cache option.\n";
        switches << "set(URK_MOD_WITH_IMGUI " << (imgui ? "ON" : "OFF") << ")\n";
        if (vendorOverlay)
            switches << "set(URK_MOD_VENDORED_OVERLAY ON)\n";
    }

    std::printf("created %s\n", targetDirectory.string().c_str());
    std::printf("  mod_id:  %s\n", modId.c_str());
    std::printf("  overlay: %s\n", imgui ? (vendorOverlay ? "ImGui (vendored copy)" : "ImGui (shared)") : "none");
    if (!imgui)
        std::printf("  note:    src/mods/%s/mod_target.cmake sets URK_MOD_WITH_IMGUI OFF\n", name.c_str());
    std::printf("next:\n  cmake --build <build-dir> --target %s\n  urk deploy %s --game <game-dir>\n", name.c_str(),
                name.c_str());
    return 0;
}

int CommandDeploy(const std::vector<std::string> &args) {
    std::string name;
    fs::path gameDirectory;
    fs::path buildDirectory;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--game" && i + 1 < args.size())
            gameDirectory = args[++i];
        else if (args[i] == "--build" && i + 1 < args.size())
            buildDirectory = args[++i];
        else if (!args[i].empty() && args[i][0] != '-')
            name = args[i];
    }
    if (name.empty() || gameDirectory.empty())
        return Fail("usage: urk deploy <name> --game <dir> [--build <dir>]");

    if (buildDirectory.empty()) {
        for (const char *candidate : {"cmake-build-relwithdebinfo", "cmake-build-release", "build", "out"}) {
            if (fs::is_directory(candidate)) {
                buildDirectory = candidate;
                break;
            }
        }
    }
    if (buildDirectory.empty())
        return Fail("no build directory found; pass --build <dir>");

    std::error_code ec;
    fs::path built = buildDirectory / "src" / "mods" / (name + ".dll");
    if (!fs::is_regular_file(built)) {
        for (const fs::directory_entry &entry : fs::recursive_directory_iterator(buildDirectory, ec)) {
            if (entry.is_regular_file() && entry.path().filename() == name + ".dll") {
                built = entry.path();
                break;
            }
        }
    }
    if (!fs::is_regular_file(built))
        return Fail("built DLL not found under " + buildDirectory.string() + "; build the " + name + " target first");

    const fs::path modsDirectory = gameDirectory / "Mods";
    fs::create_directories(modsDirectory, ec);
    if (ec)
        return Fail("cannot create " + modsDirectory.string() + ": " + ec.message());
    const fs::path destination = modsDirectory / (name + ".dll");
    fs::copy_file(built, destination, fs::copy_options::overwrite_existing, ec);
    if (ec)
        return Fail("cannot copy to " + destination.string() + ": " + ec.message());
    std::printf("deployed %s\n  from %s\n  to   %s\n", (name + ".dll").c_str(), built.string().c_str(),
                destination.string().c_str());
    return 0;
}

fs::path DiscoveryDirectory() {
    wchar_t buffer[32768]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, 32768);
    if (length == 0 || length >= 32768)
        return {};
    return fs::path(buffer) / L"URK" / L"DevBridge" / L"bridges";
}

struct BridgeTarget {
    std::uint32_t pid = 0;
    std::wstring pipe;
};

std::vector<BridgeTarget> FindBridges() {
    std::vector<BridgeTarget> targets;
    std::error_code ec;
    const fs::path directory = DiscoveryDirectory();
    if (directory.empty() || !fs::is_directory(directory, ec))
        return targets;
    for (const fs::directory_entry &entry : fs::directory_iterator(directory, ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
            continue;
        std::ifstream input(entry.path(), std::ios::binary);
        if (!input)
            continue;
        try {
            const Json record = Json::parse(input);
            BridgeTarget target;
            target.pid = record.value("pid", 0u);
            const std::string pipe = record.value("pipe", std::string{});
            target.pipe.assign(pipe.begin(), pipe.end());
            if (target.pid != 0 && !target.pipe.empty())
                targets.push_back(std::move(target));
        } catch (const Json::exception &) {
            // A half-written or stale record is not an error; skip it.
        }
    }
    std::ranges::sort(targets, [](const BridgeTarget &a, const BridgeTarget &b) { return a.pid > b.pid; });
    return targets;
}

bool Exchange(const BridgeTarget &target, const std::string &tool, const Json &arguments, Json *result,
              std::string *error) {
    HANDLE pipe = CreateFileW(target.pipe.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        *error = "cannot connect to the DevBridge pipe (Windows " + std::to_string(GetLastError()) + ")";
        return false;
    }
    const Json request{{"version", "1"}, {"id", "1"}, {"tool", tool}, {"arguments", arguments}};
    std::string line = request.dump();
    line.push_back('\n');
    DWORD written = 0;
    if (!WriteFile(pipe, line.data(), static_cast<DWORD>(line.size()), &written, nullptr) || written == 0) {
        CloseHandle(pipe);
        *error = "cannot write to the DevBridge pipe";
        return false;
    }

    std::string response;
    char value = 0;
    while (true) {
        DWORD read = 0;
        if (!ReadFile(pipe, &value, 1, &read, nullptr) || read == 0)
            break;
        if (value == '\n')
            break;
        if (value != '\r')
            response.push_back(value);
        if (response.size() > 1024 * 1024) {
            CloseHandle(pipe);
            *error = "DevBridge response is too large";
            return false;
        }
    }
    CloseHandle(pipe);
    if (response.empty()) {
        *error = "DevBridge closed the pipe without a response";
        return false;
    }
    try {
        const Json parsed = Json::parse(response);
        if (!parsed.value("ok", false)) {
            *error = parsed.value("error_code", std::string("bridge_error")) + ": " +
                     parsed.value("error_message", std::string("request failed"));
            return false;
        }
        *result = parsed.value("result", Json::object());
        return true;
    } catch (const Json::exception &exception) {
        *error = std::string("malformed DevBridge response: ") + exception.what();
        return false;
    }
}

// Human-readable rendering of a bridge result. --json prints the raw object.
void PrintResult(const std::string &tool, const Json &result) {
    if (tool == "runtime_status") {
        std::printf("pid      %u\n", result.value("process_id", 0u));
        std::printf("backend  %s\n", result.value("runtime_backend", std::string("unknown")).c_str());
        std::printf("sdk      %d\n", result.value("sdk_version", 0));
        std::printf("caps     0x%llX\n", static_cast<unsigned long long>(result.value("runtime_capabilities", 0ull)));
        if (result.value("scene_available", false)) {
            const Json &scene = result["scene"];
            std::printf("scene    %s (build %d, handle %d)\n", scene.value("name", std::string{}).c_str(),
                        scene.value("build_index", -1), scene.value("handle", 0));
        } else {
            std::printf("scene    <none>\n");
        }
        return;
    }
    if (tool == "list_runtime_tests") {
        const Json tests = result.value("tests", Json::array());
        if (tests.empty())
            std::printf("no runtime tests exported by the loaded mods\n");
        for (const Json &test : tests) {
            std::printf("%-40s %s:%d\n", test.value("qualified_name", std::string{}).c_str(),
                        test.value("source_file", std::string{}).c_str(), test.value("source_line", 0));
        }
        for (const Json &failure : result.value("errors", Json::array()))
            std::printf("error    %s\n", failure.dump().c_str());
        return;
    }
    if (tool == "run_runtime_test") {
        const bool passed = result.value("passed", false);
        const double milliseconds = static_cast<double>(result.value("duration_microseconds", 0ull)) / 1000.0;
        std::printf("%s %s (%.2f ms)\n", passed ? "PASS" : "FAIL",
                    result.value("qualified_name", std::string{}).c_str(), milliseconds);
        const std::string message = result.value("message", std::string{});
        if (!message.empty())
            std::printf("  %s\n", message.c_str());
        const std::string details = result.value("details", std::string{});
        if (!details.empty())
            std::printf("  %s\n", details.c_str());
        return;
    }
    std::cout << result.dump(2) << "\n";
}

std::wstring ProcessImagePath(std::uint32_t pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return {};
    wchar_t buffer[32768]{};
    DWORD length = static_cast<DWORD>(std::size(buffer));
    const bool ok = QueryFullProcessImageNameW(process, 0, buffer, &length) != 0;
    CloseHandle(process);
    return ok ? std::wstring(buffer, length) : std::wstring{};
}

// Lists the games that have a DevBridge running, which is what --pid picks from.
void PrintBridgeList() {
    const std::vector<BridgeTarget> bridges = FindBridges();
    if (bridges.empty()) {
        std::printf("no game is running with URKitDevBridge\n");
        return;
    }
    std::printf("%-8s %-30s %s\n", "pid", "process", "pipe");
    for (const BridgeTarget &bridge : bridges) {
        const std::wstring path = ProcessImagePath(bridge.pid);
        const std::string process = path.empty() ? "<exited>" : fs::path(path).filename().string();
        std::printf("%-8u %-30s %ls\n", bridge.pid, process.c_str(), bridge.pipe.c_str());
    }
}

int CommandDev(const std::vector<std::string> &args) {
    if (args.empty())
        return Fail("usage: urk dev <list|status|tests|test <test-name>> [--pid <pid>] [--json]");

    const std::string action = args[0];
    std::string testName;
    std::uint32_t wantedPid = 0;
    bool raw = false;
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--pid" && i + 1 < args.size())
            wantedPid = static_cast<std::uint32_t>(std::strtoul(args[++i].c_str(), nullptr, 10));
        else if (args[i] == "--json")
            raw = true;
        else if (!args[i].empty() && args[i][0] != '-')
            testName = args[i];
    }

    if (action == "list") {
        PrintBridgeList();
        return 0;
    }

    const std::vector<BridgeTarget> bridges = FindBridges();
    if (bridges.empty())
        return Fail("no URKitDevBridge is running; start the game with URKitDevBridge.dll in Mods");
    const BridgeTarget *target = &bridges.front();
    if (wantedPid != 0) {
        const auto found = std::find_if(bridges.begin(), bridges.end(),
                                        [wantedPid](const BridgeTarget &entry) { return entry.pid == wantedPid; });
        if (found == bridges.end())
            return Fail("no DevBridge for pid " + std::to_string(wantedPid));
        target = &*found;
    }

    std::string tool;
    Json arguments = Json::object();
    if (action == "status")
        tool = "runtime_status";
    else if (action == "tests")
        tool = "list_runtime_tests";
    else if (action == "test") {
        if (testName.empty())
            return Fail("usage: urk dev test <test-name> [--pid <pid>]");
        tool = "run_runtime_test";
        arguments["name"] = testName;
    } else {
        return Fail("unknown dev command: " + action);
    }

    Json result;
    std::string error;
    if (!Exchange(*target, tool, arguments, &result, &error)) {
        if (error.find("runtime_timeout") != std::string::npos)
            error += "\n  hint: the bridge runs requests on the Unity main thread; the game must be rendering. "
                     "Un-minimize/focus it and retry.";
        return Fail(error);
    }
    if (raw)
        std::cout << result.dump(2) << "\n";
    else
        PrintResult(tool, result);
    return 0;
}

void PrintUsage() {
    std::printf(
        "urk - URKit repository tool\n\n"
        "  urk new <mod> [--no-imgui] [--vendor-overlay] [--mods-dir <dir>]\n"
        "  urk deploy <mod> --game <dir> [--build <dir>]\n"
        "  urk dev <command> [options]\n"
        "\n"
        "dev commands:\n"
        "  list                 list games running with URKitDevBridge\n"
        "  status               backend, capabilities, current scene\n"
        "  tests                runtime tests exported by the loaded mods\n"
        "  test <test-name>     run one runtime test\n"
        "\n"
        "dev options:\n"
        "  --pid <pid>          target one game from `urk dev list`\n"
        "  --json               print the raw bridge response\n");
}

} // namespace

int main(int argc, char **argv) {
    try {
        if (argc < 2) {
            PrintUsage();
            return 1;
        }
        const std::string command = argv[1];
        std::vector<std::string> rest(argv + 2, argv + argc);
        if (command == "new")
            return CommandNew(rest);
        if (command == "deploy")
            return CommandDeploy(rest);
        if (command == "dev")
            return CommandDev(rest);
        if (command == "help" || command == "--help" || command == "-h") {
            PrintUsage();
            return 0;
        }
        PrintUsage();
        return 1;
    } catch (const std::exception &exception) {
        std::fprintf(stderr, "urk: %s\n", exception.what());
        return 1;
    } catch (...) {
        std::fprintf(stderr, "urk: unknown error\n");
        return 1;
    }
}








