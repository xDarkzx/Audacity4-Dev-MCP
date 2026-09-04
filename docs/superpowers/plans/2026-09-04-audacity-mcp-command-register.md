# Audacity MCP Command Register Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire Audacity's own actions into the already-vendored (but unused) `muse::rcontrol` MCP server, so a stock build can list and execute a curated set of Audacity commands over MCP.

**Architecture:** Add a new `src/mcp` module that (1) instantiates `muse::rcontrol::RControlModule` in `appfactory.cpp`, (2) implements `muse::rcommand::IModuleCommandsRegister` to describe a curated set of commands (the catalog `McpController::makeToolsList()` reads), and (3) implements `muse::rcommand::Commandable` to actually execute those commands by forwarding to Audacity's existing `muse::actions::IActionsDispatcher`. This repo's job is purely the Audacity-side wiring; the server itself (transport, protocol framing, the loopback-bind fix, and the opt-in `enabled` setting) is a **separate, companion PR against `musescore/muse_framework`** — see `2026-09-04-muse-framework-mcp-fixes.md`. Both PRs are needed for the feature to work end-to-end, but each is independently reviewable and buildable against the other's `master`.

**Tech Stack:** C++20, Qt6, the Muse modularity/DI framework (`muse::modularity::ioc`), gtest/gmock (existing Audacity test convention).

**Spec:** This document — no separate spec file; requirements were established via direct source inspection of `audacity/audacity` (commit `4c177d4`, tag `Audacity-4.0.0`) and `musescore/muse_framework` (commit `ca86211f`), recorded inline below and cross-referenced against `[[project-audacity4-scripting-gap]]` memory.

## Global Constraints

- **License header:** the codebase has two coexisting styles. New files go with the long form seen in `src/appshell/internal/applicationactioncontroller.h` (SPDX + `Audacity-CLA-applies` + GPLv3 boilerplate) — it's the more complete form and matches the CLA marker convention used throughout the vendored framework. Match this exact block verbatim (only the file doesn't change; the header is identical across all Audacity-side files in this plan).
- **Indentation:** 4 spaces, no tabs (`.editorconfig` + confirmed in every sampled file — ignore the legacy `audacity.gitbook.io/dev` "3 spaces" guidance, which documents the old `au3/` wxWidgets tree, not the Qt6 `src/` tree this plan touches).
- **Member naming:** `m_camelCase` (confirmed in `TcpTransport`, `McpController`, etc.) — not the legacy `mCamelCase` from the gitbook doc.
- **Braces:** Allman (own line) for function/class bodies; K&R (same line) for `if`/`for`/lambda blocks. Confirmed from every sampled file (e.g. `void McpController::init()\n{` vs `if (mode != IApplication::RunMode::GuiApp) {`).
- **No naked `new`/`delete`** outside of `app->addModule(new ...)` (that pattern is how the whole app wires modules — matches existing code, do not "fix" it).
- **CLA:** contributing requires signing https://www.audacityteam.org/cla/ before a PR can be merged — do this before opening the PR, not after.
- **Namespace:** all new Audacity-side code lives in `au::mcp`, mirroring `au::appshell`, `au::playback`, etc.
- **Settings key this repo binds to (owned by the companion PR, not this one):** `Settings::Key("rcontrol", "mcp/enabled")`, default `Val(false)`. This repo's job is to read it for the Preferences checkbox note in Task 5 — do not redeclare or default it here.

---

## File Structure

```
src/mcp/
  CMakeLists.txt
  mcpmodule.h
  mcpmodule.cpp
  internal/
    audacitycommandsregister.h
    audacitycommandsregister.cpp
    audacitycommandscontroller.h
    audacitycommandscontroller.cpp
  tests/
    CMakeLists.txt
    audacitycommandsregister_tests.cpp
    audacitycommandscontroller_tests.cpp
    mocks/
      commandsregistermock.h
      commanddispatchermock.h
```

