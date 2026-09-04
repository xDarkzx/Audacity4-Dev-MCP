# Audio Cleanup Pipelines Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bring v3's genre cleanup pipelines to Audacity 4 - starting with podcast, then audiobook (ACX) - built on 4 new thin C++ MCP primitives plus a corrected, real Python orchestration layer.

**Architecture:** C++ (`audacity/audacity`, `src/mcp/`) gets 4 generic, DSP-free commands: `apply-effect`, `select-all`, `select-time`, `export-wav`. Python (`Audacity4MCP`, `server4/`) owns all pipeline/genre intelligence, ported from the real v3 source. The existing `server4/bridge_client.py` is dead code (wrong port, wrong protocol) and must be fixed first, or nothing downstream is testable.

**Tech Stack:** C++/Qt6 (muse framework, `au::mcp`, `au::effects`, `au::trackedit`, `au::importexport`), Python 3.10+ (FastMCP, asyncio), GTest/GMock, pytest.

**Spec:** `docs/superpowers/specs/2026-09-04-audio-cleanup-pipelines-design.md`

## Global Constraints

- C++ diff stays inside `src/mcp/` - no changes to `src/effects/`, `src/importexport/`, `src/trackedit/` internals themselves (their interfaces are consumed, not modified). This is the scope discipline the session has held throughout (upstream PR viability).
- Every C++ handler that touches project state starts with the existing `hasOpenProject()` guard (`src/mcp/internal/audacitycommandscontroller.cpp:328`) - a real crash was fixed by this pattern earlier this session, do not skip it.
- Do NOT commit anything in either repo without explicit real-time user approval, even though task steps below show `git commit` as the normal writing-plans convention. The user has not blanket-approved commits this session.
- Every new C++ command and every ported Python pipeline gets a real live test against a running Audacity4 instance before being considered done - unit tests alone are not sufficient, per this session's established rigor (multiple real, previously-unknown bugs were only found this way).
- v3's parameter names do **NOT** reliably transfer to v4's rewritten effects - confirmed during planning research (Compressor, Limiter, and NoiseReduction all use different field names/models in v4). Every pipeline task below uses parameter names and defaults read directly from v4's source (`au3/libraries/au3-dynamic-range-processor/DynamicRangeProcessorTypes.h` and the effect headers), not v3's.

---

## Repo A: `audacity/audacity` (C++, `D:\DansProject\Audacity4-Dev`)

### Task 1: `apply-effect` command

**Files:**
- Modify: `src/mcp/internal/audacitycommandscontroller.h`
- Modify: `src/mcp/internal/audacitycommandscontroller.cpp`
- Modify: `src/mcp/internal/audacitycommandsregister.cpp`
- Test: `src/mcp/tests/audacitycommandscontroller_tests.cpp`

**Interfaces:**
- Consumes: `au::effects::IEffectExecutionScenario::performEffect(const muse::String& effectId)` and `performEffect(const muse::String& effectId, const std::string& params)` (`src/effects/effects_base/ieffectexecutionscenario.h:23-24`) - both already exist and are already used in production (`effectsactionscontroller.cpp`). `params` is a `Key=Value Key2="quoted value"` string, shell-tokenized then split on first `=` (confirmed in `au3/libraries/au3-components/EffectAutomationParameters.h:283-299`).
- Produces: `command://mcp/apply-effect`, args `effect_id` (string, required - an effect's title, e.g. `"Compressor"`, `"Normalize"`, `"Noise reduction"`, `"Limiter"`, `"Loudness Normalization"` - `performEffect` falls back to matching by title if the id doesn't match directly, per `effectexecutionscenario.cpp:56-79`) and `params` (string, optional).

- [ ] **Step 1: Add the injected dependency and handler declaration**

In `src/mcp/internal/audacitycommandscontroller.h`, add the include and inject:

```cpp
#include "effects/effects_base/ieffectexecutionscenario.h"
```

Add alongside the existing `ContextInject` members:

```cpp
muse::ContextInject<au::effects::IEffectExecutionScenario> effectExecutionScenario { this };
```

Add alongside the existing handler declarations:

```cpp
muse::rcommand::Response handleApplyEffect(const muse::rcommand::Request& request);
```

- [ ] **Step 2: Implement the handler**

In `src/mcp/internal/audacitycommandscontroller.cpp`, add:

```cpp
Response AudacityCommandsController::handleApplyEffect(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }

    if (!effectExecutionScenario()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("IEffectExecutionScenario not available")));
    }

    std::string effectId = request.query.param("effect_id").toString();
    if (effectId.empty()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Missing required 'effect_id' argument")));
    }

    std::string params = request.query.param("params").toString();

    muse::Ret ret = params.empty()
                     ? effectExecutionScenario()->performEffect(String::fromStdString(effectId))
                     : effectExecutionScenario()->performEffect(String::fromStdString(effectId), params);

    return make_response(request, ret);
}
```

- [ ] **Step 3: Register the command**

In `AudacityCommandsController::init()`, add:

```cpp
registerCommand(Command("command://mcp/apply-effect"), [this](const Request& request) {
    return handleApplyEffect(request);
});
```

In `src/mcp/internal/audacitycommandsregister.cpp`, add to `s_commandInfos`:

```cpp
CommandInfo{
    Command("command://mcp/apply-effect"),
    TranslatableString("mcp", "Apply effect"),
    TranslatableString("mcp", "Apply a built-in effect to the current selection by name, with optional parameters"),
    []() {
        InputSchema schema;
        schema.args["effect_id"] = Arg(DataType::String, u"The effect's title (e.g. \"Compressor\", \"Normalize\", \"Noise reduction\", \"Limiter\", \"Loudness Normalization\")");
        schema.args["params"] = Arg(DataType::String, u"Optional automation parameters as \"Key=Value Key2=\\\"quoted value\\\"\" pairs");
        return schema;
    }(),
    Decoration()
},
```

- [ ] **Step 4: Update the "how many commands" unit tests**

In `src/mcp/tests/audacitycommandscontroller_tests.cpp`, change `RegistersAllTwelveCommands`'s expected count from 12 to 13 (rename the test to `RegistersAllThirteenCommands` for clarity). Same in `src/mcp/tests/audacitycommandsregister_tests.cpp`'s `CommandListHasTwelveEntries` → 13 entries, renamed `CommandListHasThirteenEntries`.

- [ ] **Step 5: Add the "no project open" regression test**

In `audacitycommandscontroller_tests.cpp`, following the exact pattern of `AddLabelTrackReportsErrorWhenNoProjectIsOpen`:

```cpp
TEST_F(AudacityCommandsControllerTests, ApplyEffectReportsErrorWhenNoProjectIsOpen)
{
    ON_CALL(*m_globalContext, currentProject()).WillByDefault(Return(project::IAudacityProjectPtr()));

    Request req = make_request(CommandQuery(Command("command://mcp/apply-effect")).set("effect_id", "Normalize"));
    auto it = m_registered.find(Command("command://mcp/apply-effect"));
    ASSERT_NE(it, m_registered.end());
    Response response = it->second(req);

    EXPECT_FALSE(response.ret.success());
    EXPECT_NE(response.ret.text().find("No project"), std::string::npos);
}
```

This requires wiring an `EffectExecutionScenarioMock` into the test fixture's `SetUp()` if one doesn't already exist under `src/effects/effects_base/tests/mocks/` - check first; if missing, add a minimal GMock following the exact pattern of `CommandDispatcherMock` (`src/mcp/tests/mocks/commanddispatchermock.h`), mocking just `performEffect` (both overloads).

- [ ] **Step 6: Build and run the C++ unit tests**

```powershell
cmd.exe /c '"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && cd /d D:\DansProject\Audacity4-Dev\build\audacity-debug && cmake --build . --target mcp_tests -- -j 8'
```

Then run with Qt DLLs on PATH (established this session):

```powershell
$env:PATH = "D:\DansProject\Audacity4-Dev\src\app\bin;" + $env:PATH
$env:QT_PLUGIN_PATH = "D:\DansProject\Audacity4-Dev\src\app\bin"
& "D:\DansProject\Audacity4-Dev\build\audacity-debug\mcp_tests.exe" --gtest_filter="*AudacityCommandsControllerTests*:*AudacityCommandsRegisterTests*"
```

Expected: all tests pass, including the new `ApplyEffectReportsErrorWhenNoProjectIsOpen`.

- [ ] **Step 7: Commit** (only after explicit user go-ahead)

```bash
git add src/mcp/
git commit -m "feat(mcp): add apply-effect command for invoking built-in effects by name"
```

---

### Task 2: `select-all` and `select-time` commands

**Files:**
- Modify: `src/mcp/internal/audacitycommandscontroller.h`
- Modify: `src/mcp/internal/audacitycommandscontroller.cpp`
- Modify: `src/mcp/internal/audacitycommandsregister.cpp`
- Test: `src/mcp/tests/audacitycommandscontroller_tests.cpp`

