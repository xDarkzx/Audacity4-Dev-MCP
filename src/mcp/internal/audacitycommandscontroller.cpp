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

#include <sstream>
#include <iomanip>

#include "global/serialization/json.h"
#include "trackedit/itrackeditproject.h"

using namespace muse;
using namespace muse::rcommand;
using namespace muse::actions;
using namespace au::mcp;
using namespace au::playback;

static std::string playbackStatusToString(PlaybackStatus status)
{
    switch (status) {
    case PlaybackStatus::Stopped: return "stopped";
    case PlaybackStatus::Paused: return "paused";
    case PlaybackStatus::Running: return "playing";
    }
    return "unknown";
}

static std::string isoTimestamp(const std::chrono::system_clock::time_point& tp)
{
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tmUtc {};
#ifdef _WIN32
    gmtime_s(&tmUtc, &t);
#else
    gmtime_r(&t, &tmUtc);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tmUtc, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

void AudacityCommandsController::init()
{
    // Transport actions are registered elsewhere (PlaybackController) via ActionQuery,
    // not a plain ActionCode - dispatch the same way they were registered, then verify
    // the outcome via playbackStatus() rather than assuming dispatch succeeded.
    registerCommand(Command("command://mcp/play-stop"), [this](const Request& request) {
        return handlePlaybackAction(request, ActionQuery("action://playback/toggle-play-stop"), "play-stop");
    });
    registerCommand(Command("command://mcp/pause"), [this](const Request& request) {
        return handlePlaybackAction(request, ActionQuery("action://playback/pause"), "pause");
    });
    registerCommand(Command("command://mcp/stop"), [this](const Request& request) {
        return handlePlaybackAction(request, ActionQuery("action://playback/stop"), "stop");
    });
    registerCommand(Command("command://mcp/rewind-start"), [this](const Request& request) {
        return handlePlaybackAction(request, ActionQuery("action://playback/rewind-start"), "rewind-start");
    });

    //! NOTE mcp/new-project is intentionally NOT registered here. Live testing found it
    //! triggers a delayed debug-assertion crash in Audacity's own project-replacement code
    //! (reproduced twice, including with a stop-playback-first guard in place). handleNewProject()
    //! is left implemented below for when this is properly root-caused, but is not exposed
    //! as a command until then - do not re-enable without confirming the crash is fixed.

    registerCommand(Command("command://mcp/save-project"), [this](const Request& request) {
        return handleSaveProject(request);
    });

    registerCommand(Command("command://mcp/recent-commands"), [this](const Request& request) {
        return handleRecentCommands(request);
    });
    registerCommand(Command("command://mcp/command-status"), [this](const Request& request) {
        return handleCommandStatus(request);
    });

    registerCommand(Command("command://mcp/list-labels"), [this](const Request& request) {
        return handleListLabels(request);
    });
    registerCommand(Command("command://mcp/add-label-track"), [this](const Request& request) {
        return handleAddLabelTrack(request);
    });
    registerCommand(Command("command://mcp/add-label"), [this](const Request& request) {
        return handleAddLabel(request);
    });
    registerCommand(Command("command://mcp/remove-label"), [this](const Request& request) {
        return handleRemoveLabel(request);
    });
    registerCommand(Command("command://mcp/update-label-text"), [this](const Request& request) {
        return handleUpdateLabelText(request);
    });

    registerCommand(Command("command://mcp/apply-effect"), [this](const Request& request) {
        return handleApplyEffect(request);
    });

    registerCommand(Command("command://mcp/select-all"), [this](const Request& request) {
        return handleSelectAll(request);
    });
    registerCommand(Command("command://mcp/select-time"), [this](const Request& request) {
        return handleSelectTime(request);
    });

    registerCommand(Command("command://mcp/export-wav"), [this](const Request& request) {
        return handleExportWav(request);
    });
}

void AudacityCommandsController::registerCommand(const Command& command, const Handler& handler)
{
    commandDispatcher()->onRequest(this, command, [this, command, handler](const Request& request) -> Response {
        Response response = handler(request);
        recordHistory(std::to_string(request.callId), command.toString(), response.ret.success(), response.ret.text());
        return response;
    });
}

void AudacityCommandsController::recordHistory(const std::string& id, const std::string& command, bool success,
                                                const std::string& message)
{
    CommandHistoryEntry entry;
    entry.id = id;
    entry.command = command;
    entry.timestamp = std::chrono::system_clock::now();
    entry.success = success;
    entry.message = message;

    m_history.push_back(std::move(entry));
    while (m_history.size() > HISTORY_LIMIT) {
        m_history.pop_front();
    }
}

void AudacityCommandsController::stopPlaybackIfRunning()
{
    if (playbackController() && playbackController()->playbackStatus() == PlaybackStatus::Running) {
        dispatcher()->dispatch(ActionQuery("action://playback/stop"));
    }
}

Response AudacityCommandsController::handlePlaybackAction(const Request& request, const ActionQuery& actionQuery,
                                                            const std::string& commandName)
{
    dispatcher()->dispatch(actionQuery);

    PlaybackStatus status = playbackController() ? playbackController()->playbackStatus() : PlaybackStatus::Stopped;
    std::string message = commandName + ": playback is now " + playbackStatusToString(status);

    return make_response(request, make_ret(Ret::Code::Ok, message));
}

Response AudacityCommandsController::handleNewProject(const Request& request)
{
    stopPlaybackIfRunning();

    dispatcher()->dispatch(ActionCode("file-new"));

    project::IAudacityProjectPtr project = globalContext() ? globalContext()->currentProject() : nullptr;
    //! NOTE Audacity appears to reuse the same IAudacityProject instance for "new project"
    //! rather than swapping in a new pointer, so pointer identity can't detect success here -
    //! isNewlyCreated() is the reliable signal instead.
    bool changed = project && project->isNewlyCreated();

    std::string message = changed
                           ? "New project created"
                           : "file-new dispatched, but the current project does not report as newly created - it may have "
                             "been rejected (e.g. a save/discard prompt for the existing project)";

    return make_response(request, make_ret(changed ? Ret::Code::Ok : Ret::Code::UnknownError, message));
}

Response AudacityCommandsController::handleSaveProject(const Request& request)
{
    project::IAudacityProjectPtr project = globalContext() ? globalContext()->currentProject() : nullptr;
    if (!project) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }

    //! NOTE A project that has never been saved has no file path yet. Dispatching file-save
    //! on it triggers Audacity's interactive Save As flow (file dialog etc.), which is not
    //! safe to invoke headlessly from here - it assumes a normal UI-driven call site.
    if (project->isNewlyCreated() || project->path().empty()) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("This project has never been saved and has no file path yet - "
                                                              "saving it would require an interactive Save As dialog, which "
                                                              "isn't supported over MCP yet")));
    }

    if (!project->hasUnsavedChanges()) {
        return make_response(request, make_ret(Ret::Code::Ok, std::string("Nothing to save - project has no unsaved changes")));
    }

    stopPlaybackIfRunning();

    //! NOTE Dispatch the same action the File > Save menu item uses, rather than calling
    //! IProjectFilesController::saveProject() directly - the direct call bypasses whatever
    //! bookkeeping the UI action does around Audacity's autosave system (observed to trigger
    //! a debug assertion when called headlessly without that protection).
    dispatcher()->dispatch(ActionCode("file-save"));

    bool stillUnsaved = project->hasUnsavedChanges();
    std::string message = stillUnsaved
                           ? "file-save dispatched, but the project still reports unsaved changes - it may have failed silently"
                           : "Project saved";

    return make_response(request, make_ret(stillUnsaved ? Ret::Code::UnknownError : Ret::Code::Ok, message));
}