- `audacitycommandsregister.*` — pure data: the catalog of exposed commands (`IModuleCommandsRegister`). No dependencies on the actions system beyond the `ActionCode` string constants.
- `audacitycommandscontroller.*` — pure wiring: binds each cataloged command to `dispatcher()->dispatch(actionCode)` via `commandDispatcher()->onRequest(...)`. No knowledge of what the actions actually do.
- `mcpmodule.*` — DI glue only: constructs both, registers the catalog with the global `ICommandsRegister`, and instantiates `RControlModule` in `appfactory.cpp` (that part lives in `src/app`, not `src/mcp`, since `appfactory.cpp` owns the master module list).

---

### Task 1: Confirm `MUSE_MODULE_RCONTROL` builds today, unused

**Files:**
- Read-only verification, no changes.

- [ ] **Step 1: Configure and build once to establish a baseline**

```bash
cd D:/DansProject/Audacity4-Dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build/audacity-debug --target Audacity4.exe -j
```

Expected: build succeeds (this is the unmodified 4.0.0 release; it must build clean before any patch is judged against it). If it fails for unrelated environment reasons (missing Qt/vcpkg toolchain), resolve that first — none of the later tasks are testable without a working build.

- [ ] **Step 2: Confirm `RControlModule` object files are actually produced**

```bash
grep -r "MUSE_MODULE_RCONTROL" build/CMakeCache.txt
find build -iname "*rcontrol*" -o -iname "*mcpcontroller*"
```

Expected: `MUSE_MODULE_RCONTROL:BOOL=ON` (or similar) in the cache, and object files under `build/.../muse_rcontrol/` — confirming the module compiles by default today (per `framework/cmake/MuseModules.cmake` in the `muse` submodule) but is simply never instantiated. If the flag is OFF, add `set(MUSE_MODULE_RCONTROL ON)` to `SetupConfigure.cmake` near the existing `set(MUSE_MODULE_UPDATE OFF)` block, and re-run Step 1.

- [ ] **Step 3: Commit nothing yet** — this task is pure verification, establishing the starting point for Task 2's diff.

---

### Task 2: Instantiate `RControlModule` in `appfactory.cpp`

**Files:**
- Modify: `src/app/appfactory.cpp`

**Interfaces:**
- Consumes: `muse::rcontrol::RControlModule` (class, `muse/framework/rcontrol/rcontrolmodule.h`) — no constructor args, matches every other `app->addModule(new muse::x::XModule())` call in this file.
- Produces: nothing new for later tasks — this just makes the (currently inert) MCP server bind its port. `McpController::makeToolsList()` will still return an empty list until Task 3 registers a catalog.

- [ ] **Step 1: Add the include**

In `src/app/appfactory.cpp`, alongside the other `framework/` includes near the top:

```cpp
#include "framework/rcommand/rcommandmodule.h"
#include "framework/rcontrol/rcontrolmodule.h"
```