**Interfaces:**
- Consumes: `ISelectionController::setSelectedTracks(const TrackIdList&, bool complete = true)`, `setSelectedAllAudioData(const std::optional<secs_t>& fromTime = std::nullopt, const std::optional<secs_t>& toTime = std::nullopt)` (both already declared in `src/trackedit/iselectioncontroller.h:33,82-83`, already injected in the controller as `selectionController`). Reuses `findFirstLabelTrack()`'s pattern for walking `trackeditProject->trackList()` to build a full `TrackIdList`.
- Produces: `command://mcp/select-all` (no args), `command://mcp/select-time` (args `start`, `end` - numbers, seconds).

- [ ] **Step 1: Add handler declarations**

In `audacitycommandscontroller.h`:

```cpp
muse::rcommand::Response handleSelectAll(const muse::rcommand::Request& request);
muse::rcommand::Response handleSelectTime(const muse::rcommand::Request& request);
```

- [ ] **Step 2: Implement the handlers**

```cpp
Response AudacityCommandsController::handleSelectAll(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!selectionController()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("ISelectionController not available")));
    }

    project::IAudacityProjectPtr project = globalContext()->currentProject();
    au::trackedit::ITrackeditProjectPtr trackeditProject = project->trackeditProject();
    if (!trackeditProject) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No trackedit project available")));
    }

    au::trackedit::TrackIdList allTracks;
    for (const au::trackedit::Track& track : trackeditProject->trackList()) {
        allTracks.push_back(track.id);
    }

    selectionController()->setSelectedTracks(allTracks);
    selectionController()->setSelectedAllAudioData();

    return make_response(request, make_ret(Ret::Code::Ok, std::string("Selected all tracks and all audio data")));
}

Response AudacityCommandsController::handleSelectTime(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!selectionController()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("ISelectionController not available")));
    }

    bool startOk = false;
    bool endOk = false;
    double start = request.query.param("start").toDouble(&startOk);
    double end = request.query.param("end").toDouble(&endOk);
    if (!startOk || !endOk) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("Missing or invalid 'start'/'end' arguments (seconds)")));
    }

    selectionController()->setSelectedAllAudioData(start, end);

    std::string message = "Selected time range " + std::to_string(start) + "-" + std::to_string(end) + "s";
    return make_response(request, make_ret(Ret::Code::Ok, message));
}
```

**NOTE on Step 2:** `request.query.param(...).toDouble(&ok)` mirrors `muse::Val`'s existing conversion API used elsewhere in this file (e.g. `.toString()`) - confirm the exact overload during implementation (check `framework/global/types/val.h`); if `toDouble()` doesn't take an out-param, use `.toDouble()` directly and validate the raw string isn't empty first via `request.query.param("start").toString().empty()`.

- [ ] **Step 3: Register both commands**

In `init()`:

```cpp
registerCommand(Command("command://mcp/select-all"), [this](const Request& request) {
    return handleSelectAll(request);
});
registerCommand(Command("command://mcp/select-time"), [this](const Request& request) {
    return handleSelectTime(request);
});
```

In `audacitycommandsregister.cpp`, add two `CommandInfo` entries (`select-all` with `InputSchema()`; `select-time` with `start`/`end` `DataType::Number` args), following the exact structure of the existing `add-label`/`update-label-text` entries.

- [ ] **Step 4: Update command-count tests**

Bump the expected count from 13 (Task 1) to 15 in both `audacitycommandscontroller_tests.cpp` and `audacitycommandsregister_tests.cpp`.

- [ ] **Step 5: Add "no project open" regression tests for both commands**

Same pattern as Task 1 Step 5, one test per command (`SelectAllReportsErrorWhenNoProjectIsOpen`, `SelectTimeReportsErrorWhenNoProjectIsOpen`).

- [ ] **Step 6: Build and run unit tests** (same commands as Task 1 Step 6)

- [ ] **Step 7: Commit** (only after explicit user go-ahead)

---

### Task 3: `export-wav` command

**Files:**
- Modify: `src/mcp/internal/audacitycommandscontroller.h`
- Modify: `src/mcp/internal/audacitycommandscontroller.cpp`
- Modify: `src/mcp/internal/audacitycommandsregister.cpp`
- Test: `src/mcp/tests/audacitycommandscontroller_tests.cpp`

**Interfaces:**
- Consumes: `au::importexport::IExporter::exportData(const muse::io::path_t& path, const Options& options, muse::ProgressPtr progress, IAudacityProjectPtr project)` (`src/importexport/export/iexporter.h:43-44`). Confirmed format string for WAV is exactly `"WAV (Microsoft)"` (`au3/modules/import-export/mod-pcm/ExportPCM.cpp:61`, matched via `.description.msgid()` in `Au3Exporter::formatIndex`/`formatPlugin`). `ExportChannelsPref::ExportChannels::MONO = 1` (`src/importexport/export/types/exporttypes.h:25`). `ExportProcessType::SELECTED_AUDIO` (`exporttypes.h:13`) makes the export respect whatever `select-all`/`select-time` set up.
- Produces: `command://mcp/export-wav`, args `path` (string, required).

- [ ] **Step 1: Add the injected dependency and handler declaration**

In `audacitycommandscontroller.h`:

```cpp
#include "importexport/export/iexporter.h"
```

```cpp
muse::ContextInject<au::importexport::IExporter> exporter { this };
```

```cpp
muse::rcommand::Response handleExportWav(const muse::rcommand::Request& request);
```

- [ ] **Step 2: Implement the handler**

```cpp
Response AudacityCommandsController::handleExportWav(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!exporter()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("IExporter not available")));
    }

    std::string path = request.query.param("path").toString();
    if (path.empty()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Missing required 'path' argument")));
    }

    au::importexport::IExporter::Options options;
    options[au::importexport::IExporter::OptionKey::Format] = muse::Val(std::string("WAV (Microsoft)"));
    options[au::importexport::IExporter::OptionKey::ProcessType]
        = muse::Val(static_cast<int>(au::importexport::ExportProcessType::SELECTED_AUDIO));
    options[au::importexport::IExporter::OptionKey::ExportChannelsType]
        = muse::Val(static_cast<int>(au::importexport::ExportChannelsPref::ExportChannels::MONO));

    muse::Ret ret = exporter()->exportData(muse::io::path_t(path), options);
    return make_response(request, ret);
}
```

**NOTE on Step 2:** `OptionKey::ProcessType`'s value is read via `.toEnum<ExportProcessType>()` in `au3exporter.cpp:162` - confirm `muse::Val` construction from an enum works the same way as the `int` cast above during implementation; adjust to whatever `toEnum`'s companion constructor expects if it differs.

- [ ] **Step 3: Register the command**

```cpp
registerCommand(Command("command://mcp/export-wav"), [this](const Request& request) {
    return handleExportWav(request);
});
```

Add matching `CommandInfo` in `audacitycommandsregister.cpp` with a `path` string arg.

- [ ] **Step 4: Update command-count tests** (13+2 from Task 2 = 15, +1 = 16)

- [ ] **Step 5: Add "no project open" regression test** (same pattern)

- [ ] **Step 6: Build and run unit tests**

- [ ] **Step 7: Commit** (only after explicit user go-ahead)

---

### Task 4: Live verification of all 4 new C++ commands

**Files:** none (verification only)

**Interfaces:**
- Consumes: all handlers from Tasks 1-3, live over the MCP TCP port (2212).

- [ ] **Step 1: Build the full app**

```powershell
Get-Process Audacity4 -ErrorAction SilentlyContinue | Stop-Process -Force
Remove-Item "D:\DansProject\Audacity4-Dev\src\app\bin\Audacity4.exe","D:\DansProject\Audacity4-Dev\src\app\bin\Audacity4.pdb","D:\DansProject\Audacity4-Dev\build\audacity-debug\Audacity4-Debug.ilk","D:\DansProject\Audacity4-Dev\build\audacity-debug\src\app\Audacity4.lib" -Force -ErrorAction SilentlyContinue
cmd.exe /c '"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && cd /d D:\DansProject\Audacity4-Dev\build\audacity-debug && cmake --build . --target audacity -- -j 8'
```

- [ ] **Step 2: Launch, open a real project with an actual audio clip loaded** (not blank - `apply-effect`/`export-wav` need real audio data to act on)

- [ ] **Step 3: Live-test `select-all` + `apply-effect` with a harmless effect**

Using the raw-TCP JSON-RPC pattern established this session:

```powershell
$client = New-Object System.Net.Sockets.TcpClient('127.0.0.1', 2212)
$stream = $client.GetStream()
$writer = New-Object System.IO.StreamWriter($stream); $writer.AutoFlush = $true
$reader = New-Object System.IO.StreamReader($stream)
function Send($id, $name, $argsObj) {
    $msg = @{ jsonrpc = "2.0"; id = $id; method = "tools/call"; params = @{ name = $name; arguments = $argsObj } } | ConvertTo-Json -Compress -Depth 6
    $writer.WriteLine($msg); Start-Sleep -Milliseconds 700
    return $reader.ReadLine()
}
Send 1 'mcp_select-all' @{}
Send 2 'mcp_apply-effect' @{effect_id='Normalize'; params='PeakLevel=-3.0 RemoveDcOffset=True ApplyVolume=True StereoIndependent=False'}
```

Expected: success response, app still responding afterward (`Get-Process Audacity4 | Select Responding`), no new crash dump in `%LOCALAPPDATA%\CrashDumps`. **This is the step that confirms `performEffect`'s title-fallback resolution actually works for a real effect name** - if it fails, read `effectexecutionscenario.cpp`'s title-matching branch (only partially read during planning) to find the exact match semantics before proceeding to any other task.

