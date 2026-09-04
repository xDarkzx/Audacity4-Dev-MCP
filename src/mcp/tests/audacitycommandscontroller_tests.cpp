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
#include "playback/tests/mocks/playbackcontrollermock.h"
#include "project/tests/mocks/projectfilescontrollermock.h"
#include "context/tests/mocks/globalcontextmock.h"
#include "project/tests/mocks/audacityprojectmock.h"
#include "mocks/commanddispatchermock.h"
#include "mocks/effectexecutionscenariomock.h"

#include "../internal/audacitycommandscontroller.h"

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Invoke;
using ::testing::Return;

using namespace muse;
using namespace muse::rcommand;
using namespace au::mcp;
using namespace au::playback;

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

        m_playbackController = std::make_shared<NiceMock<PlaybackControllerMock> >();
        m_controller->playbackController.set(m_playbackController);

        m_projectFilesController = std::make_shared<NiceMock<project::ProjectFilesControllerMock> >();
        m_controller->projectFilesController.set(m_projectFilesController);

        m_globalContext = std::make_shared<NiceMock<context::GlobalContextMock> >();
        m_controller->globalContext.set(m_globalContext);

        m_effectExecutionScenario = std::make_shared<NiceMock<EffectExecutionScenarioMock> >();
        m_controller->effectExecutionScenario.set(m_effectExecutionScenario);

        ON_CALL(*m_commandDispatcher, onRequest(_, _, _))
        .WillByDefault(Invoke([this](Commandable*, const Command& command, const ICommandDispatcher::CallBack& cb) {
            m_registered[command] = cb;
        }));

        m_controller->init();
    }

    void TearDown() override
    {
        delete m_controller;
    }

    Response call(const std::string& command, uint64_t id = 1)
    {
        auto it = m_registered.find(Command(command));
        if (it == m_registered.end()) {
            return Response();
        }
        Request req = make_request(CommandQuery(Command(command)));
        req.callId = id;
        return it->second(req);
    }

    AudacityCommandsController* m_controller = nullptr;
    std::shared_ptr<muse::actions::ActionsDispatcherMock> m_actionsDispatcher;
    std::shared_ptr<CommandDispatcherMock> m_commandDispatcher;
    std::shared_ptr<PlaybackControllerMock> m_playbackController;
    std::shared_ptr<project::ProjectFilesControllerMock> m_projectFilesController;
    std::shared_ptr<context::GlobalContextMock> m_globalContext;
    std::shared_ptr<EffectExecutionScenarioMock> m_effectExecutionScenario;
    std::map<Command, ICommandDispatcher::CallBack> m_registered;
};

TEST_F(AudacityCommandsControllerTests, RegistersAllSixteenCommands)
{
    // new-project is deliberately not registered - see AudacityCommandsController::init()
    EXPECT_EQ(m_registered.size(), size_t(16));
}

TEST_F(AudacityCommandsControllerTests, PlayStopDispatchesAndReportsPlaybackStatus)
{
    EXPECT_CALL(*m_actionsDispatcher, dispatch(muse::actions::ActionQuery("action://playback/toggle-play-stop"))).Times(1);
    ON_CALL(*m_playbackController, playbackStatus()).WillByDefault(Return(PlaybackStatus::Running));

    Response response = call("command://mcp/play-stop");

    EXPECT_TRUE(response.ret.success());
    EXPECT_NE(response.ret.text().find("playing"), std::string::npos);
}

TEST_F(AudacityCommandsControllerTests, SaveProjectReportsErrorWhenNoProjectIsOpen)
{
    ON_CALL(*m_globalContext, currentProject()).WillByDefault(Return(project::IAudacityProjectPtr()));

    Response response = call("command://mcp/save-project");

    EXPECT_FALSE(response.ret.success());
    EXPECT_NE(response.ret.text().find("No project"), std::string::npos);
}

