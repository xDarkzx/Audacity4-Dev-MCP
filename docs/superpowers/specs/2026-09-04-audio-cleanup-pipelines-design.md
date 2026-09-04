# Audio Cleanup Pipelines - Design

**Status:** Approved for planning
**Repos touched:** `audacity/audacity` (this repo, `src/mcp/`), `Audacity4MCP` (Python MCP server)

## Goal

Bring Audacity v3 MCP's genre-specific audio cleanup pipelines (podcast, audiobook/ACX, interview,
vocal, live, music mastering, lo-fi) to Audacity 4, so the same "one-click cleanup" experience v3
users had is available again. First vertical slice: podcast cleanup, then audiobook mastering.
Remaining pipelines follow once the primitive is proven, since they are template instances of the
same pattern with different parameter data.

## Why this shape

v3's `AudacityMCP` project (`D:\DansProject\AudacityMCP`) already solved this problem once, via
mod-script-pipe. Reading its source directly (not from memory) shows the pattern clearly:

- `effects_tools.py`: one typed MCP tool per built-in effect, each a thin, validated wrapper over
  `client.execute_long("EffectId", Param=value, ...)`.
- `cleanup_tools.py`: a separate orchestration layer with genre pipelines that chain effect calls,
  an `auto_analyze_audio` tool that exports the current selection to a temp WAV and measures it in
  pure Python (peak, RMS, noise floor, click count, silence gaps, dynamic range, DC offset - zero
  DSP code needed beyond stdlib `wave`), and async job tracking since pipelines take minutes.
- Every pipeline follows the same safety discipline: measure first, only ever *reduce* peaks/gain
  automatically, never blindly boost, and refuse (rather than silently ship a bad result) when a
  planned gain change would clip.

Investigating Audacity 4's C++ side confirms the exact same shape is achievable with almost no new
code, because the primitives already exist:

- `IEffectExecutionScenario::performEffect(const EffectId& effectId, const std::string& params)`
  (`src/effects/effects_base/internal/effectexecutionscenario.h/cpp`) already exists in production
  code today. `params` is parsed via `ShuttleSetAutomation` from `CommandParameters`
  (`au3/libraries/au3-components/EffectAutomationParameters.h`) - the same automation/macro
  parameter format (`Key=Value Key2="quoted value"`, shell-tokenized, split on first `=`) that
  Audacity's mod-script-pipe used, which v3 already targeted. This means v3's hard-won parameter
  names (`Reduction=`, `Sensitivity=`, `Threshold=`, `Ratio=`, `PeakLevel=`, etc.) are very likely
  to transfer directly.
- `src/effects/builtin_collection/` confirms the effects v3's pipelines depend on exist in v4:
  `noisereduction`, `clickremoval`, `normalize`, `truncatesilence`, `compressor`, `limiter`,
  `basstreble`, and (new, and better than what v3 had) a real LUFS-based `loudness` effect.
- `IExporter::exportData(path, options, progress, project)` (`src/importexport/export/iexporter.h`)
  already exists and is sufficient to reproduce v3's WAV-export-then-measure analysis approach with
  zero new DSP code on the C++ side.
- `ISelectionController` (already partially wired into the MCP controller for labels) has
  everything needed to scope pipeline steps (`setSelectedTracks`, time selection).

So the same split-architecture pattern already used for labels this session applies again: **C++
stays a thin, generic, DSP-free primitive layer; Python owns all pipeline/genre intelligence.**
This keeps the diff against upstream `audacity/audacity` small (new commands in `src/mcp/`, no
changes to effects/export/selection internals) - the explicit scope constraint this session has
held to throughout.

## Non-goals for this slice

- Not porting all ~10 v3 pipelines at once. Podcast first, fully verified live, then audiobook,
  then the rest (each remaining genre is parameter data once the primitive works - low marginal
  cost, but still ported one at a time and spot-checked, not bulk-copied blind).
- Not implementing effect discovery/introspection (enumerating available effects and their
  parameter schemas from C++). v3 hand-maintained this knowledge in Python and that knowledge
  transfers; live effect discovery is a possible future enhancement, not required here.
- Not doing DSP/analysis math in C++. Analysis stays a WAV export + pure-Python measurement, as
  in v3.
- Not implementing `new-project` here - unrelated, already tracked as a separate open item.

## C++ additions (`src/mcp/`)

Three new commands, following the exact pattern already established for labels (register in
`audacitycommandsregister.cpp`, handler in `audacitycommandscontroller.cpp`/`.h`, `hasOpenProject()`
guard on every handler that touches project state):

### `command://mcp/apply-effect`
Args: `effect_id` (string, required), `params` (string, optional - pre-formatted
`Key=Value Key2="value"` automation string).

```cpp
Response AudacityCommandsController::handleApplyEffect(const Request& request)
{
    if (!hasOpenProject()) { /* ...standard error... */ }
    if (!effectExecutionScenario()) { /* ...standard error... */ }

    std::string effectId = request.query.param("effect_id").toString();
    if (effectId.empty()) { /* ...error: effect_id required... */ }

    std::string params = request.query.param("params").toString();

    muse::Ret ret = params.empty()
        ? effectExecutionScenario()->performEffect(muse::String::fromStdString(effectId))
        : effectExecutionScenario()->performEffect(muse::String::fromStdString(effectId),
                                                     params);
    return make_response(request, ret);
}
```

