# `<mod name>`

A mod in this repository. The sources here are the mod's own; everything else
(ImGui host, D3D presentation hooks, Unity SDK) is shared:

- `overlay/` in the repository root holds the ImGui host and the DX11/DX12/OpenGL
  presentation hooks. It is compiled into this mod's DLL, so the ImGui context,
  device hooks and menu state are per mod. Register your own drawing with
  `ModRenderHook::add_frame_callback`.
- `sdk/` holds the URKit SDK headers and the IL2CPP adapter.

Files you will usually touch:

| Path | Purpose |
|---|---|
| `mod/config/mod_config.h` | name, id, author, version, menu key |
| `mod/mod_entry.cpp` | the exported `ModInitEx` and scene callbacks |
| `mod/lifecycle/mod_runtime.*` | `update()` and scene handling |
| `mod/hooks/mod_hooks.*` | where the mod installs its own hooks |
| `mod/ui/menu.h`, `mod/ui/tabs/` | the menu contents |
| `mod/support/mod_log.*` | mod-local logging |

`mod_target.cmake` holds per-mod switches; `urk new --no-imgui` writes
`set(URK_MOD_WITH_IMGUI OFF)` there.
