# URKit

A loader-sdk-mod monorepo for IL2CPP Unity mods on Windows x64. Everything is
ordinary CMake: one repository builds the loader DLLs, the SDK, the mods and the
`urk` command line.

This is a restructuring of URKit. Mono and Unreal support, the wxWidgets
application, the MCP server and the string-based project generator are gone. What
used to be generated text is now source code that the compiler checks.

## Layout

```
src/loader/       the in-process runtime (loader, hooks, events, IL2CPP adapter)
src/sdk/          the public SDK headers, compiled in every build
src/overlay/      the ImGui host and D3D presentation hooks (compiled into each mod)
src/mods/_template/  the scaffold every mod is copied from
src/mods/<name>/     one mod: mod/ sources plus its own mod_config.h
src/tools/urk/    the command line: new / deploy / dev
cmake/            source lists and the urk_add_mod() rules
src/dev/bridge/   URKitDevBridge.dll, the in-game test bridge
```

A mod is a normal target, not generated output:

```
src/mods/hello/
  mod/config/mod_config.h     name, mod id, author, version
  mod/support/                logging
  mod/lifecycle/              update(), network, runtime hooks
  mod/hooks/                  your hooks, plus the optional ImGui overlay
  mod/ui/                     menu, theme, tabs, highlight (ImGui builds only)
  mod/mod_entry.cpp           the fixed exports (ModInitEx and friends)
```

`src/mods/_template` is the scaffold. `urk new` copies it and rewrites the name; the
copy is then edited by hand like any other source directory.

## Build

```powershell
cmake --preset msvc-relwithdebinfo
cmake --build cmake-build-relwithdebinfo --parallel
```

Outputs:

| Target | File |
|---|---|
| `URKit`, `URKitWinHttp`, `URKitWinMm`, `URKitDwmApi`, `URKitMsImg32` | the proxy loaders |
| `URKitInjected` | `URKitInjector.dll` |
| `urk` | `urk.exe` |
| `hello`, `demo`, ... | one DLL per mod under `src/mods/` |
| `urk_sdk_check` | compiles `sdk/` so a broken header fails the build |

Without an overlay, everything ImGui-related is dropped from the build:

```powershell
cmake -S . -B build-noimgui -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo `
      -DURK_WITH_IMGUI=OFF
cmake --build build-noimgui --target hello
```

The resulting mod links only `KERNEL32.dll` and is roughly a fifth of the size of
an ImGui build.

## Workflow

```powershell
.\cmake-build-relwithdebinfo\urk.exe new MyMod
cmake --build cmake-build-relwithdebinfo --target MyMod
.\cmake-build-relwithdebinfo\urk.exe deploy MyMod --game C:\Games\Example
```

`deploy` copies the built DLL into `<game>\Mods`. Start the game with a proxy
loader next to its executable and read `URKit_logs.log`.

With `URKitDevBridge.dll` in `Mods`, `urk dev` talks to the running game:

```powershell
.\cmake-build-relwithdebinfo\urk.exe dev status          # backend, capabilities, current scene
.\cmake-build-relwithdebinfo\urk.exe dev tests           # runtime tests exported by loaded mods
.\cmake-build-relwithdebinfo\urk.exe dev test MyMod.Basic
```


## The overlay belongs to the mod

`src/overlay/` holds the ImGui host, the theme/widgets/localization kit and the
D3D11/D3D12/OpenGL presentation hooks. It is **not** a library: `urk_add_mod`
compiles those sources into the mod DLL, so the ImGui context, the device hooks
and the menu state are per mod. Nothing is shared across mods at runtime.

The mod drives it:

```cpp
ModRenderHook::add_frame_callback(&draw_hitboxes);   // runs inside the frame
ModRenderHook::add_device_ready_callback(&make_gpu_resources);
ModRenderHook::install(ctx, config);                 // config carries ModConfig values
```

A mod that needs to change the overlay itself copies it once and owns that copy:

```powershell
.\urk.exe new bigmod --vendor-overlay
```

`src/mods/bigmod/overlay/` is then compiled instead of the shared sources, and
`mod_target.cmake` records it. Repository updates no longer touch that copy.
## Tests

```powershell
cmake --build cmake-build-relwithdebinfo --target urk_tests --parallel
ctest --test-dir cmake-build-relwithdebinfo -C RelWithDebInfo --output-on-failure
```

`urk_tests` is a plain executable with a 60-line harness and no test-framework
dependency. It covers what does not need a game: IL2CPP name matching, export
binding policy, cursor leases, the POD vector math, the ABI layout and version
invariants, and the `urk new` naming rules. `urk_sdk_check` compiles the public
SDK surface in every build, so a broken header fails even when only the loader
is built.

Anything that needs a running game is a runtime test executed through the
DevBridge; see [docs/DEV_BRIDGE.md](docs/DEV_BRIDGE.md).
## Where the old features went

| Removed | Reason |
|---|---|
| Mono backend | one backend keeps the ABI and the event pump readable |
| Unreal backend | never left preview; it carried a second reflection stack |
| `urk.exe` GUI, installer, self-update | wxWidgets dependency for a project generator |
| MCP server | `urk dev` covers it; MCP can be an adapter later |
| string-based generator | 18k lines of templates were 99% static text |

`mod_sdk.h` is still the single ABI source of truth for loader and mods. It is
included from `sdk/` by both sides, so there is no embedded copy to keep in sync.

## License

MIT. See [LICENSE](LICENSE).