TEST_F(AudacityCommandsControllerTests, SaveProjectRefusesANeverSavedProject)
{
    auto newlyCreatedProject = std::make_shared<::testing::NiceMock<project::AudacityProjectMock> >();
    ON_CALL(*newlyCreatedProject, isNewlyCreated()).WillByDefault(Return(true));
    ON_CALL(*newlyCreatedProject, path()).WillByDefault(Return(muse::io::path_t()));
    ON_CALL(*m_globalContext, currentProject()).WillByDefault(Return(newlyCreatedProject));

    Response response = call("command://mcp/save-project");

    EXPECT_FALSE(response.ret.success());
    EXPECT_NE(response.ret.text().find("never been saved"), std::string::npos);
}

TEST_F(AudacityCommandsControllerTests, SaveProjectReportsNothingToSaveWhenAlreadyClean)
{
    auto openProject = std::make_shared<::testing::NiceMock<project::AudacityProjectMock> >();
    ON_CALL(*openProject, isNewlyCreated()).WillByDefault(Return(false));
    ON_CALL(*openProject, path()).WillByDefault(Return(muse::io::path_t("D:/test.aup4")));
    ON_CALL(*openProject, hasUnsavedChanges()).WillByDefault(Return(false));
    ON_CALL(*m_globalContext, currentProject()).WillByDefault(Return(openProject));

    EXPECT_CALL(*m_actionsDispatcher, dispatch(muse::actions::ActionCode("file-save"))).Times(0);

    Response response = call("command://mcp/save-project");

    EXPECT_TRUE(response.ret.success());
}

TEST_F(AudacityCommandsControllerTests, SaveProjectDispatchesFileSaveAndReportsFailureIfStillUnsaved)
{
    auto openProject = std::make_shared<::testing::NiceMock<project::AudacityProjectMock> >();
    ON_CALL(*openProject, isNewlyCreated()).WillByDefault(Return(false));
    ON_CALL(*openProject, path()).WillByDefault(Return(muse::io::path_t("D:/test.aup4")));
    ON_CALL(*openProject, hasUnsavedChanges()).WillByDefault(Return(true));
    ON_CALL(*m_globalContext, currentProject()).WillByDefault(Return(openProject));

    EXPECT_CALL(*m_actionsDispatcher, dispatch(muse::actions::ActionCode("file-save"))).Times(1);

    Response response = call("command://mcp/save-project");

    EXPECT_FALSE(response.ret.success());
}

TEST_F(AudacityCommandsControllerTests, SaveProjectReportsSuccessWhenChangesClearAfterDispatch)
{
    auto openProject = std::make_shared<::testing::NiceMock<project::AudacityProjectMock> >();
    ON_CALL(*openProject, isNewlyCreated()).WillByDefault(Return(false));
    ON_CALL(*openProject, path()).WillByDefault(Return(muse::io::path_t("D:/test.aup4")));
    ON_CALL(*m_globalContext, currentProject()).WillByDefault(Return(openProject));

    EXPECT_CALL(*openProject, hasUnsavedChanges())
    .WillOnce(Return(true))   // checked before dispatch: there is something to save
    .WillOnce(Return(false)); // checked after dispatch: it saved successfully

    EXPECT_CALL(*m_actionsDispatcher, dispatch(muse::actions::ActionCode("file-save"))).Times(1);

    Response response = call("command://mcp/save-project");

    EXPECT_TRUE(response.ret.success());
}

// NOTE: mcp/new-project is not currently registered (see AudacityCommandsController::init()
// for why) so it has no command-dispatch tests here. handleNewProject()'s logic is still
// implemented pending root-causing the crash found in live testing.

// NOTE: These three guard against a real crash found via live testing + WinDbg analysis of
// the resulting dump: Au3TracksInteraction::projectRef() dereferences currentProject() with
// no null check, so calling any of these with no project open used to segfault instead of
// erroring. The crash was reproducible as a startup race (MCP port opens before Audacity's
// default project is set as current), not just a "never call this with no project" misuse case.
TEST_F(AudacityCommandsControllerTests, AddLabelTrackReportsErrorWhenNoProjectIsOpen)
{
    ON_CALL(*m_globalContext, currentProject()).WillByDefault(Return(project::IAudacityProjectPtr()));

    Response response = call("command://mcp/add-label-track");

    EXPECT_FALSE(response.ret.success());
    EXPECT_NE(response.ret.text().find("No project"), std::string::npos);
}

