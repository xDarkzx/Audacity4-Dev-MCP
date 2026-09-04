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

#include <deque>
#include <chrono>

#include "framework/global/modularity/ioc.h"
#include "framework/actions/iactionsdispatcher.h"
#include "framework/rcommand/commandable.h"
#include "framework/rcommand/icommanddispatcher.h"
#include "playback/iplaybackcontroller.h"
#include "project/iprojectfilescontroller.h"
#include "context/iglobalcontext.h"
#include "trackedit/ilabelsinteraction.h"
#include "trackedit/itrackeditinteraction.h"
#include "trackedit/iselectioncontroller.h"
#include "effects/effects_base/ieffectexecutionscenario.h"
#include "importexport/export/iexporter.h"
#include "framework/global/io/ifilesystem.h"

namespace au::mcp {
struct CommandHistoryEntry {
    std::string id;
    std::string command;
    std::chrono::system_clock::time_point timestamp;
    bool success = false;
    std::string message;
};

class AudacityCommandsController : public muse::rcommand::Commandable, public muse::Contextable
{
public:
    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher { this };
    muse::ContextInject<muse::rcommand::ICommandDispatcher> commandDispatcher { this };
    muse::ContextInject<playback::IPlaybackController> playbackController { this };
    muse::ContextInject<project::IProjectFilesController> projectFilesController { this };
    muse::ContextInject<context::IGlobalContext> globalContext { this };
    muse::ContextInject<au::trackedit::ILabelsInteraction> labelsInteraction { this };
    muse::ContextInject<au::trackedit::ITrackeditInteraction> trackeditInteraction { this };
    muse::ContextInject<au::trackedit::ISelectionController> selectionController { this };
    muse::ContextInject<au::effects::IEffectExecutionScenario> effectExecutionScenario { this };
    muse::ContextInject<au::importexport::IExporter> exporter { this };
    muse::GlobalInject<muse::io::IFileSystem> fileSystem;

    AudacityCommandsController(const muse::modularity::ContextPtr& ctx)
        : muse::Contextable(ctx) {}

    void init();

private:
    using Handler = std::function<muse::rcommand::Response (const muse::rcommand::Request&)>;

    void registerCommand(const muse::rcommand::Command& command, const Handler& handler);
    void recordHistory(const std::string& id, const std::string& command, bool success, const std::string& message);
    void stopPlaybackIfRunning();

    muse::rcommand::Response handlePlaybackAction(const muse::rcommand::Request& request,
                                                   const muse::actions::ActionQuery& actionQuery,
                                                   const std::string& commandName);
    muse::rcommand::Response handleNewProject(const muse::rcommand::Request& request);
    muse::rcommand::Response handleSaveProject(const muse::rcommand::Request& request);
    muse::rcommand::Response handleRecentCommands(const muse::rcommand::Request& request);
    muse::rcommand::Response handleCommandStatus(const muse::rcommand::Request& request);

    muse::rcommand::Response handleListLabels(const muse::rcommand::Request& request);
    muse::rcommand::Response handleAddLabelTrack(const muse::rcommand::Request& request);
    muse::rcommand::Response handleAddLabel(const muse::rcommand::Request& request);
    muse::rcommand::Response handleRemoveLabel(const muse::rcommand::Request& request);
    muse::rcommand::Response handleUpdateLabelText(const muse::rcommand::Request& request);

    muse::rcommand::Response handleApplyEffect(const muse::rcommand::Request& request);
    muse::rcommand::Response handleSelectAll(const muse::rcommand::Request& request);
    muse::rcommand::Response handleSelectTime(const muse::rcommand::Request& request);
    muse::rcommand::Response handleExportWav(const muse::rcommand::Request& request);

    //! NOTE Returns the id of the first label track found, or -1 if there is no
    //! label track (or no open project). v1 only supports a single label track;
    //! callers with multiple label tracks will always get the first one.
    au::trackedit::TrackId findFirstLabelTrack() const;

    //! NOTE Every ITrackeditInteraction/ILabelsInteraction call assumes a project is
    //! open and asserts internally (crashing the whole app, see handleAddLabelTrack's
    //! call site history) if it isn't. Always check this before calling into either
    //! interface from a handler that doesn't already go through findFirstLabelTrack().
    bool hasOpenProject() const;

    static const size_t HISTORY_LIMIT = 200;
    std::deque<CommandHistoryEntry> m_history;
};
}

#endif // AU_MCP_AUDACITYCOMMANDSCONTROLLER_H