Requires injecting `muse::ContextInject<au::effects::IEffectExecutionScenario>
effectExecutionScenario` into the controller. `performEffect` already reports failures (unknown
effect id, invalid params, no selection) via its `Ret` - the handler is a pure pass-through, no new
validation logic needed on the C++ side (param validation belongs in Python, matching v3, where
`effect_amplify`, `effect_reverb`, etc. validate ranges before ever reaching the wire).

### `command://mcp/select-all`
No args. Wraps `ISelectionController::setSelectedTracks()` (all tracks) plus whatever full-time-range
selection call is needed (equivalent to v3's `SelAllTracks` + `SelectAll` pair) - exact calls
confirmed during implementation by reading `ISelectionController`'s full interface and how existing
"Select All" UI actions invoke it.

### `command://mcp/select-time`
Args: `start` (number, seconds), `end` (number, seconds). Sets the time selection without changing
track selection, for pipeline steps that need to scope to a sub-region (e.g. noise-profile capture
from the first 0.5s).

### `command://mcp/export-wav`
Args: `path` (string, required - where to write the temp WAV).
Wraps `IExporter::exportData()` with WAV format options, single channel (matching v3's
`Export2 NumChannels=1` - analysis doesn't need stereo). Returns success/failure only; the Python
side owns opening and parsing the file, exactly as in v3.

All four get a unit test for the "no project open" guard (mirroring the three added for labels),
plus whatever handler-specific tests make sense once real behavior is verified live.

## Python additions (`Audacity4MCP`)

New/changed files, directly modeled on v3's existing files of (almost) the same name:

- **`server/tools/analysis_tools.py`** (new): `auto_analyze_audio`, porting v3's `_measure_wav` and
  the issue-detection/recommendation logic in `cleanup_tools.py` nearly verbatim - it is pure stdlib
  `wave`/`struct`/`math`, no v3-specific dependency. Calls the new `export-wav` MCP command instead
  of v3's `Export2` pipe command.
- **`server/tools/effects_tools.py`** (new): typed wrappers over `apply-effect`, ported from v3's
  file of the same name, covering the effects the podcast and audiobook pipelines need first
  (`noise_reduction`, `normalize`, `click_removal` (deferred if unused by these two), `compressor`,
  `limiter`, `loudness_normalize`) - the rest of v3's effect wrappers (reverb, echo, phaser, etc.)
  follow later, same low-marginal-cost reasoning as the remaining pipelines.
- **`server/tools/cleanup_tools.py`** (new): pipeline orchestration + background job tracking,
  structurally ported from v3 (`_create_job`, `_run_pipeline_step`, `check_pipeline_status`, the
  stale-job cleanup, the "refuse if a pipeline is already running" guard).

### Podcast pipeline - settings

v3's automatic pipeline deliberately avoided true LUFS normalization (its only path to it was a
fragile pipe round-trip it couldn't fully verify). v4's real `loudness` built-in effect changes
that calculus. Design:

1. DC offset removal, HPF 80Hz, noise reduction (as in v3 - unchanged, no evidence these needed
   correction).
2. Compression: keep v3's 3:1, 30ms attack, 200ms release, peak-based (voice-tuned values, not
   contradicted by research).
3. **Loudness: target -16 LUFS by default** (Apple Podcasts' published spec; also within Spotify's
   -14 LUFS guidance's practical tolerance) via the real `loudness` effect, gated by the same
   measure-before/refuse-if-clip safety pattern v3 used for its manual `loudness_normalize` tool -
   ported as an automatic pipeline step now that it's backed by a real effect instead of a
   best-effort pipe call. True peak ceiling -1 dBTP.
4. If the safety check refuses (projected clipping), fall back to v3's old behavior: reduce peaks
   only if hot, never boost, and report in the result that LUFS targeting was skipped and why.

### Audiobook pipeline - settings

Verified against current ACX spec (RMS -23 to -18 dBFS, peak ≤ -3 dBFS, noise floor ≤ -60 dBFS):
v3's numbers (RMS -20dB target via compression + safe loudness step, peak cap -3.5dB for margin)
land correctly inside every one of those ranges. **Port unchanged.**

## Error handling

Every C++ handler: `hasOpenProject()` guard first (established pattern), then pass through the
underlying interface's `Ret`/failure rather than inventing new error paths - `apply-effect` in
particular should stay a thin pass-through so effect-specific failures (bad params, no selection)
surface with Audacity's own error text rather than being swallowed or reworded.

Every Python pipeline step: keep v3's per-step try/except-and-continue pattern (`_run_pipeline_step`)
so one failed step doesn't abort the whole pipeline silently - failures accumulate in
`steps_failed` and are reported in the final job result, exactly as in v3.

## Testing

- C++: unit tests for the "no project open" guard on all four new commands (mirrors the three
  already added for labels). Live-tested against a real running Audacity4 instance before
  considering any command "done", per this session's established rigor - including deliberately
  reproducing edge cases (e.g. calling `apply-effect` with no selection) rather than only the happy
  path.
- Python: the podcast pipeline gets a full live run against a real noisy test recording, checked
  both by MCP response (`check_pipeline_status` result) and by ear/visual inspection (waveform,
  measured LUFS) before being called done. Same for audiobook, checked against the ACX numbers
  above.

## Open questions to resolve during implementation (not blocking spec approval)

- Exact `ISelectionController` calls needed for "select all tracks + all time" - confirmed by
  reading the interface fully and an existing "Select All" UI action call site.
- Exact WAV export `Options` needed (format id for WAV, channel count) - confirmed by reading
  `IExporter::formatsList()`/existing export call sites for the right `OptionKey::Format` value.