(insert directly after the existing `rcommandmodule.h` include — they're the same subsystem family, keep them adjacent.)

- [ ] **Step 2: Add the module instantiation**

In `AppFactory::newGuiApp()`, directly after the existing line:

```cpp
    app->addModule(new muse::rcommand::RCommandModule());
```

add:

```cpp
    app->addModule(new muse::rcontrol::RControlModule());
```

- [ ] **Step 3: Rebuild and verify the port opens**

```bash
cmake --build build/audacity-debug --target Audacity4.exe -j
src/app/bin/Audacity4.exe &
sleep 2
netstat -an | grep 2212
```

Expected (once the companion `muse_framework` PR's `enabled` gate — see Plan B Task 2 — is *not yet applied*): port 2212 listening. This confirms the wiring works. **Once Plan B's `enabled`-gate lands, re-run this check and expect it closed by default** (that's the point of Plan B Task 2) — this step is only a wiring sanity check for right now, before the gate exists.

- [ ] **Step 4: Commit**

```bash
git add src/app/appfactory.cpp
git commit -m "Instantiate RControlModule so the MCP server can run"
```

---

### Task 3: `AudacityCommandsRegister` — the command catalog

**Files:**
- Create: `src/mcp/internal/audacitycommandsregister.h`
- Create: `src/mcp/internal/audacitycommandsregister.cpp`
- Test: `src/mcp/tests/audacitycommandsregister_tests.cpp`

**Interfaces:**
- Consumes: `muse::rcommand::IModuleCommandsRegister` (framework interface, `muse/framework/rcommand/imodulecommandsregister.h`); `muse::rcommand::CommandInfo`/`Command`/`InputSchema`/`Arg`/`DataType`/`Decoration` (`muse/framework/rcommand/commandtypes.h`).
- Produces: `au::mcp::AudacityCommandsRegister`, consumed by Task 5 (`McpModule`).

Command set for v1 — six commands proven to exist as real, already-wired Audacity action codes (confirmed via `src/playback/internal/playbackuiactions.cpp` and `src/project/internal/projectactionscontroller.cpp`), enough to prove the pipeline end-to-end without hand-declaring the entire action set:

| MCP command | Existing Audacity `ActionCode` | Args |
|---|---|---|
| `command://mcp/play-stop` | `PLAYBACK_TOGGLE_PLAY_STOP_QUERY.toString()` | none |
| `command://mcp/pause` | `PLAYBACK_PAUSE_QUERY.toString()` | none |
| `command://mcp/stop` | `PLAYBACK_STOP_QUERY.toString()` | none |
| `command://mcp/rewind-start` | `PLAYBACK_REWIND_START_QUERY.toString()` | none |
| `command://mcp/new-project` | `"file-new"` | none |
| `command://mcp/save-project` | `"file-save"` | none |

- [ ] **Step 1: Write the failing test**

```cpp
// src/mcp/tests/audacitycommandsregister_tests.cpp
/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Audacity-CLA-applies
 *
 * Audacity
 * A Digital Audio Editor
 *
 * Copyright (C) 2026 Audacity BVBA and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include <gtest/gtest.h>

#include "../internal/audacitycommandsregister.h"

using namespace muse::rcommand;
using namespace au::mcp;

namespace au::mcp {
class AudacityCommandsRegisterTests : public ::testing::Test
{
public:
    AudacityCommandsRegister m_register;
};

TEST_F(AudacityCommandsRegisterTests, ModuleNameIsMcp)
{
    EXPECT_EQ(m_register.moduleName(), "mcp");
}

TEST_F(AudacityCommandsRegisterTests, CommandListHasSixEntries)
{
    EXPECT_EQ(m_register.commandList().size(), size_t(6));
}

TEST_F(AudacityCommandsRegisterTests, PlayStopCommandInfoIsPopulated)
{
    const Command playStop("command://mcp/play-stop");

    const auto& infos = m_register.commandInfoList();
    auto it = std::find_if(infos.begin(), infos.end(), [&](const CommandInfo& info) {
        return info.command == playStop;
    });

    ASSERT_NE(it, infos.end());
    EXPECT_FALSE(it->title.raw().translated().empty());
    EXPECT_FALSE(it->description.translated().empty());
}

TEST_F(AudacityCommandsRegisterTests, CommandListAndCommandInfoListAgree)
{
    EXPECT_EQ(m_register.commandList().size(), m_register.commandInfoList().size());
}
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build/audacity-debug --target mcp_tests -j
./build/bin/mcp_tests --gtest_filter=AudacityCommandsRegisterTests.*
```

Expected: FAIL — `audacitycommandsregister.h` doesn't exist yet, compile error.

- [ ] **Step 3: Write the header**

```cpp
// src/mcp/internal/audacitycommandsregister.h
/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Audacity-CLA-applies
 *
 * Audacity
 * A Digital Audio Editor
 *
 * Copyright (C) 2026 Audacity BVBA and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef AU_MCP_AUDACITYCOMMANDSREGISTER_H
#define AU_MCP_AUDACITYCOMMANDSREGISTER_H

#include "framework/rcommand/imodulecommandsregister.h"

namespace au::mcp {
class AudacityCommandsRegister : public muse::rcommand::IModuleCommandsRegister
{
public:
    AudacityCommandsRegister() = default;

    std::string moduleName() const override;

    const std::vector<muse::rcommand::Command>& commandList() const override;
    const std::vector<muse::rcommand::CommandInfo>& commandInfoList() const override;
};
}

#endif // AU_MCP_AUDACITYCOMMANDSREGISTER_H
```

- [ ] **Step 4: Write the implementation**

```cpp
// src/mcp/internal/audacitycommandsregister.cpp
/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Audacity-CLA-applies
 *
 * Audacity
 * A Digital Audio Editor
 *
 * Copyright (C) 2026 Audacity BVBA and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "audacitycommandsregister.h"

using namespace muse;
using namespace muse::rcommand;
using namespace au::mcp;

static const std::vector<CommandInfo> s_commandInfos = {
    CommandInfo{
        Command("command://mcp/play-stop"),
        TranslatableString("mcp", "Play/Stop toggle"),
        TranslatableString("mcp", "Toggle playback of the current project"),
        InputSchema(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/pause"),
        TranslatableString("mcp", "Pause"),
        TranslatableString("mcp", "Pause playback"),
        InputSchema(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/stop"),
        TranslatableString("mcp", "Stop"),
        TranslatableString("mcp", "Stop playback"),
        InputSchema(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/rewind-start"),
        TranslatableString("mcp", "Rewind to start"),
        TranslatableString("mcp", "Move the playback cursor to the start of the project"),
        InputSchema(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/new-project"),
        TranslatableString("mcp", "New project"),
        TranslatableString("mcp", "Create a new, empty project"),
        InputSchema(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/save-project"),
        TranslatableString("mcp", "Save project"),
        TranslatableString("mcp", "Save the current project"),
        InputSchema(),
        Decoration()
    },
};

std::string AudacityCommandsRegister::moduleName() const
{
    return "mcp";
}

const std::vector<Command>& AudacityCommandsRegister::commandList() const
{
    static std::vector<Command> commands;
    if (commands.empty()) {
        commands.reserve(s_commandInfos.size());
        for (const auto& info : s_commandInfos) {
            commands.push_back(info.command);
        }
    }
    return commands;
}

const std::vector<CommandInfo>& AudacityCommandsRegister::commandInfoList() const
{
    return s_commandInfos;
}
```

- [ ] **Step 5: Run test to verify it passes**

```bash
cmake --build build/audacity-debug --target mcp_tests -j
./build/bin/mcp_tests --gtest_filter=AudacityCommandsRegisterTests.*
```

Expected: PASS (4/4).

- [ ] **Step 6: Commit**

```bash
git add src/mcp/internal/audacitycommandsregister.h src/mcp/internal/audacitycommandsregister.cpp src/mcp/tests/audacitycommandsregister_tests.cpp
git commit -m "Add AudacityCommandsRegister: v1 catalog of 6 MCP-exposed commands"
```

---

### Task 4: `AudacityCommandsController` — execution wiring

**Files:**
- Create: `src/mcp/internal/audacitycommandscontroller.h`
- Create: `src/mcp/internal/audacitycommandscontroller.cpp`
- Create: `src/mcp/tests/mocks/commanddispatchermock.h`
- Test: `src/mcp/tests/audacitycommandscontroller_tests.cpp`

**Interfaces:**
- Consumes: `muse::rcommand::Commandable` (`muse/framework/rcommand/commandable.h`), `muse::rcommand::ICommandDispatcher` (`muse/framework/rcommand/icommanddispatcher.h`), `muse::actions::IActionsDispatcher` (`muse/framework/actions/iactionsdispatcher.h`) — the exact same `ContextInject<...>` pattern already used in `src/appshell/internal/applicationactioncontroller.h`. Reuses `AudacityCommandsRegister::commandList()` from Task 3 to avoid duplicating the command→action mapping in two places.
- Produces: `au::mcp::AudacityCommandsController`, consumed by Task 5.

No existing `ICommandDispatcher` gmock exists anywhere in the `muse` submodule (confirmed: `framework/rcommand` has no `tests/mocks/` directory) — this task adds a local one, scoped to `src/mcp/tests/mocks/`, rather than touching the companion `muse_framework` repo for test-only infra.

- [ ] **Step 1: Write the mock**

```cpp
// src/mcp/tests/mocks/commanddispatchermock.h
/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Audacity-CLA-applies
 *
 * Audacity
 * A Digital Audio Editor
 *
 * Copyright (C) 2026 Audacity BVBA and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef AU_MCP_TESTS_COMMANDDISPATCHERMOCK_H
#define AU_MCP_TESTS_COMMANDDISPATCHERMOCK_H

#include <gmock/gmock.h>

#include "framework/rcommand/icommanddispatcher.h"

namespace au::mcp {
class CommandDispatcherMock : public muse::rcommand::ICommandDispatcher
{
public:
    MOCK_METHOD(muse::async::Promise<muse::rcommand::Response>, dispatch, (const muse::rcommand::Request&), (override));
    MOCK_METHOD(void, onRequest, (muse::rcommand::Commandable*, const muse::rcommand::Command&, const CallBack&), (override));
    MOCK_METHOD(void, unreg, (muse::rcommand::Commandable*), (override));
};
}

#endif // AU_MCP_TESTS_COMMANDDISPATCHERMOCK_H
```

- [ ] **Step 2: Write the failing test**

```cpp
// src/mcp/tests/audacitycommandscontroller_tests.cpp
/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Audacity-CLA-applies
 *
 * Audacity
 * A Digital Audio Editor
 *
 * Copyright (C) 2026 Audacity BVBA and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "actions/tests/mocks/actionsdispatchermock.h"
#include "mocks/commanddispatchermock.h"

#include "../internal/audacitycommandscontroller.h"

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Invoke;

using namespace muse;
using namespace muse::rcommand;
using namespace au::mcp;

namespace au::mcp {
class AudacityCommandsControllerTests : public ::testing::Test
{
public:
    void SetUp() override
    {
        m_controller = new AudacityCommandsController(muse::modularity::globalCtx());

        m_actionsDispatcher = std::make_shared<NiceMock<muse::actions::ActionsDispatcherMock> >();
        m_controller->dispatcher.set(m_actionsDispatcher);

        m_commandDispatcher = std::make_shared<NiceMock<CommandDispatcherMock> >();
        m_controller->commandDispatcher.set(m_commandDispatcher);

        ON_CALL(*m_commandDispatcher, onRequest(_, _, _))
        .WillByDefault(Invoke([this](Commandable*, const Command& command, const ICommandDispatcher::CallBack& cb) {
            m_registered[command] = cb;
        }));

        m_controller->init();
    }

    AudacityCommandsController* m_controller = nullptr;
    std::shared_ptr<muse::actions::ActionsDispatcherMock> m_actionsDispatcher;
    std::shared_ptr<CommandDispatcherMock> m_commandDispatcher;
    std::map<Command, ICommandDispatcher::CallBack> m_registered;
};

TEST_F(AudacityCommandsControllerTests, RegistersAllSixCommands)
{
    EXPECT_EQ(m_registered.size(), size_t(6));
}

TEST_F(AudacityCommandsControllerTests, PlayStopDispatchesTheRightActionCode)
{
    EXPECT_CALL(*m_actionsDispatcher, dispatch(muse::actions::ActionCode("play-stop"), _)).Times(1);

    auto it = m_registered.find(Command("command://mcp/play-stop"));
    ASSERT_NE(it, m_registered.end());

    Request req = make_request(Command("command://mcp/play-stop"), {});
    it->second(req);
}
}
```

- [ ] **Step 3: Run test to verify it fails**

```bash
cmake --build build/audacity-debug --target mcp_tests -j
./build/bin/mcp_tests --gtest_filter=AudacityCommandsControllerTests.*
```

Expected: FAIL — `audacitycommandscontroller.h` doesn't exist yet.

- [ ] **Step 4: Confirm the real action code for play/stop**

Before wiring, check the actual constant value (used `"play-stop"` above as a placeholder guess — verify against source):

```bash
grep -rn "PLAYBACK_TOGGLE_PLAY_STOP_QUERY\s*=" src/playback/
```

Use whatever string that grep returns as the literal `ActionCode` in Step 5 below — do not guess further; this plan's earlier research did not capture the exact string literal, only the symbol name, so this lookup is a required, blocking part of this step.

- [ ] **Step 5: Write the header**

```cpp
// src/mcp/internal/audacitycommandscontroller.h
/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Audacity-CLA-applies
 *
 * Audacity
 * A Digital Audio Editor
 *
 * Copyright (C) 2026 Audacity BVBA and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef AU_MCP_AUDACITYCOMMANDSCONTROLLER_H
#define AU_MCP_AUDACITYCOMMANDSCONTROLLER_H

#include "framework/global/modularity/ioc.h"
#include "framework/actions/iactionsdispatcher.h"
#include "framework/rcommand/commandable.h"
#include "framework/rcommand/icommanddispatcher.h"

namespace au::mcp {
class AudacityCommandsController : public muse::rcommand::Commandable, public muse::Contextable
{
public:
    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher { this };
    muse::ContextInject<muse::rcommand::ICommandDispatcher> commandDispatcher { this };

    AudacityCommandsController(const muse::modularity::ContextPtr& ctx)
        : muse::Contextable(ctx) {}

    void init();

private:
    void registerCommand(const muse::rcommand::Command& command, const muse::actions::ActionCode& actionCode);
};
}

#endif // AU_MCP_AUDACITYCOMMANDSCONTROLLER_H
```

- [ ] **Step 6: Write the implementation**

Replace `"play-stop"`, `"pause"`, `"stop"`, `"rewind-start"` below with whatever Step 4's grep actually returned for each of the four playback constants (`PLAYBACK_TOGGLE_PLAY_STOP_QUERY`, `PLAYBACK_PAUSE_QUERY`, `PLAYBACK_STOP_QUERY`, `PLAYBACK_REWIND_START_QUERY`) — this plan captured the symbol names, not their literal string values, so this substitution is mandatory before the code below will be correct:

```cpp
// src/mcp/internal/audacitycommandscontroller.cpp
/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Audacity-CLA-applies
 *
 * Audacity
 * A Digital Audio Editor
 *
 * Copyright (C) 2026 Audacity BVBA and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "audacitycommandscontroller.h"

using namespace muse;
using namespace muse::rcommand;
using namespace muse::actions;
using namespace au::mcp;

void AudacityCommandsController::init()
{
    registerCommand(Command("command://mcp/play-stop"), "play-stop");           // TODO(step 4): confirm literal
    registerCommand(Command("command://mcp/pause"), "pause");                   // TODO(step 4): confirm literal
    registerCommand(Command("command://mcp/stop"), "stop");                     // TODO(step 4): confirm literal
    registerCommand(Command("command://mcp/rewind-start"), "rewind-start");     // TODO(step 4): confirm literal
    registerCommand(Command("command://mcp/new-project"), "file-new");
    registerCommand(Command("command://mcp/save-project"), "file-save");
}

void AudacityCommandsController::registerCommand(const Command& command, const ActionCode& actionCode)
{
    commandDispatcher()->onRequest(this, command, [this, actionCode](const Request& request) -> Response {
        dispatcher()->dispatch(actionCode);
        return make_response(request, make_ok());
    });
}
```

(The `// TODO(step 4)` comments are a deliberate marker for this plan step, not a "no placeholders" violation — they must all be resolved, and the test in Step 2 will fail against `ActionsDispatcherMock` expectations until they are, which is exactly how you'll know Step 4 was done correctly.)

- [ ] **Step 7: Run test to verify it passes**

```bash
cmake --build build/audacity-debug --target mcp_tests -j
./build/bin/mcp_tests --gtest_filter=AudacityCommandsControllerTests.*
```

Expected: PASS (2/2), with no `// TODO(step 4)` comments remaining in the `.cpp`.

- [ ] **Step 8: Commit**

```bash
git add src/mcp/internal/audacitycommandscontroller.h src/mcp/internal/audacitycommandscontroller.cpp src/mcp/tests/mocks/commanddispatchermock.h src/mcp/tests/audacitycommandscontroller_tests.cpp
git commit -m "Add AudacityCommandsController: bridges rcommand dispatch to existing actions"
```

---

### Task 5: `McpModule` — DI glue and CMake wiring

**Files:**
- Create: `src/mcp/mcpmodule.h`
- Create: `src/mcp/mcpmodule.cpp`
- Create: `src/mcp/CMakeLists.txt`
- Create: `src/mcp/tests/CMakeLists.txt`
- Modify: `src/CMakeLists.txt`
- Modify: `src/app/appfactory.cpp` (from Task 2 — add `au::mcp::McpModule`)

**Interfaces:**
- Consumes: `AudacityCommandsRegister` (Task 3), `AudacityCommandsController` (Task 4), `muse::rcommand::ICommandsRegister` (framework global export, resolved via `globalIoc()->resolve<...>(mname)`, same pattern as `PlaybackModule::onInit()`).
- Produces: nothing further — this is the leaf of the dependency chain.

- [ ] **Step 1: Write `mcpmodule.h`**

```cpp
// src/mcp/mcpmodule.h
/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Audacity-CLA-applies
 *
 * Audacity
 * A Digital Audio Editor
 *
 * Copyright (C) 2026 Audacity BVBA and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef AU_MCP_MCPMODULE_H
#define AU_MCP_MCPMODULE_H

#include "framework/global/modularity/imodulesetup.h"

namespace au::mcp {
class AudacityCommandsController;

class McpModule : public muse::modularity::IModuleSetup
{
public:
    std::string moduleName() const override;

    void registerExports() override;
    void onInit(const muse::IApplication::RunMode& mode) override;
    void onDeinit() override;

private:
    std::shared_ptr<AudacityCommandsController> m_commandsController;
};
}

#endif // AU_MCP_MCPMODULE_H
```

- [ ] **Step 2: Write `mcpmodule.cpp`**

```cpp
// src/mcp/mcpmodule.cpp
/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Audacity-CLA-applies
 *
 * Audacity
 * A Digital Audio Editor
 *
 * Copyright (C) 2026 Audacity BVBA and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "mcpmodule.h"

#include "framework/global/modularity/ioc.h"
#include "framework/rcommand/icommandsregister.h"

#include "internal/audacitycommandsregister.h"
#include "internal/audacitycommandscontroller.h"

using namespace muse;
using namespace muse::modularity;
using namespace au::mcp;

static const std::string module_name("mcp");

std::string McpModule::moduleName() const
{
    return module_name;
}

void McpModule::registerExports()
{
    m_commandsController = std::make_shared<AudacityCommandsController>(iocContext());
}

void McpModule::onInit(const IApplication::RunMode& mode)
{
    if (mode != IApplication::RunMode::GuiApp) {
        return;
    }

    auto commandsRegister = globalIoc()->resolve<rcommand::ICommandsRegister>(module_name);
    if (commandsRegister) {
        commandsRegister->reg(std::make_shared<AudacityCommandsRegister>());
    }

    m_commandsController->init();
}

void McpModule::onDeinit()
{
    auto commandsRegister = globalIoc()->resolve<rcommand::ICommandsRegister>(module_name);
    if (commandsRegister) {
        commandsRegister->unreg(commandsRegister->moduleRegister("mcp"));
    }
}
```

- [ ] **Step 3: Write `src/mcp/CMakeLists.txt`** (modeled on `src/record/CMakeLists.txt` — smallest comparable existing module)

```cmake
# SPDX-License-Identifier: GPL-3.0-only
# Audacity-CLA-applies
#
# Audacity
# A Digital Audio Editor
#
# Copyright (C) 2026 Audacity BVBA and others
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 3 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

declare_module(mcp)

set(MODULE_SRC
    ${CMAKE_CURRENT_LIST_DIR}/mcpmodule.cpp
    ${CMAKE_CURRENT_LIST_DIR}/mcpmodule.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/audacitycommandsregister.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/audacitycommandsregister.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/audacitycommandscontroller.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/audacitycommandscontroller.h
)

setup_module()

if (AU_BUILD_MCP_TESTS)
    add_subdirectory(tests)
endif()
```

Note: verify `declare_module`/`setup_module` are the actual macro names by checking `src/record/CMakeLists.txt` before using this verbatim — Audacity's module CMake boilerplate wasn't directly inspected in this research pass, only the higher-level `add_subdirectory` list in `src/CMakeLists.txt` and the muse-side `muse_create_module` macro (which is for the *framework*, not Audacity's own modules — do not use `muse_create_module` here). This is the one piece of this plan not verified against real source; confirm against a sibling module's actual `CMakeLists.txt` before writing this file for real.

- [ ] **Step 4: Wire into `src/CMakeLists.txt`**

Add, alongside the existing `add_subdirectory(record)` block:

```cmake
add_subdirectory(mcp)
```

- [ ] **Step 5: Instantiate `McpModule` in `appfactory.cpp`**

Add the include near the other Audacity-module includes:

```cpp
#include "mcp/mcpmodule.h"
```

Add, directly after `app->addModule(new muse::rcontrol::RControlModule());` from Task 2:

```cpp
    app->addModule(new au::mcp::McpModule());
```

- [ ] **Step 6: Full rebuild**

```bash
cmake --build build/audacity-debug --target Audacity4.exe -j
cmake --build build/audacity-debug --target mcp_tests -j
./build/bin/mcp_tests
```

Expected: app builds, all `mcp_tests` pass.

- [ ] **Step 7: Manual end-to-end smoke test**

This requires the companion `muse_framework` PR's fixes (Plan B) to be applied locally first (either via a local patch to the `muse` submodule checkout, or once that PR merges upstream) — without them, `tools/call` won't report results and `inputSchema` will be empty, but `tools/list` alone is enough to prove Tasks 2–5 work:

```bash
src/app/bin/Audacity4.exe &
sleep 3
echo '{"jsonrpc":"2.0","id":1,"method":"tools/list"}' | ./build/bin/musescore-mcpbridge 127.0.0.1 2212
```

Expected: a JSON response listing 6 tools named `mcp_play-stop`, `mcp_pause`, `mcp_stop`, `mcp_rewind-start`, `mcp_new-project`, `mcp_save-project` (per `commandToToolName()`'s `/` → `_` substitution in `mcpcontroller.cpp`).

- [ ] **Step 8: Commit**

```bash
git add src/mcp/mcpmodule.h src/mcp/mcpmodule.cpp src/mcp/CMakeLists.txt src/mcp/tests/CMakeLists.txt src/CMakeLists.txt src/app/appfactory.cpp
git commit -m "Wire McpModule into the app: registers the MCP command catalog and dispatch"
```

---

## Self-Review

**Spec coverage:** Task 1 verifies the starting state; Task 2 starts the server; Tasks 3–4 give it a real catalog and real execution; Task 5 wires it all into the app and proves it end-to-end. Preferences UI (the actual checkbox a user clicks) is explicitly out of scope for this plan — the `enabled` gate and its default live in the companion `muse_framework` PR (Plan B, Task 2); this repo has nothing to add there since Audacity has no settings key of its own to declare. If a Preferences checkbox is wanted before shipping to end users, it needs its own follow-up plan once Plan B's `IMcpConfiguration` export exists to bind against.

**Placeholder scan:** the two `// TODO(step 4)` markers in Task 4 Step 6 are intentional and explicitly resolved by Task 4 Step 4 — flagged inline as not a violation, since the plan shows the actual mechanism (grep the real source) rather than saying "add appropriate action codes." The CMake macro names in Task 5 Step 3 are flagged as unverified with an explicit instruction on how to verify — this is disclosure, not a placeholder, since the plan does not claim it's confirmed.

**Type consistency:** `Command`, `CommandInfo`, `ActionCode`, `ICommandDispatcher::CallBack`, `Request`/`Response` are used identically across Tasks 3–5, matching the real framework signatures pulled from `commandtypes.h`, `icommanddispatcher.h`, and `imodulecommandsregister.h`.
