# DevBridge: runtime tests against a running game

`URKitDevBridge.dll` is a mod-shaped DLL that runs inside the game and executes
tests exported by the other loaded mods on the Unity main thread. It is the only
way to assert things that need a live runtime: a resolved `TypeRef`, a field
read, a hook firing, a scene transition.

`urk dev` is the client. The bridge is deliberately not a general RPC surface:
three tools, one process, tests only.

## Install

Build it with the rest of the repository, then copy it into the game's `Mods`
folder next to your mod:

```powershell
cmake --build cmake-build-relwithdebinfo --target URKitDevBridge --parallel
copy cmake-build-relwithdebinfo\URKitDevBridge.dll C:\Games\Example\Mods\
```

Keep it out of public gameplay installs: it opens a named pipe and runs
whatever the loaded mods export.

## Use

```powershell
.\cmake-build-relwithdebinfo\urk.exe dev list       # games running a DevBridge
.\cmake-build-relwithdebinfo\urk.exe dev status     # backend, capabilities, scene
.\cmake-build-relwithdebinfo\urk.exe dev tests      # tests exported by loaded mods
.\cmake-build-relwithdebinfo\urk.exe dev test HeroVitals.RegistryWraps
```

`dev list` shows the pid to pass to `--pid` when more than one game is running:

```text
pid      process                        pipe
41704    MessageFromAliens.exe          \\.\pipe\URKit.DevBridge.41704
```

`--json` prints the raw bridge payload instead of the summary. `--pid <pid>`
picks a specific game.

A request is executed on the Unity main thread, so the game has to be pumping
frames. A minimized or paused game makes the bridge answer `runtime_timeout`
after 15 seconds; focus the window and retry.

Sample `dev status`:

```text
pid      21432
backend  il2cpp
sdk      35
caps     0x6FF1
scene    MainMenu (build 0, handle 2)
```

Exit code is `0` on success and `1` on failure, including a failed test. That
makes it usable directly from a script or a CI step that has a game running.

## Writing a runtime test

A mod exports three C functions. The bridge finds them by name in every loaded
mod DLL, so nothing has to be registered:

| Export | Signature |
|---|---|
| `URK_DevTestCount` | `uint32_t()` |
| `URK_DevTestDescribe` | `int(uint32_t index, URK_DevTestDescriptor *out)` |
| `URK_DevTestRun` | `int(const char *name, URK_DevTestResult *out)` |

Return `1` for success and `0` for failure; the descriptor and result structs
are in [`src/sdk/dev_test.h`](../src/sdk/dev_test.h). A minimal test file in a
mod:

```cpp
#include "sdk/dev_test.h"
#include "sdk/unity/unity.h"

namespace {
constexpr const char *kTestName = "RegistryWraps";

int run(const char *name, URK_DevTestResult *result) {
    if (std::strcmp(name, kTestName) != 0)
        return 0;
    URK::Unity::clear_error();
    const auto player = URK::Unity::GameObject::FindWithTag("Player");
    result->passed = player ? 1 : 0;
    std::snprintf(result->message, sizeof(result->message), "%s",
                  player ? "player found" : "player not found");
    return 1;
}
} // namespace

extern "C" __declspec(dllexport) uint32_t URK_DevTestCount() {
    return 1;
}

extern "C" __declspec(dllexport) int URK_DevTestDescribe(uint32_t index, URK_DevTestDescriptor *out) {
    if (index != 0 || !out)
        return 0;
    out->version = URK_DEV_TEST_API_VERSION;
    out->size = sizeof(*out);
    std::snprintf(out->name, sizeof(out->name), "%s", kTestName);
    std::snprintf(out->sourceFile, sizeof(out->sourceFile), "%s", __FILE__);
    out->sourceLine = __LINE__;
    return 1;
}

extern "C" __declspec(dllexport) int URK_DevTestRun(const char *name, URK_DevTestResult *result) {
    if (!result)
        return 0;
    result->version = URK_DEV_TEST_API_VERSION;
    result->size = sizeof(*result);
    return run(name, result);
}
```

Tests run on the Unity main thread, which is what makes `URK::Unity` calls legal
there. Keep them short: the bridge waits 15 seconds and reports `runtime_timeout`
afterwards. Exceptions and crashes are contained per call (`test_crashed`) so one
bad test does not take the game down.

## Protocol

For clients other than `urk`, the bridge publishes itself and speaks a small
line protocol.

Discovery file, written at startup:

```text
%LOCALAPPDATA%\URK\DevBridge\bridges\<pid>.json
{"protocol":"1","pid":21432,"pipe":"\\\\.\\pipe\\URK.DevBridge.21432"}
```

The pipe is byte-mode, duplex, local-only. One request or response per line, no
nested framing:

```json
{"version":"1","id":"1","tool":"run_runtime_test","arguments":{"name":"HeroVitals.RegistryWraps"}}
{"version":"1","id":"1","ok":true,"result":{"name":"RegistryWraps","qualified_name":"HeroVitals.dll!RegistryWraps","module":"HeroVitals.dll","passed":true,"duration_microseconds":412,"message":"player found","details":""}}
```

Failures use `ok:false` with `error_code` and `error_message`. Codes you will
see: `invalid_request`, `tool_not_found`, `invalid_arguments`, `runtime_unavailable`,
`bridge_busy` (queue full), `runtime_timeout`, `test_not_found`,
`ambiguous_test`, `test_crashed`, `test_execution_failed`, `invalid_test_result`.

Messages are capped at 64 KB and the request queue at 32 entries.

## Why not MCP

The earlier URKit shipped an MCP server that wrapped these three tools plus
project and build operations. The project operations duplicated the build system
and the mod manifest, and the remaining runtime tools are three JSON calls a
command line covers in a few hundred lines. A separate MCP adapter on top of
`urk dev` is possible later; it is not the primary interface.

