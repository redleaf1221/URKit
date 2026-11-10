# URsdk architecture

Three layers, one build:

- **loader** — runs inside the game, finds IL2CPP, exposes the ABI, loads mods.
- **sdk** — the headers a mod compiles against. Real files in `src/sdk/`, not generated text.
- **mod** — one directory under `src/mods/`, one DLL, one `ModInitEx` export.

## Source map

| Path | Responsibility |
|---|---|
| `src/sdk/mod_sdk.h` | public ABI: versions, capabilities, contexts, API tables |
| `src/loader/platform/` | `DllMain`, process filtering, start the bootstrap thread |
| `src/loader/core/loader/` | startup flow, runtime discovery, mod loading, events |
| `src/loader/core/` | config, logging, hooks, lifecycle, networking, shared state |
| `src/loader/unity/` | the IL2CPP export resolver and its public adapter table |
| `src/loader/proxy/` | `version.dll`, `winhttp.dll`, `winmm.dll`, `dwmapi.dll`, `msimg32.dll` |
| `src/overlay/` | ImGui host, theme/widgets, D3D11/D3D12/OpenGL presentation hooks (compiled per mod) |
| `src/sdk/` | runtime helpers, events, hooks, network, coroutines, Unity types, IL2CPP adapter |
| `src/sdk/il2cpp/` | `URK::il2cpp`: thin, size-checked wrappers over `URK_Il2CppApi` |
| `src/sdk/unity/` | `URK::Unity`: `TypeRef`, `Object`, components, invoke, inspect, shortcuts |
| `src/mods/_template/` | the mod scaffold, copied by `urk new` |
| `src/tools/urk/` | `new`, `deploy`, `dev` |
| `src/dev/bridge/` | `URKitDevBridge.dll`: runtime test discovery over a named pipe |

## Runtime flow

1. A proxy loader (or `URKitInjector.dll`) starts the runtime on a bootstrap
   thread, outside `DllMain`.
2. A process-wide owner mutex keeps exactly one runtime per process; other
   proxies only forward.
3. Startup qualifies the process by loaded modules: `GameAssembly.dll` means
   IL2CPP, everything else forwards and returns.
4. `Il2Cpp_BindExports` reads the GameAssembly export table, binds each export by
   name **and** ABI signature, and publishes the validated table as
   `URK_Il2CppApi` through `URK_ModContext`.
5. Metadata readiness is probed with the IL2CPP thread attached; nothing is
   guessed from private layouts.
6. `Mods\*.dll` are loaded, `ModInitEx` runs, and the event pump starts.

Capabilities stay explicit: a missing event pump, input service or backend entry
point is simply absent from the context, and the SDK fails closed.

## The SDK is compiled, not printed

`src/sdk/compile_check.cpp` includes the public surface and is built as
`urk_sdk_check` in every configuration. That turns "the generated header is
broken" into a build failure at the header, not in a user project three steps
later. It already caught one real bug: a non-`inline` out-of-class definition in
`unity_invoke.h` that collided as soon as two translation units included it.

ImGui is optional per build: when `URK_WITH_IMGUI=OFF`, `urk_add_mod` drops the
overlay sources and no ImGui, D3D or DXGI code is linked into the mod at all.

## Ownership rules

- `src/sdk/mod_sdk.h` is the ABI source of truth. Tables are append-only and carry
  `version`/`size` so either side can reject an incompatible peer.
- Runtime state and loader-owned resources stay under `src/loader/core`.
- IL2CPP lookup code stays in `src/loader/unity`; the SDK never resolves
  exports or method pointers itself.
- A mod owns its UI, hooks and config; the loader arbitrates the hook chain, the
  window procedure and cursor leases.

## Concurrency

The loader starts outside `DllMain`. Worker threads use generation tokens during
shutdown, managed calls attach their thread, and hooks are removed before a mod
module is freed. Code under the event mutex stays short and never calls user
code.