TEST_F(AudacityCommandsControllerTests, RemoveLabelReportsErrorWhenNoProjectIsOpen)
{
    ON_CALL(*m_globalContext, currentProject()).WillByDefault(Return(project::IAudacityProjectPtr()));

    Request req = make_request(CommandQuery(Command("command://mcp/remove-label")).set("key", "0:1"));
    auto it = m_registered.find(Command("command://mcp/remove-label"));
    ASSERT_NE(it, m_registered.end());
    Response response = it->second(req);

    EXPECT_FALSE(response.ret.success());
    EXPECT_NE(response.ret.text().find("No project"), std::string::npos);
}

TEST_F(AudacityCommandsControllerTests, UpdateLabelTextReportsErrorWhenNoProjectIsOpen)
{
    ON_CALL(*m_globalContext, currentProject()).WillByDefault(Return(project::IAudacityProjectPtr()));

    Request req = make_request(CommandQuery(Command("command://mcp/update-label-text")).set("key", "0:1").set("text", "x"));
    auto it = m_registered.find(Command("command://mcp/update-label-text"));
    ASSERT_NE(it, m_registered.end());
    Response response = it->second(req);

    EXPECT_FALSE(response.ret.success());
    EXPECT_NE(response.ret.text().find("No project"), std::string::npos);
}

TEST_F(AudacityCommandsControllerTests, RecentCommandsRecordsHistoryAfterEachCall)
{
    ON_CALL(*m_projectFilesController, saveProject(_)).WillByDefault(Return(true));
    call("command://mcp/save-project", 42);

    Response history = call("command://mcp/recent-commands", 43);

    ASSERT_TRUE(history.data.has_value());
    const std::string* json = std::any_cast<std::string>(&history.data);
    ASSERT_NE(json, nullptr);
    EXPECT_NE(json->find("\"42\""), std::string::npos);
}

TEST_F(AudacityCommandsControllerTests, CommandStatusFindsAPreviousCommandById)
{
    ON_CALL(*m_projectFilesController, saveProject(_)).WillByDefault(Return(true));
    call("command://mcp/save-project", 99);

    Request req = make_request(CommandQuery(Command("command://mcp/command-status")).set("id", "99"));
    auto it = m_registered.find(Command("command://mcp/command-status"));
    ASSERT_NE(it, m_registered.end());
    Response response = it->second(req);

    EXPECT_TRUE(response.ret.success());
    ASSERT_TRUE(response.data.has_value());
}

TEST_F(AudacityCommandsControllerTests, CommandStatusReportsMissingId)
{
    Request req = make_request(CommandQuery(Command("command://mcp/command-status")).set("id", "does-not-exist"));
    auto it = m_registered.find(Command("command://mcp/command-status"));
    ASSERT_NE(it, m_registered.end());
    Response response = it->second(req);

    EXPECT_FALSE(response.ret.success());
}

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

TEST_F(AudacityCommandsControllerTests, ApplyEffectReportsErrorWhenEffectIdMissing)
{
    auto openProject = std::make_shared<::testing::NiceMock<project::AudacityProjectMock> >();
    ON_CALL(*m_globalContext, currentProject()).WillByDefault(Return(openProject));

    Request req = make_request(CommandQuery(Command("command://mcp/apply-effect")));
    auto it = m_registered.find(Command("command://mcp/apply-effect"));
    ASSERT_NE(it, m_registered.end());
    Response response = it->second(req);

    EXPECT_FALSE(response.ret.success());
    EXPECT_NE(response.ret.text().find("effect_id"), std::string::npos);
}