Response AudacityCommandsController::handleRecentCommands(const Request& request)
{
    JsonArray arr;
    for (const CommandHistoryEntry& entry : m_history) {
        JsonObject obj;
        obj["id"] = entry.id;
        obj["command"] = entry.command;
        obj["timestamp"] = isoTimestamp(entry.timestamp);
        obj["success"] = entry.success;
        obj["message"] = entry.message;
        arr << obj;
    }

    JsonDocument doc(arr);
    std::string json(doc.toJson(JsonDocument::Format::Compact).constChar());

    Response response = make_response(request, make_ret(Ret::Code::Ok, std::string("recent-commands")));
    response.data = json;
    return response;
}

Response AudacityCommandsController::handleCommandStatus(const Request& request)
{
    std::string targetId = request.query.param("id").toString();

    for (const CommandHistoryEntry& entry : m_history) {
        if (entry.id == targetId) {
            JsonObject obj;
            obj["id"] = entry.id;
            obj["command"] = entry.command;
            obj["timestamp"] = isoTimestamp(entry.timestamp);
            obj["success"] = entry.success;
            obj["message"] = entry.message;

            JsonDocument doc(obj);
            std::string json(doc.toJson(JsonDocument::Format::Compact).constChar());

            Response response = make_response(request, make_ret(Ret::Code::Ok, std::string("found")));
            response.data = json;
            return response;
        }
    }

    return make_response(request, make_ret(Ret::Code::UnknownError, "No command found with id: " + targetId));
}

static au::trackedit::LabelKey parseLabelKey(const std::string& key)
{
    size_t sep = key.find(':');
    if (sep == std::string::npos) {
        return au::trackedit::LabelKey();
    }

    try {
        au::trackedit::TrackId trackId = std::stoll(key.substr(0, sep));
        au::trackedit::TrackItemId itemId = std::stoll(key.substr(sep + 1));
        return au::trackedit::LabelKey(trackId, itemId);
    } catch (const std::exception&) {
        return au::trackedit::LabelKey();
    }
}

