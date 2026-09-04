# Muse Framework MCP Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Finish the three incomplete pieces of `muse::rcontrol::mcp` left by its original author (Igor Korsukov, May–Jun 2026), and close a real security gap, so the MCP server is safe to enable by default and actually usable end-to-end.

**Architecture:** Four self-contained fixes inside `framework/rcontrol/`, no new modules. This targets **`musescore/muse_framework`**, a separate repository from `audacity/audacity` with (per file headers) its own CLA — **not** the same PR as `2026-09-04-audacity-mcp-command-register.md`, which depends on this one's `enabled`-gate and `inputSchema`/result-reporting fixes to be fully useful, but can be developed and reviewed independently (the Audacity-side plan's Task 5 Step 7 smoke test is the integration point between the two).

**Tech Stack:** C++20, Qt6 (`QTcpServer`), the Muse modularity/DI framework, gtest/gmock.

**Spec:** This document. Requirements from direct inspection of `musescore/muse_framework` commit `ca86211f8d5e45405de999827c43bdb24e4d682e` (the commit pinned by Audacity's `Audacity-4.0.0` tag) — `framework/rcontrol/mcp/mcpcontroller.cpp`, `framework/rcontrol/mcp/transport/tcptransport.cpp`, `framework/rcontrol/rcontrolmodule.cpp`.

## Global Constraints

- **License header:** every file in `framework/rcontrol/` uses the long form: `SPDX-License-Identifier: GPL-3.0-only` / `MuseScore/Audacity CLA applies` / `Copyright (C) 2026 MuseScore/Audacity and others` / GPLv3 boilerplate. Match exactly — this repo has only the one style, unlike `audacity/audacity`.
- **Indentation:** 4 spaces (confirmed in every file in this subtree).
- **Member naming:** `m_camelCase` (confirmed: `m_mcpServer`, `m_server`, `m_connection`, `m_onRequest`).
- **CLA:** contributing requires the shared MuseScore/Audacity CLA referenced in the file headers — same family as Audacity's own CLA per the header text, but verify the actual signing process at whatever URL `CONTRIBUTING.md` in `musescore/muse_framework` points to before opening a PR; this wasn't independently confirmed in this research pass.
- **Settings key introduced by this plan:** `Settings::Key("rcontrol", "mcp/enabled")`, default `Val(false)`. This is the key the companion `audacity/audacity` plan's future Preferences-checkbox follow-up would bind to — don't rename it without updating that side too.

---

## File Structure

No new files — every change is inside an existing file:

```
framework/rcontrol/
  mcp/
    mcpcontroller.cpp       (modify — Tasks 3, 4, 5)
    transport/
      tcptransport.cpp      (modify — Task 1)
  rcontrolmodule.h           (modify — Task 2)
  rcontrolmodule.cpp         (modify — Task 2)
  tests/
    CMakeLists.txt           (create — Task 6)
    mcpcontroller_tests.cpp  (create — Task 6)
```

---

### Task 1: Bind loopback-only, not all interfaces

**Files:**
- Modify: `framework/rcontrol/mcp/transport/tcptransport.cpp`

**Interfaces:** none — purely internal to `TcpTransport::start()`.

This is a real, currently-shipping gap: `m_server->listen(QHostAddress::Any, DEFAULT_PORT)` binds every network interface, meaning any other machine on the same LAN (or a hostile process on a shared/cloud host) could reach it the moment it's enabled — not just processes on the same machine. Confirmed directly from source; this isn't a hypothetical.

- [ ] **Step 1: Change the bind address**

In `TcpTransport::start()`:

```cpp
    if (!m_server->listen(QHostAddress::Any, DEFAULT_PORT)) {
```

becomes:

```cpp
    if (!m_server->listen(QHostAddress::LocalHost, DEFAULT_PORT)) {
```

- [ ] **Step 2: Manual verification** (no existing test file covers `TcpTransport`, and adding a full socket-level test harness is disproportionate to a one-line fix — verify by hand)

```bash
# after rebuilding with this change and Task 2's gate enabled:
netstat -an | grep 2212
# expect: 127.0.0.1:2212, NOT 0.0.0.0:2212 or *:2212
```

- [ ] **Step 3: Commit**

```bash
git add framework/rcontrol/mcp/transport/tcptransport.cpp
git commit -m "Bind MCP TCP transport to loopback only, not all interfaces"
```

---

### Task 2: Opt-in `enabled` setting, default off

**Files:**
- Modify: `framework/rcontrol/rcontrolmodule.h`
- Modify: `framework/rcontrol/rcontrolmodule.cpp`

**Interfaces:**
- Consumes: `muse::ISettings` (`global/settings.h` — same interface every other configuration in the framework uses, e.g. `settings()->setDefaultValue(...)`, `settings()->value(...)`, confirmed pattern from Audacity's own `appshellconfiguration.cpp`).
- Produces: the `Settings::Key("rcontrol", "mcp/enabled")` key that any host app's Preferences UI can read/write via `settings()->value(key)` / `settings()->setSharedValue(key, Val(true))` — no new exported interface needed for this alone, since a raw settings key is directly bindable.

Today, `RContext::onInit()` unconditionally calls `m_mcpController->init()` whenever `mode == GuiApp` — the server starts the instant the module is instantiated, with no way to turn it off short of not compiling the module in at all. This task adds the missing gate.

- [ ] **Step 1: Add the settings include and key to `rcontrolmodule.cpp`**

```cpp
#include "rcontrolmodule.h"

#include "mcp/mcpcontroller.h"
#include "global/settings.h"

using namespace muse;
using namespace muse::rcontrol;

static const std::string mname("rcontrol");
static const Settings::Key MCP_ENABLED_KEY(mname, "mcp/enabled");
```

- [ ] **Step 2: Register the default and gate `onInit`**

```cpp
void RControlContext::registerExports()
{
    m_mcpController = std::make_shared<mcp::McpController>(iocContext());

    settings()->setDefaultValue(MCP_ENABLED_KEY, Val(false));
}

void RControlContext::onInit(const IApplication::RunMode& mode)
{
    if (mode != IApplication::RunMode::GuiApp) {
        return;
    }

    if (!settings()->value(MCP_ENABLED_KEY).toBool()) {
        return;
    }

    m_mcpController->init();
}
```

- [ ] **Step 3: Inject `settings()` into `RControlContext`**

Check `rcontrolmodule.h` for the current base class of `RControlContext` — it needs a `muse::ContextInject<muse::ISettings> settings { this };` member (or `muse::GlobalInject`, depending on whether `ISettings` is a `MODULE_GLOBAL_INTERFACE` or `MODULE_CONTEXT_INTERFACE` — verify against another module's header, e.g. Audacity's `appshellconfiguration.h`'s own settings injection, before finalizing this line; this plan's research did not directly inspect `ISettings`'s interface macro).

- [ ] **Step 4: Rebuild and verify the gate works**

```bash
cmake --build build --target au4 -j
./build/bin/au4 &
sleep 2
netstat -an | grep 2212   # expect: nothing — default is off
```

Then flip the setting manually (until a Preferences checkbox exists — see the companion plan's Self-Review note) to confirm it can be turned on:

```bash
killall au4
# find Audacity's settings file (path varies by OS/build; check IAppShellConfiguration's settings path or the app's own --help for its config dir), add:
#   rcontrol/mcp/enabled=true
./build/bin/au4 &
sleep 2
netstat -an | grep 2212   # expect: 127.0.0.1:2212
```

- [ ] **Step 5: Commit**

```bash
git add framework/rcontrol/rcontrolmodule.h framework/rcontrol/rcontrolmodule.cpp
git commit -m "Gate MCP server startup behind an opt-in setting, default off"
```

---

### Task 3: Pass `tools/call` arguments through to the dispatched command

**Files:**
- Modify: `framework/rcontrol/mcp/mcpcontroller.cpp`

**Interfaces:**
- Consumes: `muse::JsonObject` (`global/serialization/json.h`), `muse::rcommand::CommandQuery::set()` (used identically in MuseScore's own `PlaybackCommandsRegister`-adjacent call sites — `query.set(name, value)`).

Today `commandQuery()` takes `args` and immediately discards it (`UNUSED(args); // TODO: implement`) — every command reaches Audacity/MuseScore with zero parameters, no matter what the MCP client sent.

- [ ] **Step 1: Write the failing test**

```cpp
// framework/rcontrol/tests/mcpcontroller_tests.cpp (new file — see Task 6 for the full harness this belongs in)
TEST_F(McpControllerTests, ToolCallArgsAreForwardedAsCommandParams)
{
    JsonObject args;
    args.setValue("path", "/tmp/x.wav");

    EXPECT_CALL(*m_commandsDispatcher, dispatch(testing::AllOf(
        testing::Property(&CommandQuery::uri, Command("command://mcp/save-project")),
        testing::ResultOf([](const CommandQuery& q) { return q.params().at("path").toString(); }, std::string("/tmp/x.wav"))
    )));

    m_server->simulateToolsCall("mcp_save-project", args);  // see Task 6 for m_server test double
}
```

(This depends on Task 6's test scaffolding existing first — sequence Task 6 before Tasks 3–5 if executing literally in this file order, or treat Tasks 3–5's "write the test" steps as deferred until Task 6 lands. Noted here for completeness of what correct behavior looks like; the fix itself in Step 2 below is independently correct regardless of test sequencing.)

- [ ] **Step 2: Fix `commandQuery()`**

```cpp
static CommandQuery commandQuery(const std::string& name, const muse::JsonObject& args)
{
    std::string path = name;
    muse::strings::replace(path, "_", "/");
    Command cmd(std::string(COMMAND_SCHEME), path);
    CommandQuery q(cmd);

    for (const std::string& key : args.keys()) {
        q.set(key, args.value(key).toStdString());
    }

    return q;
}
```

Note: `JsonObject`'s real iteration API (`.keys()` + `.value(key)`, vs. a `begin()`/`end()` pair) wasn't directly confirmed in this research pass — check `global/serialization/json.h` for the actual accessor names before finalizing; the commented-out original code (`for (const auto& arg : args)`) suggests range-based iteration may in fact be supported directly, which would be simpler than the `.keys()` form above. Verify and prefer whichever is idiomatic elsewhere in the framework.

- [ ] **Step 3: Run test, verify pass; commit**

```bash
git add framework/rcontrol/mcp/mcpcontroller.cpp
git commit -m "Forward tools/call arguments through to the dispatched command"
```

---

### Task 4: Populate `inputSchema` in `tools/list`

**Files:**
- Modify: `framework/rcontrol/mcp/mcpcontroller.cpp`

**Interfaces:**
- Consumes: `muse::rcommand::CommandInfo::inputSchema` (`InputSchema.args`, a `std::map<std::string, Arg>` — already populated by any `IModuleCommandsRegister` implementation, e.g. Audacity's `AudacityCommandsRegister` from the companion plan, or MuseScore's `PlaybackCommandsRegister` for its `rewind` command's `position` arg).

Today `makeToolsList()` builds an empty `InputSchema()` for every tool regardless of what the command register declared — an MCP client has no way to discover what arguments a tool accepts.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_F(McpControllerTests, ToolsListIncludesDeclaredInputSchema)
{
    // given a registered command whose CommandInfo.inputSchema has one Arg("position", DataType::Float, ...)
    auto tools = m_controller->makeToolsList();

    auto it = std::find_if(tools.begin(), tools.end(), [](const Tool& t) { return t.name == "playback_rewind"; });
    ASSERT_NE(it, tools.end());
    ASSERT_EQ(it->inputSchema.args.size(), size_t(1));  // adjust field name to match Tool's actual InputSchema shape
}
```

- [ ] **Step 2: Fix `makeToolsList()`**

```cpp
std::vector<Tool> McpController::makeToolsList() const
{
    std::vector<Tool> tools;
    auto commandList = commandsRegister()->commandInfoList();
    tools.reserve(commandList.size());
    for (const auto& info : commandList) {
        Tool tool;
        tool.name = commandToToolName(info.command);
        tool.title = info.title.raw().translated().toStdString();
        tool.description = info.description.translated().toStdString();

        InputSchema schema;
        for (const auto& [argName, arg] : info.inputSchema.args) {
            Property property;
            property.name = String::fromStdString(argName);
            property.type = String::fromStdString(dataTypeToString(arg.type));   // see Step 3
            property.description = arg.description;
            property.minimum = arg.min.isValid() ? arg.min.toString() : String();
            property.maximum = arg.max.isValid() ? arg.max.toString() : String();
            schema.properties.push_back(property);   // verify actual InputSchema/Property container field name
        }
        tool.inputSchema = schema;

        tools.push_back(std::move(tool));
    }
    return tools;
}
```

- [ ] **Step 3: Add `dataTypeToString()`**

`DataType` (`Undefined`, `String`, `Integer`, `Float`, `Boolean`, `Object`, `Array`, `Null`) needs a string form for the MCP `inputSchema` JSON (MCP's own spec uses JSON Schema type names — `"string"`, `"integer"`, `"number"`, `"boolean"`, `"object"`, `"array"`, `"null"`). Add near `commandToToolName()`:

```cpp
static std::string dataTypeToString(DataType type)
{
    switch (type) {
    case DataType::String: return "string";
    case DataType::Integer: return "integer";
    case DataType::Float: return "number";
    case DataType::Boolean: return "boolean";
    case DataType::Object: return "object";
    case DataType::Array: return "array";
    case DataType::Null: return "null";
    case DataType::Undefined: default: return "string";
    }
}
```

Note: this task's Step 2 references a `Tool`/`InputSchema`/`Property` shape (`mcptypes.h`) that wasn't directly fetched in this research pass — only `mcpcontroller.cpp`'s *usage* of `InputSchema()`/commented-out `Property` fields was seen, which is where `property.name`/`.type`/`.description`/`.minimum`/`.maximum` above come from. **Fetch `framework/rcontrol/mcp/mcptypes.h` and confirm the exact `Tool`/`InputSchema` field names before writing this for real** — the field names above are inferred from the commented-out code in the current source, not confirmed against the struct definition.

- [ ] **Step 4: Run test, verify pass; commit**

```bash
git add framework/rcontrol/mcp/mcpcontroller.cpp
git commit -m "Populate tools/list inputSchema from the command register's declared Args"
```

---

### Task 5: Report dispatch results and errors back through `tools/call`

**Files:**
- Modify: `framework/rcontrol/mcp/mcpcontroller.cpp`

**Interfaces:**
- Consumes: `muse::rcommand::ICommandDispatcher::dispatch(CommandQuery)` returns `async::Promise<Response>` (confirmed in `icommanddispatcher.h`), `Response.ret` (a `muse::Ret`, has `.success()`/similar per every other `Ret` usage in the framework).

Today `onToolsCallRequest`'s handler ignores the dispatch outcome entirely — `commandsDispatcher()->dispatch(commandQuery(name, args)); onResult(ToolResult());` — success and failure look identical to the MCP client.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_F(McpControllerTests, ToolsCallReportsFailureFromDispatch)
{
    ON_CALL(*m_commandsDispatcher, dispatch(testing::_))
    .WillByDefault(testing::Return(async::Promise<Response>([](auto resolve) {
        Response r; r.ret = make_ret(Ret::Code::UnknownError);
        return resolve(r);
    })));

    ToolResult result = m_server->simulateToolsCall("mcp_save-project", JsonObject());
    EXPECT_TRUE(result.isError);   // verify actual ToolResult field name against mcptypes.h
}
```

- [ ] **Step 2: Fix the handler in `init()`**

```cpp
    m_mcpServer->onToolsCallRequest([this](const std::string& name,
                                           const muse::JsonObject& args,
                                           const McpServer::ToolsCallResultHandler& onResult)
    {
        LOGDA() << "Tools call: " << name;

        commandsDispatcher()->dispatch(commandQuery(name, args))
        .onResolve(this, [onResult](const Response& response) {
            ToolResult result;
            result.isError = !response.ret.success();
            if (!response.ret.success()) {
                result.errorMessage = response.ret.text();   // verify actual Ret/ToolResult field names
            }
            onResult(result);
        });
    });
```

Note: `Promise<Response>::onResolve` and `Ret::success()`/`.text()` are the framework's standard promise/result idiom (seen consistently across `muse::async::Promise` usage elsewhere), but the exact `ToolResult` field names (`isError`/`errorMessage` above) weren't confirmed against `mcptypes.h` in this research pass — same caveat as Task 4 Step 3. Fetch `mcptypes.h` once, and resolve both tasks' field-name TODOs together.

- [ ] **Step 3: Run test, verify pass; commit**

```bash
git add framework/rcontrol/mcp/mcpcontroller.cpp
git commit -m "Report tools/call success/failure back to the MCP client instead of fire-and-forget"
```

---

### Task 6: Test scaffolding for `McpController`

**Files:**
- Create: `framework/rcontrol/tests/CMakeLists.txt`
- Create: `framework/rcontrol/tests/mcpcontroller_tests.cpp`
- Modify: `framework/rcontrol/CMakeLists.txt` (currently has `#if (MUSE_MODULE_RCONTROL_TESTS)` commented out entirely — uncomment and wire it properly)

**Interfaces:**
- Produces: the `McpControllerTests` fixture Tasks 3–5 reference, with a mock `muse::rcommand::ICommandsRegister` (`commandsRegister()`) and mock `ICommandDispatcher` (`commandsDispatcher()`), plus a small test double for `McpServer` (`m_server`) that can call `simulateToolsCall(name, args)` directly instead of going over a real TCP socket.

This is genuinely the largest single task in this plan — building the harness `McpControllerTests` needs (mocking two DI-resolved interfaces plus stubbing `McpServer`'s request callbacks) is more setup than any individual fix in Tasks 3–5. Do this task **first**, before Tasks 3–5's "write the failing test" steps, despite its ordering in this document — the document lists it last only because it's most legible read after the fixes it supports are understood.

- [ ] **Step 1: Uncomment and fix the tests block in `framework/rcontrol/CMakeLists.txt`**

```cmake
if (MUSE_MODULE_RCONTROL_TESTS)
    add_subdirectory(tests)
endif()
```

- [ ] **Step 2: Write `framework/rcontrol/tests/CMakeLists.txt`**, modeled on another small framework module's test CMakeLists (e.g. `framework/toast/tests/CMakeLists.txt` or similar — pick whichever sibling module's test setup is smallest, since `rcontrol` has no prior art of its own to copy). Verify the actual macro name (likely `muse_add_test` or similar, parallel to Audacity's own unconfirmed `declare_module`/`setup_module` — see the companion plan's Task 5 Step 3 note) against that sibling file directly; do not guess it here.

- [ ] **Step 3: Write the fixture skeleton in `mcpcontroller_tests.cpp`**, providing `m_commandsRegister` (mock `IModuleCommandsRegister`-aggregating `ICommandsRegister`), `m_commandsDispatcher` (mock `ICommandDispatcher`, same shape as the Audacity-side plan's `CommandDispatcherMock` — consider whether this mock belongs in a shared location both repos could reference, though as two separate repos there's no clean way to share it; duplicating a ~10-line gmock class in both places is fine), and `m_server` exposing a `simulateToolsCall(name, args) -> ToolResult` helper that invokes whatever callback `McpController::init()` registered via `m_mcpServer->onToolsCallRequest(...)` without needing a real socket. This requires `McpServer` to be constructed with an injectable transport or the callback captured directly — inspect `mcpserver.h`'s constructor before finalizing (not fetched in this research pass).

- [ ] **Step 4: Run the full `rcontrol` test suite**

```bash
cmake --build build --target rcontrol_tests -j
./build/bin/rcontrol_tests
```

Expected: all of Tasks 3–5's tests pass together.

- [ ] **Step 5: Commit**

```bash
git add framework/rcontrol/tests/ framework/rcontrol/CMakeLists.txt
git commit -m "Add McpController test scaffolding"
```

---

## Self-Review

**Spec coverage:** Task 1 closes the loopback gap. Task 2 makes the server opt-in (addressing the user's decision that this must be enabled via settings, not on by default). Tasks 3–5 close blarghmatey's three TODOs exactly as found in `mcpcontroller.cpp`. Task 6 is the test infrastructure all three depend on.

**Placeholder scan:** Several steps (Task 3 Step 2, Task 4 Steps 2–3, Task 5 Step 2, Task 6 Step 2–3) contain explicit, labeled notes that a specific type/field name wasn't independently confirmed against source in this research pass, with a precise instruction on which file to check and what to verify. This is disclosure of the honest boundary of this session's research, not a vague "add appropriate handling" placeholder — every such note names the exact symbol to look up and the exact file it lives in. Resolve all of them by reading `mcptypes.h` and the relevant sibling `CMakeLists.txt`/`ISettings` header once, before executing this plan for real, rather than guessing.

**Type consistency:** `CommandQuery`, `Command`, `Response`, `Ret`, `JsonObject`, `DataType` are used identically across all tasks, matching `commandtypes.h`'s real definitions fetched during research. `Tool`/`InputSchema`/`Property`/`ToolResult` (from the unfetched `mcptypes.h`) are the one family of types this plan could not fully verify — flagged consistently everywhere they appear rather than asserted as fact.