TEST_F(AudacityCommandsControllerTests, ApplyEffectDispatchesWithParams)
{
    auto openProject = std::make_shared<::testing::NiceMock<project::AudacityProjectMock> >();
    ON_CALL(*m_globalContext, currentProject()).WillByDefault(Return(openProject));

    EXPECT_CALL(*m_effectExecutionScenario, performEffect(muse::String("Normalize"), std::string("PeakLevel=-3.0")))
    .WillOnce(Return(make_ret(Ret::Code::Ok)));

    Request req = make_request(CommandQuery(Command("command://mcp/apply-effect"))
                                .set("effect_id", "Normalize").set("params", "PeakLevel=-3.0"));
    auto it = m_registered.find(Command("command://mcp/apply-effect"));
    ASSERT_NE(it, m_registered.end());
    Response response = it->second(req);

    EXPECT_TRUE(response.ret.success());
}

TEST_F(AudacityCommandsControllerTests, ApplyEffectWithNoParamsStillUsesTitleFallbackOverload)
{
    // Regression test: the one-arg performEffect(effectId) overload skips title-fallback
    // resolution and only accepts a real internal PluginID, not a human title like
    // "Noise reduction" - calling it for a no-params request (e.g. NoiseReduction's
    // profile-capture call) caused "Effect not found", which in turn left the effect
    // applying with no profile ever captured and crashing. The two-arg overload must
    // always be used, even with an empty params string.
    auto openProject = std::make_shared<::testing::NiceMock<project::AudacityProjectMock> >();
    ON_CALL(*m_globalContext, currentProject()).WillByDefault(Return(openProject));

    EXPECT_CALL(*m_effectExecutionScenario, performEffect(muse::String("Noise reduction"), std::string("")))
    .WillOnce(Return(make_ret(Ret::Code::Ok)));
    EXPECT_CALL(*m_effectExecutionScenario, performEffect(muse::String("Noise reduction")))
    .Times(0);

    Request req = make_request(CommandQuery(Command("command://mcp/apply-effect")).set("effect_id", "Noise reduction"));
    auto it = m_registered.find(Command("command://mcp/apply-effect"));
    ASSERT_NE(it, m_registered.end());
    Response response = it->second(req);

    EXPECT_TRUE(response.ret.success());
}

TEST_F(AudacityCommandsControllerTests, SelectAllReportsErrorWhenNoProjectIsOpen)
{
    ON_CALL(*m_globalContext, currentProject()).WillByDefault(Return(project::IAudacityProjectPtr()));

    Response response = call("command://mcp/select-all");

    EXPECT_FALSE(response.ret.success());
    EXPECT_NE(response.ret.text().find("No project"), std::string::npos);
}

TEST_F(AudacityCommandsControllerTests, SelectTimeReportsErrorWhenNoProjectIsOpen)
{
    ON_CALL(*m_globalContext, currentProject()).WillByDefault(Return(project::IAudacityProjectPtr()));

    Request req = make_request(CommandQuery(Command("command://mcp/select-time")).set("start", "0").set("end", "1"));
    auto it = m_registered.find(Command("command://mcp/select-time"));
    ASSERT_NE(it, m_registered.end());
    Response response = it->second(req);

    EXPECT_FALSE(response.ret.success());
    EXPECT_NE(response.ret.text().find("No project"), std::string::npos);
}

TEST_F(AudacityCommandsControllerTests, ExportWavReportsErrorWhenNoProjectIsOpen)
{
    ON_CALL(*m_globalContext, currentProject()).WillByDefault(Return(project::IAudacityProjectPtr()));

    Request req = make_request(CommandQuery(Command("command://mcp/export-wav")).set("path", "C:/tmp/test.wav"));
    auto it = m_registered.find(Command("command://mcp/export-wav"));
    ASSERT_NE(it, m_registered.end());
    Response response = it->second(req);

    EXPECT_FALSE(response.ret.success());
    EXPECT_NE(response.ret.text().find("No project"), std::string::npos);
}
}
