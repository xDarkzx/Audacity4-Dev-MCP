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
#include "effects/effects_base/ieffectsprovider.h"
#include "effects/effects_base/irealtimeeffectservice.h"
#include "effects/effects_base/ieffectparametersprovider.h"
#include "effects/effects_base/ieffectinstancesregister.h"
#include "effects/effects_base/ieffectpresetsprovider.h"
#include "importexport/export/iexporter.h"
#include "importexport/import/iimporter.h"
#include "playback/itrackplaybackcontrol.h"
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
    muse::GlobalInject<au::effects::IEffectsProvider> effectsProvider;
    muse::ContextInject<au::effects::IRealtimeEffectService> realtimeEffectService { this };
    muse::ContextInject<au::effects::IEffectParametersProvider> effectParametersProvider { this };
    muse::GlobalInject<au::effects::IEffectInstancesRegister> effectInstancesRegister;
    muse::ContextInject<au::effects::IEffectPresetsProvider> effectPresetsProvider { this };
    muse::ContextInject<au::importexport::IExporter> exporter { this };
    muse::ContextInject<au::importexport::IImporter> importer { this };
    muse::ContextInject<au::playback::ITrackPlaybackControl> trackPlaybackControl { this };
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
    muse::rcommand::Response handleUpdateLabelTime(const muse::rcommand::Request& request);

    muse::rcommand::Response handleApplyEffect(const muse::rcommand::Request& request);
    muse::rcommand::Response handleSelectAll(const muse::rcommand::Request& request);
    muse::rcommand::Response handleSelectTime(const muse::rcommand::Request& request);
    muse::rcommand::Response handleExportWav(const muse::rcommand::Request& request);
    muse::rcommand::Response handleSelectNone(const muse::rcommand::Request& request);
    muse::rcommand::Response handleSelectTracks(const muse::rcommand::Request& request);

    muse::rcommand::Response handleTrackAddMono(const muse::rcommand::Request& request);
    muse::rcommand::Response handleTrackAddStereo(const muse::rcommand::Request& request);
    muse::rcommand::Response handleTrackRemove(const muse::rcommand::Request& request);
    muse::rcommand::Response handleTrackSetProperties(const muse::rcommand::Request& request);
    muse::rcommand::Response handleTrackDuplicate(const muse::rcommand::Request& request);
    muse::rcommand::Response handleTrackResample(const muse::rcommand::Request& request);
    muse::rcommand::Response handleTrackMuteAll(const muse::rcommand::Request& request);
    muse::rcommand::Response handleTrackUnmuteAll(const muse::rcommand::Request& request);
    muse::rcommand::Response handleTrackMuteOrUnmuteAll(const muse::rcommand::Request& request, bool mute);

    muse::rcommand::Response handleSetClipPitch(const muse::rcommand::Request& request);
    muse::rcommand::Response handleResetClipPitch(const muse::rcommand::Request& request);
    muse::rcommand::Response handleSetClipSpeed(const muse::rcommand::Request& request);
    muse::rcommand::Response handleResetClipSpeed(const muse::rcommand::Request& request);
    muse::rcommand::Response handleRenderClipPitchSpeed(const muse::rcommand::Request& request);
    muse::rcommand::Response handleResetClipPitchSpeed(const muse::rcommand::Request& request);
    muse::rcommand::Response handleSplitClipAtSilences(const muse::rcommand::Request& request);
    muse::rcommand::Response handleSplitRangeAtSilences(const muse::rcommand::Request& request);
    muse::rcommand::Response handleTrimClip(const muse::rcommand::Request& request);
    muse::rcommand::Response handleStretchClip(const muse::rcommand::Request& request);
    muse::rcommand::Response handleNearestZeroCrossing(const muse::rcommand::Request& request);
    muse::rcommand::Response handleSetClipColor(const muse::rcommand::Request& request);
    muse::rcommand::Response handleSetTrackColor(const muse::rcommand::Request& request);

    muse::rcommand::Response handleEditCut(const muse::rcommand::Request& request);
    muse::rcommand::Response handleEditCopy(const muse::rcommand::Request& request);
    muse::rcommand::Response handleEditPaste(const muse::rcommand::Request& request);
    muse::rcommand::Response handleEditDelete(const muse::rcommand::Request& request);
    muse::rcommand::Response handleEditSplit(const muse::rcommand::Request& request);
    muse::rcommand::Response handleEditTrim(const muse::rcommand::Request& request);
    muse::rcommand::Response handleEditSilence(const muse::rcommand::Request& request);
    muse::rcommand::Response handleEditDuplicate(const muse::rcommand::Request& request);
    muse::rcommand::Response handleEditUndo(const muse::rcommand::Request& request);
    muse::rcommand::Response handleEditRedo(const muse::rcommand::Request& request);
    muse::rcommand::Response handleEditSplitNew(const muse::rcommand::Request& request);
    muse::rcommand::Response handleEditSplitCut(const muse::rcommand::Request& request);
    muse::rcommand::Response handleEditSplitDelete(const muse::rcommand::Request& request);
    muse::rcommand::Response handleEditDisjoin(const muse::rcommand::Request& request);
    muse::rcommand::Response handleEditJoin(const muse::rcommand::Request& request);
    muse::rcommand::Response handleSelectZeroCrossing(const muse::rcommand::Request& request);

    muse::rcommand::Response handleProjectOpen(const muse::rcommand::Request& request);
    muse::rcommand::Response handleProjectImport(const muse::rcommand::Request& request);
    muse::rcommand::Response handleProjectClose(const muse::rcommand::Request& request);
    muse::rcommand::Response handleProjectSaveAs(const muse::rcommand::Request& request);
    muse::rcommand::Response handleProjectExport(const muse::rcommand::Request& request);

    muse::rcommand::Response handleTransportRecord(const muse::rcommand::Request& request);
    muse::rcommand::Response handleCursorSet(const muse::rcommand::Request& request);

    muse::rcommand::Response handleProjectGetInfo(const muse::rcommand::Request& request);
    muse::rcommand::Response handleTrackGetInfo(const muse::rcommand::Request& request);
    muse::rcommand::Response handleSelectClip(const muse::rcommand::Request& request);
    muse::rcommand::Response handleTransportGetPlayPosition(const muse::rcommand::Request& request);
    muse::rcommand::Response handleListEffects(const muse::rcommand::Request& request);
    muse::rcommand::Response handleAddRealtimeEffect(const muse::rcommand::Request& request);
    muse::rcommand::Response handleListRealtimeEffects(const muse::rcommand::Request& request);
    muse::rcommand::Response handleRemoveRealtimeEffect(const muse::rcommand::Request& request);
    muse::rcommand::Response handleSetRealtimeEffectActive(const muse::rcommand::Request& request);
    muse::rcommand::Response handleListEffectParameters(const muse::rcommand::Request& request);
    muse::rcommand::Response handleSetEffectParameter(const muse::rcommand::Request& request);
    muse::rcommand::Response handleListEffectPresets(const muse::rcommand::Request& request);
    muse::rcommand::Response handleApplyEffectPreset(const muse::rcommand::Request& request);

    //! NOTE track_id=-2 addresses the Master bus (IRealtimeEffectService::masterTrackId) -
    //! valid for all four realtime-effect handlers above, not just per-track ids.
    au::effects::RealtimeEffectStatePtr realtimeEffectAt(au::trackedit::TrackId trackId, int index) const;

    //! NOTE Returns -1 (an invalid EffectInstanceId) if the state has no
    //! registered instance yet - always check before using.
    au::effects::EffectInstanceId realtimeEffectInstanceId(const au::effects::RealtimeEffectStatePtr& state) const;

    //! NOTE Returns the currently selected tracks, or all tracks if none are
    //! selected (matches how most edit operations should behave: "operate on
    //! selection, or everything if nothing is specifically selected" would be
    //! surprising - so this returns an EMPTY list when nothing is selected
    //! instead, and callers should treat that as an error, not "select all").
    au::trackedit::TrackIdList selectedOrEmptyTracks() const;

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
