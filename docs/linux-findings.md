# Findings: Audacity4-Dev-MCP on Debian 13 (Qt 6.8.2) with libforge-mcp

Found while using the build for a real production (sermon with music beds, mixed
over MCP). Each entry: what happens, cause, state.

| # | Area | Finding | State |
|---|------|---------|-------|
| 1 | mcp | A failing command's message did not reach the client ("Command failed with an unrecognised error"): au3 throws AudacityException, which is not a std::exception. | fixed: MessageBoxException message passed on |
| 2 | project | Importing into a project restored from an unsaved session (after a reboot) fails; the manual import (File > Import) crashes. A fresh project imports fine. | open |
| 3 | mcp | `trim-clip` failed with a valid key while labels were selected (after `add-labels`): the trim delta is clamped to selected labels. | fixed: trim/stretch clear the label selection first |
| 4 | mcp | `list-labels` returned `[]`: the labels had been deleted by #7. | not a bug |
| 5 | mcp | `project-get-info` lists label tracks only as `labelTrackCount`. | by design |
| 6 | mcp | `add-label-track` reported failure once (project restored from an unsaved session); not reproducible in a fresh project. | watch |
| 7 | mcp | `select-time` ("without changing track selection") called setSelectedAllAudioData and selected every track; a following `edit-split-delete` cut all tracks, labels included. | fixed: keeps the selected tracks |
| 8 | mcp / forge | `mcp.ini` with the bearer token is created world-readable (0644). | open |
| 9 | mcp / forge | The server instructions sent to clients are the ForgeIEC (PLC/fieldbus) text, not Audacity's `instructions()`. | open |
| 10 | Qt 6.8 | File dialog: `Interactive::selectOpeningFilesSync` "Not supported", the QML fallback sets `selectExisting`, which Qt 6.8's FileDialog does not have; hidden folders cannot be reached. | open |
| 11 | Qt 6.8 | `LegacyTreeView.qml`: `ReferenceError: __listView is not defined`, `__currentRow`. | open |
| 12 | mcp | No command for the audio setup; a playback device bound to raw hardware (hw:x,y) fails while PipeWire holds it. | fixed: audio-get-devices / audio-set-device |