static std::string labelKeyToString(const au::trackedit::LabelKey& key)
{
    return std::to_string(key.trackId) + ":" + std::to_string(key.itemId);
}

au::trackedit::TrackId AudacityCommandsController::findFirstLabelTrack() const
{
    project::IAudacityProjectPtr project = globalContext() ? globalContext()->currentProject() : nullptr;
    if (!project) {
        return au::trackedit::INVALID_TRACK;
    }

    au::trackedit::ITrackeditProjectPtr trackeditProject = project->trackeditProject();
    if (!trackeditProject) {
        return au::trackedit::INVALID_TRACK;
    }

    for (const au::trackedit::Track& track : trackeditProject->trackList()) {
        if (track.type == au::trackedit::TrackType::Label) {
            return track.id;
        }
    }

    return au::trackedit::INVALID_TRACK;
}

Response AudacityCommandsController::handleListLabels(const Request& request)
{
    au::trackedit::TrackId trackId = findFirstLabelTrack();
    if (trackId == au::trackedit::INVALID_TRACK) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No label track found in the current project")));
    }

    project::IAudacityProjectPtr project = globalContext()->currentProject();
    au::trackedit::ITrackeditProjectPtr trackeditProject = project->trackeditProject();

    JsonArray arr;
    for (const au::trackedit::Label& label : trackeditProject->labelList(trackId)) {
        JsonObject obj;
        obj["key"] = labelKeyToString(label.key);
        obj["title"] = label.title.toStdString();
        obj["start"] = label.startTime;
        obj["end"] = label.endTime;
        arr << obj;
    }

    JsonDocument doc(arr);
    std::string json(doc.toJson(JsonDocument::Format::Compact).constChar());

    Response response = make_response(request, make_ret(Ret::Code::Ok, std::string("list-labels")));
    response.data = json;
    return response;
}

bool AudacityCommandsController::hasOpenProject() const
{
    return globalContext() && globalContext()->currentProject() != nullptr;
}

Response AudacityCommandsController::handleAddLabelTrack(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }

    if (!trackeditInteraction()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("ITrackeditInteraction not available")));
    }

    RetVal<au::trackedit::TrackId> result = trackeditInteraction()->newLabelTrack();
    if (!result.ret.success()) {
        return make_response(request, result.ret);
    }

    std::string message = "Label track created with id " + std::to_string(result.val);
    return make_response(request, make_ret(Ret::Code::Ok, message));
}

Response AudacityCommandsController::handleAddLabel(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }

    if (!trackeditInteraction() || !selectionController()) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("ITrackeditInteraction/ISelectionController not available")));
    }

    //! NOTE Deliberately using ITrackeditInteraction::addLabelToSelection() here, NOT
    //! ILabelsInteraction::addLabel() - the latter looks like the obvious API (it even takes
    //! a track id) but Au3LabelsInteraction::addLabel() hardcodes the new label's time to
    //! 0.0-0.0 regardless of the actual selection (found via live testing: every label landed
    //! at t=0 no matter where the selection/playhead actually was). addLabelToSelection() is
    //! the real code path behind the "Add Label at Selection" keyboard shortcut - it places
    //! the label at the playback position while playing/recording, or the current edit
    //! selection otherwise, and creates a label track automatically if none exists yet.
    bool ok = trackeditInteraction()->addLabelToSelection();
    if (!ok) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Failed to add label")));
    }

    //! NOTE addLabelToSelection() only returns bool - it reports the new label's key by
    //! setting it as the selection, so read it back from there rather than re-deriving it.
    au::trackedit::LabelKeyList selected = selectionController()->selectedLabels();
    if (selected.empty()) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("Label added but its key could not be determined")));
    }
    au::trackedit::LabelKey key = selected.front();

    std::string text = request.query.param("text").toString();
    if (!text.empty()) {
        labelsInteraction()->changeLabelTitle(key, String::fromStdString(text));
    }

    std::string message = "Label added with key " + labelKeyToString(key);
    return make_response(request, make_ret(Ret::Code::Ok, message));
}

Response AudacityCommandsController::handleRemoveLabel(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }

    if (!labelsInteraction()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("ILabelsInteraction not available")));
    }

    std::string keyStr = request.query.param("key").toString();
    au::trackedit::LabelKey key = parseLabelKey(keyStr);
    if (!key.isValid()) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("Invalid or missing 'key' argument - expected format "
                                                              "\"trackId:itemId\" (as returned by list-labels/add-label)")));
    }

    bool ok = labelsInteraction()->removeLabel(key);
    std::string message = ok ? "Label removed" : "Failed to remove label - it may not exist";
    return make_response(request, make_ret(ok ? Ret::Code::Ok : Ret::Code::UnknownError, message));
}