- [ ] **Step 4: Live-test `select-time`**

```powershell
Send 3 'mcp_select-time' @{start=0; end=2.5}
```

Confirm (screenshot or `list-labels`-style follow-up isn't available for time selection - check the Selection field at the bottom of the Audacity window via screenshot, as done earlier this session for label positions) that the selection actually changed to 0-2.5s.

- [ ] **Step 5: Live-test `export-wav`**

```powershell
Send 4 'mcp_export-wav' @{path='C:\Users\GGPC\AppData\Local\Temp\claude\D--DansProject-Audacity4MCP\test_export.wav'}
```

Then confirm the file exists and is non-trivial:

```powershell
Get-Item 'C:\Users\GGPC\AppData\Local\Temp\claude\D--DansProject-Audacity4MCP\test_export.wav' | Select Length
```

Expected: file exists, size > a few KB (not an empty/failed export).

- [ ] **Step 6: Report findings**

If any step reveals a real bug (crash, wrong behavior, wrong parameter format), fix it in the relevant Task 1-3 files and re-verify before moving to Repo B tasks - do not proceed with a known-broken primitive underneath the Python layer.

---

## Repo B: `Audacity4MCP` (Python, `D:\DansProject\Audacity4MCP`)

**Note:** This repo has no git history (`git status` fails with "not a git repository"). If the user wants version control here, that's a separate decision - do not run `git init` without asking, since it's a repo-shaping decision outside this plan's scope.

### Task 5: Fix the MCP bridge client to speak the real protocol

**Files:**
- Modify: `server4/bridge_client.py` (complete rewrite of the transport - class name/shape can stay similar)
- Modify: `server4/tool_registry.py` (fix broken import)
- Modify: `server4/tools/label_tools.py` (rewrite to call real commands - currently 100% non-functional)
- Test: `tests/test_bridge_client.py` (new)

**Interfaces:**
- Consumes: the real C++ MCP server's wire protocol - TCP port 2212, line-delimited JSON-RPC 2.0, `{"jsonrpc":"2.0","id":<n>,"method":"tools/call","params":{"name":"mcp_<command-name-with-dashes>","arguments":{...}}}`, confirmed working throughout this session via raw `System.Net.Sockets.TcpClient` calls. Response shape: `{"id":<n>,"jsonrpc":"2.0","result":{"content":[{"text":"...","type":"text"}, ...],"isError":<bool>}}`.
- Produces: `BridgeClient.call(command_name: str, arguments: dict) -> dict` - returns the parsed `result` object; raises on `isError: true` or a transport failure. `BridgeClient.connect()`/`close()` as before.

- [ ] **Step 1: Write the failing test**

```python
# tests/test_bridge_client.py
import asyncio
import json
import pytest
from server4.bridge_client import BridgeClient


class _FakeServer:
    """Minimal asyncio TCP server that echoes back a canned MCP response."""
    def __init__(self, response: dict):
        self.response = response
        self.received = None

    async def _handle(self, reader, writer):
        line = await reader.readline()
        self.received = json.loads(line)
        writer.write((json.dumps(self.response) + "\n").encode())
        await writer.drain()
        writer.close()

    async def start(self):
        self.server = await asyncio.start_server(self._handle, "127.0.0.1", 0)
        return self.server.sockets[0].getsockname()[1]

    async def stop(self):
        self.server.close()
        await self.server.wait_closed()


@pytest.mark.asyncio
async def test_call_sends_correct_jsonrpc_shape_and_parses_result():
    fake = _FakeServer({
        "id": 1, "jsonrpc": "2.0",
        "result": {"content": [{"text": "ok", "type": "text"}], "isError": False},
    })
    port = await fake.start()
    client = BridgeClient(host="127.0.0.1", port=port)

    result = await client.call("play-stop", {})

    assert fake.received["method"] == "tools/call"
    assert fake.received["params"]["name"] == "mcp_play-stop"
    assert fake.received["params"]["arguments"] == {}
    assert result["content"][0]["text"] == "ok"

    await client.close()
    await fake.stop()


@pytest.mark.asyncio
async def test_call_raises_on_error_response():
    fake = _FakeServer({
        "id": 1, "jsonrpc": "2.0",
        "result": {"content": [{"text": "No project is currently open", "type": "text"}], "isError": True},
    })
    port = await fake.start()
    client = BridgeClient(host="127.0.0.1", port=port)

    with pytest.raises(RuntimeError, match="No project is currently open"):
        await client.call("add-label-track", {})

    await client.close()
    await fake.stop()
```

- [ ] **Step 2: Run to verify it fails**

```bash
cd /d/DansProject/Audacity4MCP && python -m pytest tests/test_bridge_client.py -v
```

Expected: FAIL - `BridgeClient` has no `call` method yet (or `ModuleNotFoundError` if `bridge_client.py` still only has the old WebSocket shape).

- [ ] **Step 3: Rewrite `bridge_client.py`**

```python
import asyncio
import json
from typing import Any


class BridgeClient:
    """TCP JSON-RPC client for Audacity 4's built-in MCP server
    (muse::rcontrol::mcp, listening on port 2212 by default)."""

    def __init__(self, host: str = "127.0.0.1", port: int = 2212):
        self._host = host
        self._port = port
        self._reader: asyncio.StreamReader | None = None
        self._writer: asyncio.StreamWriter | None = None
        self._id = 0
        self._lock = asyncio.Lock()

    async def connect(self) -> None:
        if self._writer is None or self._writer.is_closing():
            self._reader, self._writer = await asyncio.open_connection(self._host, self._port)

    async def close(self) -> None:
        if self._writer:
            self._writer.close()
            await self._writer.wait_closed()
            self._writer = None
            self._reader = None

    async def call(self, command_name: str, arguments: dict[str, Any]) -> dict:
        """Call an mcp/<command_name> command. Returns the parsed `result` object.
        Raises RuntimeError if the response reports isError: true, or on a transport
        problem (connection refused, timeout, malformed response)."""
        async with self._lock:
            await self.connect()
            self._id += 1
            msg = {
                "jsonrpc": "2.0",
                "id": self._id,
                "method": "tools/call",
                "params": {"name": f"mcp_{command_name}", "arguments": arguments},
            }
            self._writer.write((json.dumps(msg) + "\n").encode())
            await self._writer.drain()

            raw = await asyncio.wait_for(self._reader.readline(), timeout=30.0)
            if not raw:
                raise RuntimeError(f"Connection closed while waiting for response to {command_name}")
            resp = json.loads(raw)

            result = resp.get("result", {})
            if result.get("isError"):
                text = "; ".join(c.get("text", "") for c in result.get("content", []))
                raise RuntimeError(text or f"Command {command_name} failed with no error text")
            return result
```

- [ ] **Step 4: Run to verify it passes**

```bash
python -m pytest tests/test_bridge_client.py -v
```

Expected: PASS.

- [ ] **Step 5: Fix `tool_registry.py`'s broken import**

```python
# server4/tool_registry.py
import importlib
import pkgutil
from mcp.server.fastmcp import FastMCP  # was: mcp.server4.fastmcp (does not exist)

import server4.tools as tools_package


def register_all_tools(mcp: FastMCP):
    for finder, name, ispkg in pkgutil.iter_modules(tools_package.__path__):
        module = importlib.import_module(f"server4.tools.{name}")
        if hasattr(module, "register"):
            module.register(mcp)
```

- [ ] **Step 6: Update `main.py` for the new `bridge.call()` shape**

`server4/main.py` currently does `bridge = BridgeClient()` with no args - the new constructor's defaults (`127.0.0.1`, `2212`) already match, so no change needed there. Confirm this by reading the file again after Step 3's rewrite.

- [ ] **Step 7: Rewrite `label_tools.py` to call the real label commands**

The old file dispatched non-existent actions (`"label-add"`, `"label-cut"`, etc.). Replace with the 5 real commands built earlier this session:

```python
from mcp.server.fastmcp import FastMCP


def register(mcp: FastMCP):
    from server4.main import bridge

    @mcp.tool()
    async def label_list() -> dict:
        """List all labels on the project's (first) label track, with key, text, and time range."""
        return await bridge.call("list-labels", {})

    @mcp.tool()
    async def label_add_track() -> dict:
        """Create a new, empty label track."""
        return await bridge.call("add-label-track", {})

    @mcp.tool()
    async def label_add(text: str = "") -> dict:
        """Add a label at the current selection/playback position. Creates a label
        track automatically if none exists yet.

        Args:
            text: Optional text for the new label.
        """
        return await bridge.call("add-label", {"text": text} if text else {})

    @mcp.tool()
    async def label_remove(key: str) -> dict:
        """Remove a label by its key.

        Args:
            key: The label's key, in "trackId:itemId" format (from label_list/label_add).
        """
        return await bridge.call("remove-label", {"key": key})

    @mcp.tool()
    async def label_update_text(key: str, text: str) -> dict:
        """Change the text of an existing label.

        Args:
            key: The label's key, in "trackId:itemId" format.
            text: The new text for the label.
        """
        return await bridge.call("update-label-text", {"key": key, "text": text})
```

- [ ] **Step 8: Live smoke test against the real running Audacity4 instance**

With Audacity4 running (from Task 4) and a project open:

```bash
cd /d/DansProject/Audacity4MCP
python -c "
import asyncio
from server4.bridge_client import BridgeClient

async def main():
    client = BridgeClient()
    result = await client.call('list-labels', {})
    print(result)
    await client.close()

asyncio.run(main())
"
```

Expected: a real result back from the running app (not a connection error), matching what raw-TCP testing has shown all session.

- [ ] **Step 9: Commit** (only after explicit user go-ahead)

```bash
git add -A
git commit -m "fix: replace dead WebSocket bridge with real TCP/JSON-RPC MCP client"
```

(Only if the user has separately approved `git init` for this repo - otherwise skip and just leave the files saved.)

---

### Task 6: `analysis_tools.py` - port `auto_analyze_audio`

**Files:**
- Create: `server4/tools/analysis_tools.py`
- Test: `tests/test_analysis_tools.py`

**Interfaces:**
- Consumes: `bridge.call("select-all", {})`, `bridge.call("export-wav", {"path": ...})` (from Task 5's rewritten `BridgeClient`, Task 2/3's new C++ commands).
- Produces: MCP tool `auto_analyze_audio() -> dict` with the same measurement fields as v3's version (`peak_db`, `noise_floor_db`, `overall_rms_db`, `dc_offset`, `clipped_samples`, `click_count`, `silence_gaps`, `dynamic_range_db`, `issues`, `recommendation`).

- [ ] **Step 1: Write the failing test for the pure-Python measurement function**

The measurement logic (`_measure_wav`) is pure stdlib and independently testable without a live Audacity instance - test it directly with a synthetic WAV file:

```python
# tests/test_analysis_tools.py
import struct
import wave
import pytest
from server4.tools.analysis_tools import _measure_wav


def _write_test_wav(path, samples, rate=44100):
    with wave.open(str(path), "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(rate)
        wf.writeframes(struct.pack(f"<{len(samples)}h", *samples))


def test_measure_wav_detects_peak_level(tmp_path):
    path = tmp_path / "test.wav"
    # Half-scale peak (16384 / 32768 = -6.02 dB)
    _write_test_wav(path, [16384, -16384] * 1000)

    result = _measure_wav(str(path))

    assert result is not None
    assert -6.5 < result["peak_db"] < -5.5


def test_measure_wav_returns_none_for_missing_file():
    result = _measure_wav("does_not_exist.wav")
    assert result is None
```

- [ ] **Step 2: Run to verify it fails**

```bash
python -m pytest tests/test_analysis_tools.py -v
```

Expected: FAIL - `server4.tools.analysis_tools` doesn't exist yet.

- [ ] **Step 3: Create `analysis_tools.py`**

Port `_measure_wav` and `auto_analyze_audio` from `D:\DansProject\AudacityMCP\audacity_mcp\tools\cleanup_tools.py` (lines 616-995) essentially verbatim - it is pure stdlib (`wave`, `struct`, `math`) with zero v3-specific dependencies. Two changes from the v3 source:
1. Replace the `Export2` mod-script-pipe call with `bridge.call("select-all", {})` then `bridge.call("export-wav", {"path": tmp_wav})`.
2. Replace the `GetInfo Type=Tracks` call (for track metadata) with whatever v4 command already returns track info - check `server4/tools/track_tools.py` for an existing "list tracks" tool first; if none exists, drop the per-track breakdown from the result (it's supplementary - `tracks`/`track_count` fields) rather than adding a new C++ command for it, since that's out of this plan's scope.

```python
import math
import os
import struct
import tempfile
import uuid
import wave

from mcp.server.fastmcp import FastMCP


def _temp_wav_path() -> str:
    return os.path.join(tempfile.gettempdir(), f"audacity_mcp_analyze_{uuid.uuid4().hex[:8]}.wav")


def _measure_wav(wav_path: str) -> dict | None:
    """Read a WAV file and compute audio diagnostics. Returns None on failure.
    Ported near-verbatim from v3's AudacityMCP/audacity_mcp/tools/cleanup_tools.py."""
    try:
        with wave.open(wav_path, "rb") as wf:
            rate = wf.getframerate()
            n_frames = wf.getnframes()
            sw = wf.getsampwidth()
            if n_frames == 0:
                return None

            if sw == 2:
                fmt_char = "h"
                max_val = 32768.0
            elif sw == 4:
                fmt_char = "f"
                max_val = 1.0
            else:
                return None

            duration = round(n_frames / rate, 2)
            chunk_size = rate

            peak_abs = 0.0
            noise_sum_sq = 0.0
            noise_count = 0
            noise_target = min(int(rate * 0.5), n_frames)
            total_sum = 0.0
            total_sum_sq = 0.0
            total_count = 0
            clipped_samples = 0
            clip_threshold = max_val * 0.999

            click_count = 0
            click_threshold = max_val * 0.3
            prev_sample = 0.0

            silence_threshold = max_val * 0.001
            min_gap_samples = int(rate * 0.5)
            current_silence_run = 0
            silence_gaps = []

            second_rms_values = []
            second_sum_sq = 0.0
            second_count = 0

            frames_read = 0
            while frames_read < n_frames:
                n = min(chunk_size, n_frames - frames_read)
                raw = wf.readframes(n)
                if len(raw) < n * sw:
                    break
                samples = struct.unpack(f"<{n}{fmt_char}", raw)

                for s in samples:
                    a = abs(s)
                    if a > peak_abs:
                        peak_abs = a
                    if a >= clip_threshold:
                        clipped_samples += 1
                    total_sum += s
                    total_sum_sq += s * s
                    total_count += 1
                    delta = abs(s - prev_sample)
                    if delta > click_threshold and total_count > 1:
                        click_count += 1
                    prev_sample = s
                    if a < silence_threshold:
                        current_silence_run += 1
                    else:
                        if current_silence_run >= min_gap_samples:
                            gap_start = (frames_read + total_count - current_silence_run) / rate
                            gap_dur = current_silence_run / rate
                            silence_gaps.append((round(max(gap_start, 0), 2), round(gap_dur, 2)))
                        current_silence_run = 0
                    second_sum_sq += s * s
                    second_count += 1
                    if second_count >= rate:
                        rms = math.sqrt(second_sum_sq / second_count) / max_val
                        if rms > 1e-10:
                            second_rms_values.append(20 * math.log10(rms))
                        second_sum_sq = 0.0
                        second_count = 0

                if noise_count < noise_target:
                    take = min(n, noise_target - noise_count)
                    for s in samples[:take]:
                        noise_sum_sq += s * s
                    noise_count += take

                frames_read += n

            if current_silence_run >= min_gap_samples:
                gap_start = (n_frames - current_silence_run) / rate
                silence_gaps.append((round(max(gap_start, 0), 2), round(current_silence_run / rate, 2)))

            if second_count > rate * 0.1:
                rms = math.sqrt(second_sum_sq / second_count) / max_val
                if rms > 1e-10:
                    second_rms_values.append(20 * math.log10(rms))

            peak_linear = peak_abs / max_val
            peak_db = round(20 * math.log10(max(peak_linear, 1e-10)), 1)

            noise_db = None
            if noise_count > 0:
                rms_linear = math.sqrt(noise_sum_sq / noise_count) / max_val
                noise_db = round(20 * math.log10(max(rms_linear, 1e-10)), 1)

            overall_rms_db = None
            if total_count > 0:
                overall_rms = math.sqrt(total_sum_sq / total_count) / max_val
                overall_rms_db = round(20 * math.log10(max(overall_rms, 1e-10)), 1)

            dc_offset = round((total_sum / total_count) / max_val, 6) if total_count > 0 else 0.0

            dynamic_range_db = None
            if len(second_rms_values) >= 2:
                dynamic_range_db = round(max(second_rms_values) - min(second_rms_values), 1)

            return {
                "peak_db": peak_db,
                "noise_floor_db": noise_db,
                "overall_rms_db": overall_rms_db,
                "dc_offset": dc_offset,
                "duration": duration,
                "sample_rate": rate,
                "clipped_samples": clipped_samples,
                "click_count": click_count,
                "silence_gaps": silence_gaps[:10],
                "silence_gap_count": len(silence_gaps),
                "dynamic_range_db": dynamic_range_db,
            }
    except Exception:
        return None


def register(mcp: FastMCP):
    from server4.main import bridge

    @mcp.tool()
    async def auto_analyze_audio() -> dict:
        """Analyze the current project's audio and recommend a cleanup pipeline.
        Selects all audio first, exports it to a temp WAV, measures it, and returns
        peak/noise/clipping/click/silence-gap/dynamic-range diagnostics plus a
        recommendation for which pipeline to run next.
        """
        await bridge.call("select-all", {})

        tmp_wav = _temp_wav_path()
        measurement_error = None
        measurements = None
        try:
            await bridge.call("export-wav", {"path": tmp_wav})
            if not os.path.exists(tmp_wav):
                measurement_error = f"export-wav reported success but no file was created at {tmp_wav}"
            else:
                file_size = os.path.getsize(tmp_wav)
                if file_size < 100:
                    measurement_error = f"Exported WAV is too small ({file_size} bytes) - export may have failed"
                else:
                    measurements = _measure_wav(tmp_wav)
                    if measurements is None:
                        measurement_error = f"WAV file exists ({file_size} bytes) but could not be parsed"
        except Exception as e:
            measurement_error = f"export-wav failed: {type(e).__name__}: {e}"
        finally:
            try:
                os.remove(tmp_wav)
            except OSError:
                pass

        peak_db = measurements["peak_db"] if measurements else None
        noise_floor_db = measurements["noise_floor_db"] if measurements else None
        overall_rms_db = measurements["overall_rms_db"] if measurements else None
        dc_offset = measurements["dc_offset"] if measurements else None
        clipped_samples = measurements["clipped_samples"] if measurements else 0
        click_count = measurements["click_count"] if measurements else 0
        silence_gaps = measurements["silence_gaps"] if measurements else []
        silence_gap_count = measurements["silence_gap_count"] if measurements else 0
        dynamic_range_db = measurements["dynamic_range_db"] if measurements else None
        duration = measurements["duration"] if measurements else None

        is_clipping = peak_db is not None and peak_db >= -0.1

        issues = []
        if peak_db is not None:
            if peak_db < -30:
                issues.append(f"VERY QUIET: Peak is only {peak_db} dB. Run normalize (to -3 dB) first.")
            elif peak_db < -20:
                issues.append(f"QUIET: Peak is {peak_db} dB - below normal levels.")
            elif peak_db < -12:
                issues.append(f"LOW VOLUME: Peak is {peak_db} dB - slightly quiet but workable.")
            if is_clipping:
                issues.append(f"CLIPPING: Peak is {peak_db} dB with {clipped_samples} clipped samples.")

        if noise_floor_db is not None and peak_db is not None:
            snr = peak_db - noise_floor_db
            if snr < 15:
                issues.append(f"VERY NOISY: SNR is only {round(snr, 1)} dB.")
            elif snr < 20:
                issues.append(f"NOISY: SNR is {round(snr, 1)} dB.")
            if noise_floor_db > -30:
                issues.append(f"HIGH NOISE FLOOR: {noise_floor_db} dB - needs noise reduction.")

        if dc_offset is not None and abs(dc_offset) > 0.005:
            issues.append(f"DC OFFSET: {dc_offset} - will be removed by pipeline.")

        if click_count > 50:
            issues.append(f"LOTS OF CLICKS/POPS: {click_count} detected.")
        elif click_count > 10:
            issues.append(f"SOME CLICKS/POPS: {click_count} detected.")

        if silence_gap_count > 0:
            total_silence = sum(g[1] for g in silence_gaps)
            if silence_gap_count > 5:
                issues.append(f"MANY GAPS: {silence_gap_count} gaps totalling {round(total_silence, 1)}s.")

        if dynamic_range_db is not None:
            if dynamic_range_db > 40:
                issues.append(f"EXTREME DYNAMIC RANGE: {dynamic_range_db} dB - compression strongly recommended.")
            elif dynamic_range_db > 25:
                issues.append(f"WIDE DYNAMIC RANGE: {dynamic_range_db} dB.")

        if overall_rms_db is not None and peak_db is not None:
            crest_factor = peak_db - overall_rms_db
            if crest_factor < 3 and peak_db > -6:
                issues.append(f"OVER-COMPRESSED: Crest factor only {round(crest_factor, 1)} dB.")

        if peak_db is not None:
            recommendation = ("ISSUES FOUND:\n" + "\n".join(f"  - {i}" for i in issues)
                               if issues else "Audio looks healthy - no issues detected.")
            recommendation += "\n\nChoose pipeline based on content type:\n  - Podcast/voiceover: auto_cleanup_podcast\n  - Audiobook (ACX): auto_audiobook_mastering"
        else:
            recommendation = "Could not measure audio levels.\n  - Podcast/voiceover: auto_cleanup_podcast\n  - Audiobook (ACX): auto_audiobook_mastering"

        result = {
            "peak_db": peak_db,
            "noise_floor_db": noise_floor_db,
            "overall_rms_db": overall_rms_db,
            "is_clipping": is_clipping,
            "clipped_samples": clipped_samples,
            "dc_offset": dc_offset,
            "click_pop_count": click_count,
            "silence_gaps": silence_gap_count,
            "dynamic_range_db": dynamic_range_db,
            "duration_seconds": duration,
            "issues": issues,
            "recommendation": recommendation,
        }
        if measurement_error:
            result["measurement_error"] = measurement_error
        return result
```

- [ ] **Step 4: Run to verify tests pass**

```bash
python -m pytest tests/test_analysis_tools.py -v
```

Expected: PASS.

- [ ] **Step 5: Live test against the running Audacity4 instance**

With a real (ideally slightly noisy) audio clip loaded:

```bash
python -c "
import asyncio
from server4.tools.analysis_tools import register
from server4.main import mcp
result = asyncio.run(mcp._tool_manager._tools['auto_analyze_audio'].fn())
print(result)
"
```

(Adjust invocation to however `server4/tests/test_tools.py`'s existing patterns call tools directly if that differs - check it first.) Expected: real peak/RMS/noise numbers back, not an exception - confirms the `select-all` → `export-wav` → `_measure_wav` chain actually works end-to-end against the live app.

- [ ] **Step 6: Commit** (only after explicit user go-ahead)

---

### Task 7: `effects_tools.py` rewrite - podcast/audiobook effect wrappers

**Files:**
- Modify: `server4/tools/effects_tools.py` (full rewrite - old content dispatches non-existent dialog-opening actions)
- Test: `tests/test_effects_tools.py`

**Interfaces:**
- Consumes: `bridge.call("apply-effect", {"effect_id": ..., "params": ...})` from Task 5.
- Produces: MCP tools `noise_reduction`, `normalize`, `compressor`, `limiter`, `loudness_normalize` - real parameter names below are read directly from v4's source, NOT v3's mod-script-pipe names (confirmed different during planning: `au3/libraries/au3-dynamic-range-processor/DynamicRangeProcessorTypes.h`, `src/effects/builtin_collection/{normalize,noisereduction,loudness}/*.h`).

**Real v4 parameter keys (source-confirmed, do not substitute v3's names):**
- Normalize (`"Normalize"`): `PeakLevel` (default -1.0, range -145..0), `RemoveDcOffset` (bool), `ApplyVolume` (bool - v3 called this `ApplyGain`), `StereoIndependent` (bool).
- Noise reduction (`"Noise reduction"`): `Sensitivity` (default per `NoiseReductionSettings::sensitivityDefault`, range 0.01..24), `"Noise Gain"` (space in the key! range 0..48 - replaces v3's `Reduction`), `"Frequency Smoothing Bands"` (space in the key! range 0..12 - replaces v3's `Smoothing`), `"Noise Reduction Choice"` (space in the key! enum 0..NRC_COUNT-1 - **verify the 0 value means "reduce noise" not "capture profile" during live testing in Task 8**, since this param didn't exist in v3's simpler two-call profile/apply flow).
- Compressor (`"Compressor"`): `thresholdDb` (default -12), `makeupGainDb` (default -3, i.e. threshold/ratio), `kneeWidthDb` (default 6), `compressionRatio` (default 4), `lookaheadMs` (default 3), `attackMs` (default 3), `releaseMs` (default 100).
- Limiter (`"Limiter"`): `thresholdDb` (default -6), `makeupTargetDb` (default -1, matches v3's -1dB ceiling), `kneeWidthDb` (default 2), `lookaheadMs` (default 1), `releaseMs` (default 20). No `type`/per-channel gain params like v3 had - v4's Limiter is a compressor-family effect, not the old hard/soft-clip model.
- Loudness (`"Loudness Normalization"`): `StereoIndependent` (bool), `LUFSLevel` (default -23), `RMSLevel` (default -20), `DualMono` (bool, default true), `NormalizeTo` (int enum, default `kLoudness` - **verify the integer value for "LUFS mode" during live testing in Task 8**, since this enum's exact values weren't captured during planning research).

- [ ] **Step 1: Write the failing test**

```python
# tests/test_effects_tools.py
import pytest
from unittest.mock import AsyncMock
import server4.tools.effects_tools as effects_tools


class _FakeMCP:
    def __init__(self):
        self.tools = {}

    def tool(self):
        def decorator(fn):
            self.tools[fn.__name__] = fn
            return fn
        return decorator


@pytest.mark.asyncio
async def test_normalize_builds_correct_params(monkeypatch):
    fake_bridge = AsyncMock()
    monkeypatch.setattr("server4.main.bridge", fake_bridge)

    fake_mcp = _FakeMCP()
    effects_tools.register(fake_mcp)

    await fake_mcp.tools["normalize"](peak_level_db=-3.0, remove_dc=True, stereo_independent=False)

    fake_bridge.call.assert_called_once_with("apply-effect", {
        "effect_id": "Normalize",
        "params": 'PeakLevel=-3.0 RemoveDcOffset=True ApplyVolume=True StereoIndependent=False',
    })


@pytest.mark.asyncio
async def test_normalize_rejects_out_of_range_peak_level(monkeypatch):
    fake_bridge = AsyncMock()
    monkeypatch.setattr("server4.main.bridge", fake_bridge)
    fake_mcp = _FakeMCP()
    effects_tools.register(fake_mcp)

    with pytest.raises(ValueError, match="peak_level_db"):
        await fake_mcp.tools["normalize"](peak_level_db=10.0)
```

- [ ] **Step 2: Run to verify it fails**

```bash
python -m pytest tests/test_effects_tools.py -v
```

Expected: FAIL.

- [ ] **Step 3: Rewrite `effects_tools.py`**

```python
from mcp.server.fastmcp import FastMCP


def _params(**kwargs) -> str:
    """Format kwargs as Audacity's Key=Value automation string, quoting values
    containing spaces (matches au3-components/EffectAutomationParameters.h's
    shell-tokenized SetParameters() format)."""
    parts = []
    for key, value in kwargs.items():
        s = str(value)
        if " " in s:
            s = f'"{s}"'
        parts.append(f"{key}={s}")
    return " ".join(parts)


def register(mcp: FastMCP):
    from server4.main import bridge

    @mcp.tool()
    async def normalize(
        peak_level_db: float = -3.0,
        remove_dc: bool = True,
        stereo_independent: bool = False,
    ) -> dict:
        """Normalize the selected audio to a target peak level.

        Args:
            peak_level_db: Target peak level in dB (-145 to 0). Default: -3.0
            remove_dc: Remove DC offset before normalizing. Default: True
            stereo_independent: Normalize L/R channels separately. Default: False
        """
        if not -145 <= peak_level_db <= 0:
            raise ValueError("peak_level_db must be -145 to 0")
        params = _params(
            PeakLevel=peak_level_db, RemoveDcOffset=remove_dc,
            ApplyVolume=True, StereoIndependent=stereo_independent,
        )
        return await bridge.call("apply-effect", {"effect_id": "Normalize", "params": params})

    @mcp.tool()
    async def noise_reduction(
        sensitivity: float = 6.0,
        noise_gain_db: float = 12.0,
        frequency_smoothing_bands: int = 3,
    ) -> dict:
        """Apply noise reduction to the selected audio. Select a region of pure
        noise first and run this once to capture a profile, then select the
        audio to clean and run it again.

        Args:
            sensitivity: Detection sensitivity (0.01-24). Default: 6.0
            noise_gain_db: Amount of noise reduction in dB (0-48). Default: 12.0
            frequency_smoothing_bands: Frequency smoothing bands (0-12). Default: 3
        """
        if not 0.01 <= sensitivity <= 24:
            raise ValueError("sensitivity must be 0.01 to 24")
        if not 0 <= noise_gain_db <= 48:
            raise ValueError("noise_gain_db must be 0 to 48")
        if not 0 <= frequency_smoothing_bands <= 12:
            raise ValueError("frequency_smoothing_bands must be 0 to 12")
        params = _params(**{
            "Sensitivity": sensitivity,
            "Noise Gain": noise_gain_db,
            "Frequency Smoothing Bands": frequency_smoothing_bands,
        })
        return await bridge.call("apply-effect", {"effect_id": "Noise reduction", "params": params})

    @mcp.tool()
    async def compressor(
        threshold_db: float = -12.0,
        ratio: float = 4.0,
        attack_ms: float = 3.0,
        release_ms: float = 100.0,
        makeup_gain_db: float = -3.0,
        knee_width_db: float = 6.0,
    ) -> dict:
        """Apply dynamic range compression to the selected audio.

        Args:
            threshold_db: Level above which compression starts (dB). Default: -12
            ratio: Compression ratio (e.g. 4.0 = 4:1). Default: 4.0
            attack_ms: Attack time in milliseconds. Default: 3.0
            release_ms: Release time in milliseconds. Default: 100.0
            makeup_gain_db: Makeup gain applied after compression (dB). Default: -3.0
            knee_width_db: Soft-knee width in dB. Default: 6.0
        """
        params = _params(
            thresholdDb=threshold_db, compressionRatio=ratio, attackMs=attack_ms,
            releaseMs=release_ms, makeupGainDb=makeup_gain_db, kneeWidthDb=knee_width_db,
        )
        return await bridge.call("apply-effect", {"effect_id": "Compressor", "params": params})

    @mcp.tool()
    async def limiter(
        threshold_db: float = -6.0,
        makeup_target_db: float = -1.0,
        release_ms: float = 20.0,
        knee_width_db: float = 2.0,
    ) -> dict:
        """Apply a limiter to prevent audio from exceeding a ceiling.

        Args:
            threshold_db: Level above which limiting starts (dB). Default: -6
            makeup_target_db: Output ceiling (dB) - the industry-standard streaming
                ceiling is -1.0. Default: -1.0
            release_ms: Release time in milliseconds. Default: 20.0
            knee_width_db: Soft-knee width in dB. Default: 2.0
        """
        params = _params(
            thresholdDb=threshold_db, makeupTargetDb=makeup_target_db,
            releaseMs=release_ms, kneeWidthDb=knee_width_db,
        )
        return await bridge.call("apply-effect", {"effect_id": "Limiter", "params": params})

    @mcp.tool()
    async def loudness_normalize(
        lufs_level: float = -16.0,
        stereo_independent: bool = False,
        dual_mono: bool = True,
    ) -> dict:
        """Normalize audio to a target LUFS loudness. DANGER: can boost quiet/badly
        recorded audio by 20-30dB causing clipping - measure levels with
        auto_analyze_audio first. Targets: -16 LUFS (Apple Podcasts), -14 LUFS
        (Spotify/YouTube), -11 LUFS (loud masters).

        Args:
            lufs_level: Target loudness in LUFS (-145 to 0). Default: -16.0
            stereo_independent: Normalize L/R channels independently. Default: False
            dual_mono: Treat mono as dual-mono for correct LUFS measurement. Default: True
        """
        if not -145 <= lufs_level <= 0:
            raise ValueError("lufs_level must be -145 to 0")
        # NOTE: NormalizeTo's integer value for "LUFS mode" (vs RMS mode) must be
        # confirmed live (Task 8) - 0 is the working assumption, matching v3's
        # LoudnessNormalization NormalizeTo=0 convention, but v4's enum values were
        # not confirmed during planning.
        params = _params(
            NormalizeTo=0, StereoIndependent=stereo_independent,
            LUFSLevel=lufs_level, DualMono=dual_mono,
        )
        return await bridge.call("apply-effect", {"effect_id": "Loudness Normalization", "params": params})
```

- [ ] **Step 4: Run to verify tests pass**

```bash
python -m pytest tests/test_effects_tools.py -v
```

Expected: PASS.

- [ ] **Step 5: Live test each effect individually against the running Audacity4 instance**

For each of the 5 tools, call it directly (same pattern as Task 6 Step 5) with a real audio clip loaded and selected, and confirm via a follow-up `auto_analyze_audio` call (or a screenshot of the waveform) that the effect actually changed the audio as expected - not just that the call returned success. **This is where `"Noise Reduction Choice"`'s and `NormalizeTo`'s real enum values get confirmed** - if the default (0) produces the wrong behavior (e.g. noise reduction capturing a profile instead of reducing, or loudness targeting RMS instead of LUFS), fix the constant in `effects_tools.py` and re-test before moving on.

- [ ] **Step 6: Commit** (only after explicit user go-ahead)

---

### Task 8: `cleanup_tools.py` - podcast pipeline + job tracking

**Files:**
- Create: `server4/tools/cleanup_tools.py`
- Test: `tests/test_cleanup_tools.py`

**Interfaces:**
- Consumes: `bridge.call("select-all", {})`, `bridge.call("select-time", {"start":.., "end":..})`, `bridge.call("apply-effect", {...})` from Tasks 2/3/5; the effect-parameter knowledge from Task 7 (but calls `apply-effect` directly with pre-built params strings, not through the `effects_tools.py` MCP tool wrappers, to avoid one tool calling another).
- Produces: MCP tools `auto_cleanup_podcast(remove_noise: bool = True) -> dict` (returns a job_id immediately, matching v3's async-job pattern) and `check_pipeline_status(job_id: str) -> dict`.

- [ ] **Step 1: Write the failing test for job tracking**

```python
# tests/test_cleanup_tools.py
import pytest
from unittest.mock import AsyncMock
import server4.tools.cleanup_tools as cleanup_tools


class _FakeMCP:
    def __init__(self):
        self.tools = {}

    def tool(self):
        def decorator(fn):
            self.tools[fn.__name__] = fn
            return fn
        return decorator


@pytest.mark.asyncio
async def test_auto_cleanup_podcast_returns_job_id_immediately(monkeypatch):
    fake_bridge = AsyncMock()
    fake_bridge.call.return_value = {"content": [{"text": "ok"}], "isError": False}
    monkeypatch.setattr("server4.main.bridge", fake_bridge)

    fake_mcp = _FakeMCP()
    cleanup_tools.register(fake_mcp)

    result = await fake_mcp.tools["auto_cleanup_podcast"]()

    assert "job_id" in result
    assert result["status"] == "running"


@pytest.mark.asyncio
async def test_check_pipeline_status_reports_unknown_job():
    fake_mcp = _FakeMCP()
    cleanup_tools.register(fake_mcp)

    with pytest.raises(Exception, match="Unknown job_id"):
        await fake_mcp.tools["check_pipeline_status"]("does-not-exist")
```

- [ ] **Step 2: Run to verify it fails**

```bash
python -m pytest tests/test_cleanup_tools.py -v
```

Expected: FAIL.

- [ ] **Step 3: Implement `cleanup_tools.py`**

Port the job-tracking infrastructure (`_create_job`, `_run_pipeline_step`, `_cleanup_stale_jobs`, `check_pipeline_status`, the "pipeline already running" guard) from v3's `cleanup_tools.py` (lines 1-42, 60-101, 581-608) essentially unchanged - it's pure asyncio/dict bookkeeping with no Audacity-specific calls. Build the podcast pipeline per the spec's validated settings (DC offset removal, HPF, noise reduction unchanged from v3's values; compression using v4's real Compressor params from Task 7; loudness via v4's real `loudness` effect targeting -16 LUFS with a measure-first/refuse-if-clip safety gate, falling back to peak-only reduction if the gate refuses):

```python
import asyncio
import time
import uuid

from mcp.server.fastmcp import FastMCP
from server4.tools.effects_tools import _params
from server4.tools.analysis_tools import _measure_wav, _temp_wav_path
import os

_jobs: dict[str, dict] = {}
_MAX_COMPLETED_JOBS = 50
_STALE_JOB_TIMEOUT = 600
_job_lock = asyncio.Lock()


def _cleanup_stale_jobs():
    now = time.time()
    for job_id, job in list(_jobs.items()):
        if job["status"] == "running" and (now - job["started_at"]) > _STALE_JOB_TIMEOUT:
            job["status"] = "error"
            job["error"] = "Timed out after 10 minutes"
    completed = [(k, v) for k, v in _jobs.items() if v["status"] in ("complete", "error")]
    if len(completed) > _MAX_COMPLETED_JOBS:
        completed.sort(key=lambda x: x[1].get("started_at", 0))
        for k, _ in completed[:-_MAX_COMPLETED_JOBS]:
            del _jobs[k]


def _has_running_pipeline() -> bool:
    return any(j["status"] == "running" for j in _jobs.values())


async def _create_job(pipeline_name: str):
    async with _job_lock:
        _cleanup_stale_jobs()
        if _has_running_pipeline():
            return None, None
        job_id = str(uuid.uuid4())[:8]
        job = {
            "status": "running", "pipeline": pipeline_name, "current_step": "starting",
            "steps_applied": [], "steps_failed": [], "started_at": time.time(),
            "result": None, "error": None,
        }
        _jobs[job_id] = job
        return job_id, job


def _running_job_error() -> dict:
    running = next(j for j in _jobs.values() if j["status"] == "running")
    running_id = next(k for k, v in _jobs.items() if v is running)
    return {
        "error": "A pipeline is already running. Do NOT start another one.",
        "job_id": running_id, "current_step": running["current_step"],
        "message": "Use check_pipeline_status to monitor the existing pipeline.",
    }


async def _run_step(job: dict, name: str, bridge, effect_id: str, params: str):
    job["current_step"] = name
    try:
        await bridge.call("select-all", {})
        await bridge.call("apply-effect", {"effect_id": effect_id, "params": params})
        job["steps_applied"].append(name)
    except Exception as e:
        job["steps_failed"].append(f"{name}: {e}")
    await asyncio.sleep(0.5)


async def _noise_reduction_step(job: dict, bridge, sensitivity: float, noise_gain_db: float, smoothing_bands: int):
    """Noise reduction is a TWO-CALL effect, same as in v3 - do not collapse this
    into a single apply-effect call. Step 1 selects a short noise-only region and
    calls the effect with NO params, which captures that selection as the noise
    profile. Step 2 selects everything and calls it again WITH params to apply
    reduction using the captured profile. Skipping step 1 either fails outright
    or silently no-ops, depending on how v4 ported this from v3/Audacity 3.x -
    confirm which during live testing (Task 8 Step 5)."""
    job["current_step"] = "noise profile capture"
    try:
        await bridge.call("select-time", {"start": 0, "end": 0.5})
        await bridge.call("apply-effect", {"effect_id": "Noise reduction", "params": ""})
        await asyncio.sleep(1.0)

        job["current_step"] = "noise reduction"
        await bridge.call("select-all", {})
        params = _params(**{
            "Sensitivity": sensitivity, "Noise Gain": noise_gain_db,
            "Frequency Smoothing Bands": smoothing_bands,
        })
        await bridge.call("apply-effect", {"effect_id": "Noise reduction", "params": params})
        job["steps_applied"].append(f"noise reduction {noise_gain_db}dB")
    except Exception as e:
        job["steps_failed"].append(f"noise reduction: {e}")
    await asyncio.sleep(0.5)


async def _measure_current(bridge) -> dict | None:
    tmp_wav = _temp_wav_path()
    try:
        await bridge.call("select-all", {})
        await bridge.call("export-wav", {"path": tmp_wav})
        if not os.path.exists(tmp_wav):
            return None
        return _measure_wav(tmp_wav)
    except Exception:
        return None
    finally:
        try:
            os.remove(tmp_wav)
        except OSError:
            pass


async def _safe_loudness_step(job: dict, bridge, lufs_target: float = -16.0):
    """Measure first; only apply LUFS normalization if it won't clip. Falls back
    to peak-only reduction (never boost) if the projected result would clip -
    same safety discipline as v3's loudness_normalize, now automatic since v4
    has a real LUFS effect to gate."""
    measured = await _measure_current(bridge)
    if measured is None or measured.get("peak_db") is None:
        job["steps_applied"].append("loudness skipped (measurement failed)")
        return

    peak_before = measured["peak_db"]
    rms_before = measured.get("overall_rms_db")

    if rms_before is not None:
        estimated_gain_db = lufs_target - rms_before
        projected_peak_db = peak_before + estimated_gain_db
        if projected_peak_db <= -1.0:
            await bridge.call("select-all", {})
            params = _params(NormalizeTo=0, StereoIndependent=False, LUFSLevel=lufs_target, DualMono=True)
            try:
                await bridge.call("apply-effect", {"effect_id": "Loudness Normalization", "params": params})
                job["steps_applied"].append(f"loudness normalized to {lufs_target} LUFS")
                return
            except Exception as e:
                job["steps_failed"].append(f"loudness normalize: {e}")

    # Fallback: only reduce peaks if hot, never boost
    if peak_before > -3.0:
        await bridge.call("select-all", {})
        params = _params(PeakLevel=-3.0, RemoveDcOffset=False, ApplyVolume=True, StereoIndependent=False)
        try:
            await bridge.call("apply-effect", {"effect_id": "Normalize", "params": params})
            job["steps_applied"].append("peaks reduced to -3dB (LUFS target would have clipped)")
        except Exception as e:
            job["steps_failed"].append(f"fallback normalize: {e}")
    else:
        job["steps_applied"].append(f"peaks already at {peak_before}dB - no change needed")


async def _podcast_pipeline(job: dict, bridge, remove_noise: bool):
    try:
        await _run_step(job, "remove DC offset", bridge, "Normalize",
                         _params(PeakLevel=-1.0, RemoveDcOffset=True, ApplyVolume=False, StereoIndependent=False))
        await _run_step(job, "HPF 80Hz", bridge, "High-passFilter", _params(frequency=80.0, rolloff="dB12"))

        if remove_noise:
            await _noise_reduction_step(job, bridge, sensitivity=6.0, noise_gain_db=12.0, smoothing_bands=3)

        await _run_step(job, "compress 4:1", bridge, "Compressor",
                         _params(thresholdDb=-18.0, compressionRatio=4.0, attackMs=3.0,
                                 releaseMs=200.0, makeupGainDb=0.0, kneeWidthDb=6.0))

        await _safe_loudness_step(job, bridge, lufs_target=-16.0)

        job["status"] = "complete"
        job["current_step"] = "done"
        elapsed = round(time.time() - job["started_at"], 1)
        job["result"] = {
            "success": len(job["steps_failed"]) == 0,
            "message": f"Podcast Cleanup: {' > '.join(job['steps_applied'])}",
            "elapsed_seconds": elapsed,
        }
        if job["steps_failed"]:
            job["result"]["warnings"] = job["steps_failed"].copy()
    except Exception as e:
        job["status"] = "error"
        job["error"] = str(e)


def register(mcp: FastMCP):
    from server4.main import bridge

    @mcp.tool()
    async def auto_cleanup_podcast(remove_noise: bool = True) -> dict:
        """ONE-CLICK PODCAST CLEANUP. Runs in background - returns a job_id
        immediately. Use check_pipeline_status to monitor.

        Pipeline: DC offset > HPF 80Hz > noise reduction (opt) > compress 4:1 >
        safe LUFS loudness (-16 LUFS, Apple Podcasts target) with clip-safe fallback.

        Args:
            remove_noise: Apply noise reduction using the first 0.5s as a noise
                profile. Default: True. IMPORTANT: the first 0.5s should be room
                tone/silence if this is True.
        """
        job_id, job = await _create_job("podcast_cleanup")
        if job is None:
            return _running_job_error()
        coro = _podcast_pipeline(job, bridge, remove_noise)
        job["_task"] = asyncio.create_task(coro)
        return {
            "job_id": job_id, "status": "running",
            "message": "Podcast Cleanup started. Call check_pipeline_status every 15-30s.",
        }

    @mcp.tool()
    async def check_pipeline_status(job_id: str) -> dict:
        """Check the status of a running cleanup pipeline.

        Args:
            job_id: The job ID returned by an auto_ pipeline tool.
        """
        _cleanup_stale_jobs()
        job = _jobs.get(job_id)
        if not job:
            raise ValueError(f"Unknown job_id: {job_id}")

        elapsed = round(time.time() - job["started_at"], 1)
        result = {
            "job_id": job_id, "status": job["status"], "current_step": job["current_step"],
            "steps_completed": job["steps_applied"].copy(), "elapsed_seconds": elapsed,
        }
        if job["status"] == "complete":
            result["result"] = job["result"]
        elif job["status"] == "error":
            result["error"] = job["error"]
        if job["steps_failed"]:
            result["warnings"] = job["steps_failed"].copy()
        return result
```

**NOTE on the DC-offset step:** it reuses `Normalize` with `ApplyVolume=False, RemoveDcOffset=True` - confirm this actually skips gain change and only removes DC offset during Task 9's live testing (matches v3's exact same trick with `ApplyGain=False`).

**NOTE on High-passFilter:** the effect id/param names (`"High-passFilter"`, `frequency`, `rolloff`) are carried over from v3 unverified against v4's source (this effect wasn't part of the parameter research done during planning, since it's not in `builtin_collection`'s dynamics/normalize/noisereduction/loudness folders checked). Grep `src/effects/builtin_collection/` for the actual high-pass filter effect and its real param names before this step, following the same method used for Compressor/Limiter/Normalize (find the effect's `.h`, find its `EffectParameter`/`_PARAM` macro invocations) - do not assume v3's names are correct without checking, per the Global Constraints above.

- [ ] **Step 4: Run to verify tests pass**

```bash
python -m pytest tests/test_cleanup_tools.py -v
```

Expected: PASS.

- [ ] **Step 5: Live end-to-end test against a real noisy recording**

Load a real test recording with audible background noise (or record a short clip with room noise) into Audacity4, then:

```bash
python -c "
import asyncio
from server4.main import mcp
result = asyncio.run(mcp._tool_manager._tools['auto_cleanup_podcast'].fn())
print(result)
"
```

Then poll `check_pipeline_status` with the returned `job_id` every few seconds until `status` is `complete` or `error`. Confirm by ear (or a follow-up `auto_analyze_audio` call comparing before/after) that: noise is reduced, dynamics are more consistent, and loudness lands near -16 LUFS without clipping. **This is the step that validates the whole "modern, correct settings" goal of this feature** - do not consider this task done until a real before/after comparison confirms the pipeline actually improves the audio the way the podcast standard requires.

- [ ] **Step 6: Commit** (only after explicit user go-ahead)

---

### Task 9: Audiobook (ACX) pipeline

**Files:**
- Modify: `server4/tools/cleanup_tools.py`
- Test: `tests/test_cleanup_tools.py`

**Interfaces:**
- Consumes: same primitives as Task 8.
- Produces: MCP tool `auto_audiobook_mastering(remove_noise: bool = True) -> dict`.

- [ ] **Step 1: Write the failing test**

```python
@pytest.mark.asyncio
async def test_auto_audiobook_mastering_returns_job_id(monkeypatch):
    fake_bridge = AsyncMock()
    fake_bridge.call.return_value = {"content": [{"text": "ok"}], "isError": False}
    monkeypatch.setattr("server4.main.bridge", fake_bridge)

    fake_mcp = _FakeMCP()
    cleanup_tools.register(fake_mcp)

    result = await fake_mcp.tools["auto_audiobook_mastering"]()

    assert "job_id" in result
    assert result["status"] == "running"
```

- [ ] **Step 2: Run to verify it fails**

```bash
python -m pytest tests/test_cleanup_tools.py -v -k audiobook
```

- [ ] **Step 3: Implement the audiobook pipeline**

Port v3's numbers unchanged (confirmed correct against current ACX spec during planning: RMS -20dB target inside the -23/-18 range, peak cap -3.5dB safely below the -3dB ceiling, noise floor requirement ≤-60dB is a property of the noise reduction step, not separately enforced), translated to v4's real Compressor/Limiter param names from Task 7:

```python
async def _audiobook_pipeline(job: dict, bridge, remove_noise: bool):
    try:
        await _run_step(job, "remove DC offset", bridge, "Normalize",
                         _params(PeakLevel=-1.0, RemoveDcOffset=True, ApplyVolume=False, StereoIndependent=False))
        await _run_step(job, "HPF 80Hz", bridge, "High-passFilter", _params(frequency=80.0, rolloff="dB12"))

        if remove_noise:
            await _noise_reduction_step(job, bridge, sensitivity=6.0, noise_gain_db=12.0, smoothing_bands=3)

        # Light compression for voice consistency (long release for audiobook pacing)
        await _run_step(job, "compress 2.5:1", bridge, "Compressor",
                         _params(thresholdDb=-18.0, compressionRatio=2.5, attackMs=10.0,
                                 releaseMs=1000.0, makeupGainDb=0.0, kneeWidthDb=6.0))

        await _safe_loudness_step(job, bridge, lufs_target=-20.0)  # targets ACX's -20dB RMS sweet spot

        # Peak cap -3.5dB (ACX requires peaks below -3dB; -3.5 gives safety margin)
        await _run_step(job, "peak cap -3.5dB", bridge, "Limiter",
                         _params(thresholdDb=-6.0, makeupTargetDb=-3.5, releaseMs=20.0, kneeWidthDb=2.0))

        job["status"] = "complete"
        job["current_step"] = "done"
        elapsed = round(time.time() - job["started_at"], 1)
        job["result"] = {
            "success": len(job["steps_failed"]) == 0,
            "message": f"Audiobook Mastering (ACX): {' > '.join(job['steps_applied'])}",
            "standard": "ACX/Audible - RMS -23 to -18dB, peaks below -3dB, noise floor below -60dB",
            "elapsed_seconds": elapsed,
        }
        if job["steps_failed"]:
            job["result"]["warnings"] = job["steps_failed"].copy()
    except Exception as e:
        job["status"] = "error"
        job["error"] = str(e)
```

Add the corresponding `@mcp.tool()` registration in `register()`, matching `auto_cleanup_podcast`'s shape.

**NOTE:** `_safe_loudness_step` targets LUFS, but ACX's spec is RMS-based (-23 to -18dB), not LUFS. Since v4's `loudness` effect's `NormalizeTo` enum has both LUFS and RMS modes (confirmed by the presence of both `LUFSLevel` and `RMSLevel` params in `normalizeloudnesseffect.h`), this step should target RMS mode for the audiobook pipeline specifically, not reuse the LUFS-targeting helper as-is - add a `mode: str = "lufs"` parameter to `_safe_loudness_step` (or a small `_safe_rms_step` variant) that sets `NormalizeTo` to whatever the RMS-mode integer value turns out to be (confirm during Task 8 Step 5's live testing when `NormalizeTo`'s enum values get resolved) and reads/writes `RMSLevel` instead of `LUFSLevel`.

- [ ] **Step 4: Run to verify tests pass**

- [ ] **Step 5: Live end-to-end test against a real narration/voice recording**, checked against the ACX numbers (RMS -23 to -18dB, peaks below -3dB) via a follow-up `auto_analyze_audio` call.

- [ ] **Step 6: Commit** (only after explicit user go-ahead)

---

### Task 10: Final end-to-end verification pass

**Files:** none (verification only)

- [ ] **Step 1: Full test suite** - both repos

```bash
# C++
$env:PATH = "D:\DansProject\Audacity4-Dev\src\app\bin;" + $env:PATH
$env:QT_PLUGIN_PATH = "D:\DansProject\Audacity4-Dev\src\app\bin"
& "D:\DansProject\Audacity4-Dev\build\audacity-debug\mcp_tests.exe"

# Python
cd /d/DansProject/Audacity4MCP && python -m pytest tests/ -v
```

Expected: all green.

- [ ] **Step 2: Full live run-through** - fresh Audacity4 launch, real noisy podcast-style recording, run `auto_analyze_audio` → `auto_cleanup_podcast` → poll to completion → `auto_analyze_audio` again to confirm improvement, then repeat with a voice recording for `auto_audiobook_mastering`.

- [ ] **Step 3: Confirm no new crash dumps** in `%LOCALAPPDATA%\CrashDumps` across the whole session's testing.

- [ ] **Step 4: Report to the user** what's verified working, what (if anything) from the plan's flagged uncertainties (High-passFilter's real param names, `NoiseReductionChoice`'s enum values, `NormalizeTo`'s enum values) needed correction during implementation, and confirm before considering this feature slice done.

---

## Explicitly out of scope for this plan

Interview, vocal, live-recording, music-mastering (×6 genres), and lo-fi (×3 intensities) pipelines from v3 - deferred to a follow-up plan once podcast + audiobook are proven working end-to-end, per user's explicit scoping decision during brainstorming.
