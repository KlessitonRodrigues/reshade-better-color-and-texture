# AGENTS.md

Guidance for agents working in this repository.
File convention: <parent_folder>.<file_name>.ts
Keep agents.md and readme.md up to date on change project structure
Commit small changes using "feat/update/fix/refactor/remove:<short_description>"
Push commits is always manual

## Project Overview

A distributable **ReShade preset** that improves color, shadows, texture detail, and sharpness in PC games. This is a content/config repository, not a compiled software project: it bundles a prebuilt ReShade runtime DLL, INI configuration, HLSL shader effects, and texture LUTs. Users copy the files next to a game executable.

## File Structure

```text
reshade-better-color-and-texture/
├── AGENTS.md                       # Agent guidance (this file)
├── readme.md                       # Purpose, install steps, showcases/videos
├── ReShade.ini                     # ReShade runtime config (search paths, UI, input, screenshots, add-ons)
├── ReShadePreset.ini               # Active preset: enabled techniques + shader parameter overrides
├── dxgi.dll                        # Prebuilt ReShade DXGI proxy DLL (Windows x86-64, PE32+)
├── .github/
│   └── workflows/
│       └── release.yml             # CI: zip repo and publish GitHub Release on push to main
├── addons/
│   └── performance-hud/            # "Performance HUD" ReShade add-on (CPU/GPU usage + power OSD)
│       ├── build.sh                # Cross-build script (llvm-mingw), produced PerformanceHUD.addon64
│       ├── CMakeLists.txt          # CMake project (MSVC or mingw) for the same add-on
│       ├── PerformanceHUD.addon64  # Prebuilt add-on binary, loaded via [ADDON] AddonPath
│       ├── include/                # Vendored ReShade add-on API headers + matching imgui.h (1.92.5)
│       └── src/
│           └── performance_hud.cpp # Add-on implementation (NVML/PDH sampling, OSD overlay)
└── reshade-shaders/
    ├── Shaders/                    # HLSL effects (.fx) and shared headers (.fxh)
    │   ├── ambient_v2.fx           # Ambient removal + contrast curve (DH_Ambient_Remove_V2)
    │   ├── Colourfulness_v3.fx     # Colourfulness + Vibrance variant (ColourfulnessV3)
    │   ├── light.fx                # CeeJay.dk Levels 1.2 - black/white point remap
    │   ├── qUINT_sharp.fx          # Marty McFly DELCS depth-enhanced local contrast sharpener
    │   ├── ReShade.fxh             # ReShade standard header (depth macros, samplers, helpers)
    │   └── ReShadeUI.fxh           # ReShade UI annotation/version compatibility macros
    └── Textures/                   # PNG resources referenced by shaders
        ├── FontAtlas.png           # 512x512 font atlas
        └── lut.png                 # 1024x32 RGB lookup texture
```

## Technologies

- **ReShade** — post-processing injector for games.
- **HLSL** — shader language used by all `.fx`/`.fxh` files (ReShade FX dialect).
- **ReShade** — post-processing injector for games.
- **HLSL** — shader language used by all `.fx`/`.fxh` files (ReShade FX dialect).
- **C++ / WinAPI** — the Performance HUD add-on (`addons/performance-hud/`), built with llvm-mingw or MSVC.
- **DirectX / DXGI** — runtime proxied via `dxgi.dll`.
- **INI configuration** — `ReShade.ini` and `ReShadePreset.ini`.
- **PNG textures** — lookup textures and font atlas.
- **GitHub Actions** — release automation (`.github/workflows/release.yml`).
- **Git / GitHub** — version control and distribution.

## Conventions

- Every `.fx` includes `"ReShade.fxh"` and defines a `technique <Name>` block with `pass { VertexShader = PostProcessVS; PixelShader = ...; }`.
- Tunables are exposed via `uniform` declarations with `ui_type`, `ui_min`/`ui_max`, `ui_step`, `ui_label`, and `ui_tooltip` annotations.
- Third-party shaders are wrapped in namespaces (e.g. `DH_Ambient_Remove_030`, `DH_Ambient_Remove_V2`, `qUINT`) and retain original authors/licenses.
- Variant shaders use a `_v2` suffix (`ambient_v2.fx`, `Colourfulness_v2.fx`); the preset selects one variant of each.
- `ReShadePreset.ini` line 1 `Techniques=` lists active effects as `TechniqueName@ShaderFile.fx`. `TechniqueSorting=` may list defined-but-disabled effects.
- `ReShade.ini` uses relative `EffectSearchPaths`/`TextureSearchPaths`/`PresetPath` for portability, but contains some machine-specific absolute paths.
- Keep the full working setup (DLL, INIs, shaders, textures) in release ZIPs, not just source shaders.

## Build / Test / Release

- The HLSL preset has **no build system or test suite**: validation is manual (load the preset in ReShade and verify effects render without compile errors).
- The Performance HUD add-on (`addons/performance-hud/`) builds via `build.sh` (llvm-mingw cross-build, also works on Linux) or `CMakeLists.txt` (MSVC on Windows). A prebuilt `PerformanceHUD.addon64` is committed and loaded via `[ADDON] AddonPath` in `ReShade.ini`; rebuild it after editing `src/performance_hud.cpp` and make sure the vendored headers in `include/` (reshade + imgui 1.92.5) stay in sync with the installed ReShade version.
- Release: pushing to `main` triggers `.github/workflows/release.yml`, which tags `v${GITHUB_RUN_NUMBER}`, zips the repo (excluding `.git/*` and `.github/*`), and publishes a GitHub Release.

## Notes for Agents

- Do not edit `dxgi.dll` (prebuilt binary) or the PNG textures unless explicitly requested.
- Preserve upstream attribution, licenses, and namespaces when modifying shaders.
- Keep INI paths relative where possible for portability.
- No secrets or keys are present; do not introduce any.