Response AudacityCommandsController::handleUpdateLabelText(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }

    if (!labelsInteraction()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("ILabelsInteraction not available")));
    }

    std::string keyStr = request.query.param("key").toString();
    au::trackedit::LabelKey key = parseLabelKey(keyStr);
    if (!key.isValid()) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("Invalid or missing 'key' argument - expected format "
                                                              "\"trackId:itemId\" (as returned by list-labels/add-label)")));
    }

    std::string text = request.query.param("text").toString();
    bool ok = labelsInteraction()->changeLabelTitle(key, String::fromStdString(text));
    std::string message = ok ? "Label text updated" : "Failed to update label text - the label may not exist";
    return make_response(request, make_ret(ok ? Ret::Code::Ok : Ret::Code::UnknownError, message));
}

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

    //! NOTE Deliberately always calling the two-arg performEffect() overload, even
    //! with an empty params string, rather than branching to the one-arg overload
    //! for the no-params case. Confirmed via a crash dump: the one-arg overload
    //! (performEffect(effectId)) skips the "resolve by id with title fallback"
    //! search that the two-arg overload does, so it only accepts an effect's real
    //! internal PluginID - not the human title ("Noise reduction" etc) this
    //! command's InputSchema documents as the expected effect_id format. That
    //! caused "Effect not found" for the no-params profile-capture call NoiseReduction
    //! needs, which in turn left it applying reduction with no profile ever
    //! captured - NoiseReductionEffect::Process() then hit an unguarded assert and
    //! crashed the whole app. The two-arg overload already skips parameter
    //! application internally when params is empty, so this is a pure bugfix, not
    //! a behavior change for the params-provided case.
    muse::Ret ret = effectExecutionScenario()->performEffect(String::fromStdString(effectId), params);

    return make_response(request, ret);
}

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

    muse::Val startVal = request.query.param("start");
    muse::Val endVal = request.query.param("end");
    if (startVal.isNull() || endVal.isNull()) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("Missing 'start'/'end' arguments (seconds)")));
    }

    //! NOTE Val::toDouble() calls std::stod() internally with no exception guard of
    //! its own - a malformed/non-numeric value throws std::invalid_argument, which
    //! is uncaught all the way up through the command dispatcher and crashes the
    //! whole app (confirmed via a crash dump). Never call toDouble()/toInt() on
    //! externally-supplied MCP arguments without a try/catch, here or elsewhere.
    double start = 0.0;
    double end = 0.0;
    try {
        start = startVal.toDouble();
        end = endVal.toDouble();
    } catch (const std::exception& e) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("Invalid 'start'/'end' argument - must be numbers: ") + e.what()));
    }

    selectionController()->setSelectedAllAudioData(start, end);

    std::string message = "Selected time range " + std::to_string(start) + "-" + std::to_string(end) + "s";
    return make_response(request, make_ret(Ret::Code::Ok, message));
}

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

    //! NOTE Refuse to silently overwrite an existing file by default - this command
    //! accepts an arbitrary filesystem path from an MCP caller with no other
    //! restriction on where it may point, so an AI-driven client passing the wrong
    //! path (or a malicious/compromised one) must not be able to clobber a file it
    //! didn't create. Pass overwrite=true to explicitly allow it (e.g. re-exporting
    //! the same analysis temp file on a later pipeline step).
    bool overwrite = request.query.param("overwrite").toBool();
    if (!overwrite && fileSystem() && fileSystem()->exists(muse::io::path_t(path))) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("File already exists at '") + path
                                                 + "' - pass overwrite=true to replace it"));
    }

    au::importexport::IExporter::Options options;
    options[au::importexport::IExporter::OptionKey::Format] = muse::Val(std::string("WAV (Microsoft)"));
    options[au::importexport::IExporter::OptionKey::ProcessType]
        = muse::Val(static_cast<int>(au::importexport::ExportProcessType::SELECTED_AUDIO));
    options[au::importexport::IExporter::OptionKey::ExportChannelsType]
        = muse::Val(static_cast<int>(au::importexport::ExportChannelsPref::ExportChannels::MONO));
    //! NOTE exportConfiguration()->exportSampleRate() defaults to 0 when the export
    //! dialog has never been opened through the normal UI flow (confirmed live: the
    //! exported WAV's header had SampleRate=0, and libsndfile silently wrote a
    //! zero-length/zero-rate file instead of real audio data). Always set this
    //! explicitly rather than relying on that default.
    options[au::importexport::IExporter::OptionKey::ExportSampleRate] = muse::Val(44100);

    muse::Ret ret = exporter()->exportData(muse::io::path_t(path), options);
    return make_response(request, ret);
}
