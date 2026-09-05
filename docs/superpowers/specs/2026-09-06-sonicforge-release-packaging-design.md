# SonicForge Release Packaging — Design

## Goal

Produce a self-contained, portable Windows ZIP of this Audacity 4 fork (with its
`src/mcp/` MCP bridge built in) that a non-developer can download, unzip, and run —
no Qt/CMake/Visual Studio toolchain required. Distributed under a new product
name, not "Audacity", per real trademark precedent (see Background).

## Background

"Audacity" is a registered trademark owned by Muse Group. There is no currently
published formal trademark policy page on audacityteam.org, but real precedent
exists: when a 2021 fork stripped Audacity's telemetry, Muse Group required it to
fully rename (it shipped as "Tenacity", not "Audacity Lite" or similar) rather than
accepting a disclaimer. Building from source and running it locally under
upstream's own "Audacity" branding (what this repo does today) is not the risk —
the risk is specifically a publicly distributed, pre-built binary carrying the
"Audacity" name as its own product identity, which is what this design produces.
The realistic worst case for non-compliance is a cease-and-desist / takedown
request, not litigation — renaming proactively avoids that scenario rather than
just preparing to comply with it later.

Chosen product name for the distributed build: **SonicForge**. This applies only
to the compiled app's branding (exe name, window title, about box, bundle
identifier). The Python MCP server repo (`Audacity4MCP` / `Audacity4-MCP` on
GitHub) is unaffected — it's a companion/sidecar tool referencing Audacity by
name in a nominative-fair-use sense ("MCP support for Audacity 4"), not a rebrand
of the editor itself, so it keeps its existing name.

## Scope

In scope: one Windows x64 portable ZIP release, built from this repo's current
`main`-equivalent state, with the MCP bridge (`src/mcp/`) included.

Out of scope: macOS/Linux builds, an installer (MSI/Inno Setup) — a portable ZIP
only, code-signing, auto-update mechanism, a real custom app icon/logo design
(a plain placeholder ships for now; a real logo is a follow-up task for the user).

## Rename

All in `version.cmake`:

| Variable | Current | New |
|---|---|---|
| `MUSE_APP_NAME_HUMAN_READABLE` | `"Audacity"` | `"SonicForge"` |
| `MUSE_APP_NAME_MACHINE_READABLE` | `"Audacity"` | `"SonicForge"` |
| `MUSE_APP_NAME_HUMAN_READABLE_COMPAT` | `"Audacity"` | `"SonicForge"` |
| `MUSE_APP_NAME_MACHINE_READABLE_COMPAT` | `"Audacity"` | `"SonicForge"` |
| `MUSE_APP_GUI_IDENTIFIER` | `org.audacityteam.audacity4` | `com.xdarkzx.sonicforge` |

Confirmed via grep across `*.cmake`/`*.cpp`/`*.h`/`CMakeLists.txt` (excluding
`build/`) that the `_COMPAT` variables are only consumed to define
`MUSE_APP_NAME` in `version.cmake` itself — no other file branches on their
literal string value, so this rename carries no hidden behavior change (no old
settings-migration path depends on the literal word "Audacity").

`MUSE_APP_VERSION_MAJOR` stays `"4"` (reflects the Audacity-4-derived engine
internally) — so the produced executable is `SonicForge4.exe`, window title
`SonicForge 4`, Start Menu / window-manager class `SonicForge4Development` (per
`WINDOW_MANAGER_CLASS` in `SetupAppImagePackaging.cmake`, Linux-only, not part of
this Windows-only pass but confirmed as consuming the same rebranded name).

Icon: `au3/win/audacity.rc` currently references `au3/win/audacity.ico` (the real
Audacity leaf logo) as the exe's icon resource. A different product name with the
original logo still visually reads as Audacity, so this gets swapped: generate a
plain placeholder `.ico` (a simple solid-color shape, no text, no waveform/leaf
imagery resembling the original) via a small one-off script, drop it in as
`au3/win/sonicforge.ico`, and repoint `audacity.rc`'s icon resource to it.
Replacing this with real designed branding later is a follow-up task for the
user, not part of this pass.

## Build

Use the existing `audacity-release` CMake preset (`CMAKE_BUILD_TYPE:
RelWithDebInfo`, inherits `base`) — this preset has never actually been built
before in this project; only `audacity-debug` has. Real risk of hitting new
compile errors that never surfaced in debug builds (different optimization
levels, different `NDEBUG`-gated code paths). Same flow as every other build this
project has done:

```bat
"...\VC\Auxiliary\Build\vcvars64.bat"
cmake --preset audacity-release
cmake --build build/audacity-release
cmake --install build/audacity-release
```

Deploys to `src/app/bin/` per the existing `CMAKE_INSTALL_PREFIX` pattern
(confirmed this is the same install prefix used by `audacity-debug`; if the
release preset's actual prefix differs this gets corrected during the build task,
not guessed here).

## Dependency bundling

- **Qt DLLs**: run `windeployqt.exe` (Qt's own official bundling tool, ships with
  the Qt install already used to build this project) against the built
  `SonicForge4.exe`. This is the standard, correct approach for a portable Qt
  app — it copies exactly the DLLs/plugins that binary actually needs, rather than
  guessing.
- **MSVC runtime**: copy the specific redistributable DLLs (e.g. `msvcp140.dll`,
  `vcruntime140.dll`, `vcruntime140_1.dll`) from the Visual Studio install's own
  redist folder (`VC\Redist\MSVC\<version>\x64\Microsoft.VC143.CRT\`).
  Microsoft's VS license explicitly permits redistributing these specific files
  alongside an app; this avoids requiring users to separately install the VC++
  Redistributable.
- **No third-party VST3 plugins are bundled.** FabFilter, ValhallaDSP, etc. (used
  during this project's own testing) are commercial, no redistribution rights.
  Only Audacity's own built-in (GPL) effects ship. The package's README notes
  that users can drop their own separately-owned VST3s into the standard VST3
  folder afterward — realtime-effect/VST3 hosting still works, it's just empty
  of plugins out of the box.

## Package contents & disclaimer

One ZIP, `SonicForge4-win64.zip`, containing:

- `SonicForge4.exe` + all bundled DLLs/Qt plugins/resources from the steps above
- `NOTICE.txt` (new file, package root) stating:
  - This is an unofficial, community-modified build of Audacity 4, with
    experimental AI/MCP automation added via `src/mcp/`.
  - Not affiliated with, endorsed by, or supported by the Audacity team or Muse
    Group. Do not contact Audacity's own support channels about this build.
  - Issues specific to this build: report at this project's GitHub
    (`Audacity4-Dev`), not upstream Audacity's tracker.
  - Link to real, official Audacity (audacityteam.org) for the standard,
    supported version.
  - GPL v3 source-availability notice (this build's own source is publicly on
    GitHub) — required alongside any binary distribution under the GPL.

## Testing / verification

- Build succeeds (`cmake --build` exits 0) — if it doesn't on the first attempt,
  fix compile errors as they surface; this is expected to be non-trivial since
  the preset is untested.
- Launch `SonicForge4.exe` from the packaged folder (not the build tree) on a
  clean check that it starts, shows "SonicForge 4" as its title/about-box (not
  "Audacity"), and the MCP bridge still starts (matches this project's existing
  live-verification standard — connect the Python bridge client and confirm
  `project-get-info` succeeds).
- Confirm no VST3 plugin `.vst3` files are present anywhere in the packaged
  folder before zipping.
