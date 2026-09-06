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
#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>

#include "global/serialization/json.h"
#include "trackedit/itrackeditproject.h"
#include "au3-realtime-effects/RealtimeEffectState.h"
#include "au3-effects/EffectPlugin.h"

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
    const bool ok = gmtime_s(&tmUtc, &t) == 0;
#else
    const bool ok = gmtime_r(&t, &tmUtc) != nullptr;
#endif
    //! Checked rather than assumed: tmUtc is zero initialised, so a failure here
    //! would otherwise be reported as a timestamp in the year 1900 rather than as
    //! an error.
    if (!ok) {
        return std::string("unknown");
    }
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
    //! NOTE Deliberately a distinct action from play-stop - confirmed live that
    //! toggle-play-stop does NOT play from the current selection, it just
    //! resumes from wherever the last cursor/playback position was (a
    //! composition of select-time + play-stop was tested and found to start
    //! playback from t=0 regardless of the selection just set). play-selection
    //! is PlaybackController's own dedicated action for "play what's currently
    //! selected" (see playSelectionAction() in playbackcontroller.cpp).
    registerCommand(Command("command://mcp/play-selection"), [this](const Request& request) {
        return handlePlaybackAction(request, ActionQuery("action://playback/play-selection"), "play-selection");
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
    registerCommand(Command("command://mcp/update-label-time"), [this](const Request& request) {
        return handleUpdateLabelTime(request);
    });

    registerCommand(Command("command://mcp/apply-effects"), [this](const Request& request) {
        return handleApplyEffects(request);
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

    registerCommand(Command("command://mcp/select-none"), [this](const Request& request) {
        return handleSelectNone(request);
    });
    registerCommand(Command("command://mcp/select-tracks"), [this](const Request& request) {
        return handleSelectTracks(request);
    });

    registerCommand(Command("command://mcp/track-add-mono"), [this](const Request& request) {
        return handleTrackAddMono(request);
    });
    registerCommand(Command("command://mcp/track-add-stereo"), [this](const Request& request) {
        return handleTrackAddStereo(request);
    });
    registerCommand(Command("command://mcp/track-remove"), [this](const Request& request) {
        return handleTrackRemove(request);
    });
    registerCommand(Command("command://mcp/track-set-properties"), [this](const Request& request) {
        return handleTrackSetProperties(request);
    });
    registerCommand(Command("command://mcp/track-duplicate"), [this](const Request& request) {
        return handleTrackDuplicate(request);
    });
    registerCommand(Command("command://mcp/track-resample"), [this](const Request& request) {
        return handleTrackResample(request);
    });
    registerCommand(Command("command://mcp/track-mute-all"), [this](const Request& request) {
        return handleTrackMuteAll(request);
    });
    registerCommand(Command("command://mcp/track-unmute-all"), [this](const Request& request) {
        return handleTrackUnmuteAll(request);
    });
    registerCommand(Command("command://mcp/set-clip-pitch"), [this](const Request& request) {
        return handleSetClipPitch(request);
    });
    registerCommand(Command("command://mcp/reset-clip-pitch"), [this](const Request& request) {
        return handleResetClipPitch(request);
    });
    registerCommand(Command("command://mcp/set-clip-speed"), [this](const Request& request) {
        return handleSetClipSpeed(request);
    });
    registerCommand(Command("command://mcp/reset-clip-speed"), [this](const Request& request) {
        return handleResetClipSpeed(request);
    });
    registerCommand(Command("command://mcp/render-clip-pitch-speed"), [this](const Request& request) {
        return handleRenderClipPitchSpeed(request);
    });
    registerCommand(Command("command://mcp/reset-clip-pitch-speed"), [this](const Request& request) {
        return handleResetClipPitchSpeed(request);
    });
    registerCommand(Command("command://mcp/split-clip-at-silences"), [this](const Request& request) {
        return handleSplitClipAtSilences(request);
    });
    registerCommand(Command("command://mcp/split-range-at-silences"), [this](const Request& request) {
        return handleSplitRangeAtSilences(request);
    });
    registerCommand(Command("command://mcp/trim-clip"), [this](const Request& request) {
        return handleTrimClip(request);
    });
    registerCommand(Command("command://mcp/stretch-clip"), [this](const Request& request) {
        return handleStretchClip(request);
    });
    registerCommand(Command("command://mcp/nearest-zero-crossing"), [this](const Request& request) {
        return handleNearestZeroCrossing(request);
    });
    registerCommand(Command("command://mcp/set-clip-color"), [this](const Request& request) {
        return handleSetClipColor(request);
    });
    registerCommand(Command("command://mcp/set-track-color"), [this](const Request& request) {
        return handleSetTrackColor(request);
    });

    registerCommand(Command("command://mcp/edit-cut"), [this](const Request& request) {
        return handleEditCut(request);
    });
    registerCommand(Command("command://mcp/edit-copy"), [this](const Request& request) {
        return handleEditCopy(request);
    });
    registerCommand(Command("command://mcp/edit-paste"), [this](const Request& request) {
        return handleEditPaste(request);
    });
    registerCommand(Command("command://mcp/edit-delete"), [this](const Request& request) {
        return handleEditDelete(request);
    });
    registerCommand(Command("command://mcp/edit-split"), [this](const Request& request) {
        return handleEditSplit(request);
    });
    registerCommand(Command("command://mcp/edit-trim"), [this](const Request& request) {
        return handleEditTrim(request);
    });
    registerCommand(Command("command://mcp/edit-silence"), [this](const Request& request) {
        return handleEditSilence(request);
    });
    registerCommand(Command("command://mcp/edit-duplicate"), [this](const Request& request) {
        return handleEditDuplicate(request);
    });
    registerCommand(Command("command://mcp/edit-undo"), [this](const Request& request) {
        return handleEditUndo(request);
    });
    registerCommand(Command("command://mcp/edit-redo"), [this](const Request& request) {
        return handleEditRedo(request);
    });
    registerCommand(Command("command://mcp/edit-split-new"), [this](const Request& request) {
        return handleEditSplitNew(request);
    });
    registerCommand(Command("command://mcp/edit-split-cut"), [this](const Request& request) {
        return handleEditSplitCut(request);
    });
    registerCommand(Command("command://mcp/edit-split-delete"), [this](const Request& request) {
        return handleEditSplitDelete(request);
    });
    registerCommand(Command("command://mcp/edit-disjoin"), [this](const Request& request) {
        return handleEditDisjoin(request);
    });
    registerCommand(Command("command://mcp/edit-join"), [this](const Request& request) {
        return handleEditJoin(request);
    });
    registerCommand(Command("command://mcp/select-zero-crossing"), [this](const Request& request) {
        return handleSelectZeroCrossing(request);
    });

    registerCommand(Command("command://mcp/project-open"), [this](const Request& request) {
        return handleProjectOpen(request);
    });
    registerCommand(Command("command://mcp/project-import"), [this](const Request& request) {
        return handleProjectImport(request);
    });
    registerCommand(Command("command://mcp/project-close"), [this](const Request& request) {
        return handleProjectClose(request);
    });
    registerCommand(Command("command://mcp/project-save-as"), [this](const Request& request) {
        return handleProjectSaveAs(request);
    });
    registerCommand(Command("command://mcp/project-export"), [this](const Request& request) {
        return handleProjectExport(request);
    });

    registerCommand(Command("command://mcp/transport-record"), [this](const Request& request) {
        return handleTransportRecord(request);
    });
    registerCommand(Command("command://mcp/cursor-set"), [this](const Request& request) {
        return handleCursorSet(request);
    });
    registerCommand(Command("command://mcp/project-new"), [this](const Request& request) {
        return handleNewProject(request);
    });

    registerCommand(Command("command://mcp/project-get-info"), [this](const Request& request) {
        return handleProjectGetInfo(request);
    });
    registerCommand(Command("command://mcp/project-get-metadata"), [this](const Request& request) {
        return handleProjectGetMetadata(request);
    });
    registerCommand(Command("command://mcp/project-set-metadata"), [this](const Request& request) {
        return handleProjectSetMetadata(request);
    });
    registerCommand(Command("command://mcp/track-get-info"), [this](const Request& request) {
        return handleTrackGetInfo(request);
    });
    registerCommand(Command("command://mcp/select-clip"), [this](const Request& request) {
        return handleSelectClip(request);
    });
    registerCommand(Command("command://mcp/transport-get-play-position"), [this](const Request& request) {
        return handleTransportGetPlayPosition(request);
    });
    registerCommand(Command("command://mcp/list-effects"), [this](const Request& request) {
        return handleListEffects(request);
    });
    registerCommand(Command("command://mcp/add-realtime-effects"), [this](const Request& request) {
        return handleAddRealtimeEffects(request);
    });
    registerCommand(Command("command://mcp/add-realtime-effect"), [this](const Request& request) {
        return handleAddRealtimeEffect(request);
    });
    registerCommand(Command("command://mcp/list-realtime-effects"), [this](const Request& request) {
        return handleListRealtimeEffects(request);
    });
    registerCommand(Command("command://mcp/remove-realtime-effect"), [this](const Request& request) {
        return handleRemoveRealtimeEffect(request);
    });
    registerCommand(Command("command://mcp/set-realtime-effect-active"), [this](const Request& request) {
        return handleSetRealtimeEffectActive(request);
    });
    registerCommand(Command("command://mcp/list-effect-parameters"), [this](const Request& request) {
        return handleListEffectParameters(request);
    });
    registerCommand(Command("command://mcp/set-effect-parameters"), [this](const Request& request) {
        return handleSetEffectParameters(request);
    });
    registerCommand(Command("command://mcp/set-effect-parameter"), [this](const Request& request) {
        return handleSetEffectParameter(request);
    });
    registerCommand(Command("command://mcp/list-effect-presets"), [this](const Request& request) {
        return handleListEffectPresets(request);
    });
    registerCommand(Command("command://mcp/apply-effect-preset"), [this](const Request& request) {
        return handleApplyEffectPreset(request);
    });
}

void AudacityCommandsController::registerCommand(const Command& command, const Handler& handler)
{
    //! The body is wrapped in a catch-all below, so nothing escapes in practice. The
    //! check still reports it because it treats every call it cannot prove noexcept,
    //! including the fallback Response construction itself, as able to throw; the only
    //! paths left are allocation failures, where there is nothing useful left to do.
    //! NOLINTNEXTLINE(bugprone-exception-escape)
    commandDispatcher()->onRequest(this, command, [this, command, handler](const Request& request) -> Response {
        try {
        //! Arguments arrive from outside the application and several muse::Val
        //! conversions throw on malformed input - Val::toDouble() calls std::stod(),
        //! which raises std::invalid_argument for a non-numeric string. An exception
        //! escaping a handler unwinds through the command dispatcher and the Qt event
        //! loop and terminates the process, so one bad argument from a client would
        //! take the whole application down (confirmed previously via a crash dump).
        //! Individual handlers still validate their own arguments and return proper
        //! messages; this is the backstop that keeps any missed case from being fatal.
        const Response response = [&]() -> Response {
            try {
                return handler(request);
            } catch (const std::exception& e) {
                return make_response(request, make_ret(Ret::Code::UnknownError,
                                                       std::string("Command failed: ") + e.what()));
            } catch (...) {
                return make_response(request, make_ret(Ret::Code::UnknownError,
                                                       std::string("Command failed with an unrecognised error")));
            }
        }();

        //! Also guarded, and deliberately so: this runs after the handler has already
        //! produced its result, and it allocates, so an exception here would escape
        //! past the guard above and defeat the whole point of it. Failing to record
        //! history must never change what the caller is told about their command.
        try {
            recordHistory(std::to_string(request.callId), command.toString(), response.ret.success(), response.ret.text());
        } catch (...) { // NOLINT(bugprone-empty-catch) - see below
            //! Intentionally swallowed. The only failure mode is allocation, and the
            //! command itself has already succeeded or failed on its own terms; there
            //! is nothing to report to the caller that would not be more misleading
            //! than saying nothing, and logging here would allocate too.
        }

        return response;
    } catch (...) {
        //! Outermost guard, so the "nothing escapes" guarantee above is actually
        //! total. The catch handlers above allocate while building their error strings,
        //! so they can themselves throw; returning a default-constructed Response
        //! reports a plain failure without allocating anything further.
        return Response();
    }
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
    //! NOTE ProjectActionsController::newProject() only creates the project in the current
    //! window when no project is currently open; if one is already open it spawns a whole new
    //! OS-level window via multiwindowsProvider(), which is not safe to trigger headlessly here.
    //! So this command is restricted to the no-project-open case - use project-open/project-import
    //! to bring in another project instead of calling this while one is already open.
    if (hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("A project is already open - creating a new one here would open "
                                                              "a new application window instead of replacing it, which isn't "
                                                              "safe to trigger over MCP. Close the current project first, or "
                                                              "use project-open/project-import instead.")));
    }

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

//! Parses a client-supplied numeric argument safely.
//!
//! Two separate hazards, both reachable from outside the application:
//!  - Val::toDouble() calls std::stod(), which throws std::invalid_argument on a
//!    non-numeric string. Uncaught, that unwinds through the command dispatcher and
//!    the Qt event loop and terminates the process.
//!  - std::stod() also accepts "nan"/"inf" and ignores trailing characters, so a
//!    value like "NaN-ish" yields NaN with no exception at all. Such a value then
//!    propagates into trackedit/audio arithmetic and crashes there instead
//!    (confirmed live: stretch-clip with min_clip_duration="NaN-ish").
//!
//! Returns false and fills @p error if the value is missing-but-required, not a
//! number, or not finite.
static bool tryParseFiniteDouble(const muse::Val& value, const char* name, bool required,
                                 double defaultValue, double& out, std::string& error)
{
    if (value.isNull()) {
        if (required) {
            error = std::string("Missing required '") + name + "' argument";
            return false;
        }
        out = defaultValue;
        return true;
    }

    double parsed = 0.0;
    try {
        parsed = value.toDouble();
    } catch (const std::exception& e) {
        error = std::string("Invalid '") + name + "' argument - must be a number: " + e.what();
        return false;
    }

    if (!std::isfinite(parsed)) {
        error = std::string("Invalid '") + name + "' argument - must be a finite number";
        return false;
    }

    out = parsed;
    return true;
}

//! Val::toDouble() for arguments that are consumed directly rather than through
//! tryParseFiniteDouble(). std::stod() accepts "nan"/"inf" and ignores trailing
//! characters, so "NaN-ish" parses to NaN without throwing; that NaN then reaches
//! trackedit/audio arithmetic and crashes there (confirmed live via cursor-set and
//! stretch-clip). Throwing here instead means the caller's existing try/catch turns
//! it into an ordinary "invalid argument" response.
static double toFiniteDouble(const muse::Val& value)
{
    const double parsed = value.toDouble();
    if (!std::isfinite(parsed)) {
        throw std::invalid_argument("value must be a finite number");
    }
    return parsed;
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

//! NOTE ClipKey and LabelKey are both aliases for the same TrackItemKey type,
//! so this is just labelKeyToString() under a name that reads correctly at
//! clip call sites - not a separate implementation.
static std::string clipKeyToString(const au::trackedit::ClipKey& key)
{
    return labelKeyToString(key);
}

static std::string effectFamilyToString(au::effects::EffectFamily family)
{
    switch (family) {
    case au::effects::EffectFamily::Builtin: return "Builtin";
    case au::effects::EffectFamily::VST3: return "VST3";
#ifdef Q_OS_LINUX
    case au::effects::EffectFamily::LV2: return "LV2";
#endif
#ifdef Q_OS_MACOS
    case au::effects::EffectFamily::AudioUnit: return "AudioUnit";
#endif
    case au::effects::EffectFamily::Nyquist: return "Nyquist";
    case au::effects::EffectFamily::Extension: return "Extension";
    case au::effects::EffectFamily::Unknown:
    default: return "Unknown";
    }
}

static std::string effectTypeToString(au::effects::EffectType type)
{
    switch (type) {
    case au::effects::EffectType::Analyzer: return "Analyzer";
    case au::effects::EffectType::Generator: return "Generator";
    case au::effects::EffectType::Processor: return "Processor";
    case au::effects::EffectType::Tool: return "Tool";
    case au::effects::EffectType::Unknown:
    default: return "Unknown";
    }
}

static std::string parameterTypeToString(au::effects::ParameterType type)
{
    switch (type) {
    case au::effects::ParameterType::Toggle: return "Toggle";
    case au::effects::ParameterType::Dropdown: return "Dropdown";
    case au::effects::ParameterType::Slider: return "Slider";
    case au::effects::ParameterType::Numeric: return "Numeric";
    case au::effects::ParameterType::ReadOnly: return "ReadOnly";
    case au::effects::ParameterType::Time: return "Time";
    case au::effects::ParameterType::File: return "File";
    case au::effects::ParameterType::Text: return "Text";
    case au::effects::ParameterType::Unknown:
    default: return "Unknown";
    }
}

static std::string toLowerAscii(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

static std::string trackTypeToString(au::trackedit::TrackType type)
{
    switch (type) {
    case au::trackedit::TrackType::Mono: return "Mono";
    case au::trackedit::TrackType::Stereo: return "Stereo";
    case au::trackedit::TrackType::Label: return "Label";
    case au::trackedit::TrackType::Undefined:
    default: return "Undefined";
    }
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

    //! Checked explicitly rather than relying on findFirstLabelTrack() above having
    //! already bailed out when no project is open: that is a real but non-obvious
    //! invariant, and dereferencing a null project here is an access violation.
    project::IAudacityProjectPtr project = globalContext() ? globalContext()->currentProject() : nullptr;
    if (!project) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }

    au::trackedit::ITrackeditProjectPtr trackeditProject = project->trackeditProject();
    if (!trackeditProject) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No trackedit project available")));
    }

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

Response AudacityCommandsController::handleUpdateLabelTime(const Request& request)
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

    muse::Val startVal = request.query.param("start");
    muse::Val endVal = request.query.param("end");
    if (startVal.isNull() && endVal.isNull()) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("Provide at least one of 'start', 'end'")));
    }

    std::vector<std::string> applied;

    if (!startVal.isNull()) {
        double newStart = 0.0;
        try {
            newStart = toFiniteDouble(startVal);
        } catch (const std::exception& e) {
            return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Invalid 'start' argument: ") + e.what()));
        }
        bool ok = labelsInteraction()->stretchLabelLeft(key, newStart, true);
        if (!ok) {
            return make_response(request, make_ret(Ret::Code::UnknownError,
                                                     std::string("Failed to move label start - the label may not exist")));
        }
        applied.push_back("start");
    }

    if (!endVal.isNull()) {
        double newEnd = 0.0;
        try {
            newEnd = toFiniteDouble(endVal);
        } catch (const std::exception& e) {
            return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Invalid 'end' argument: ") + e.what()));
        }
        bool ok = labelsInteraction()->stretchLabelRight(key, newEnd, true);
        if (!ok) {
            return make_response(request, make_ret(Ret::Code::UnknownError,
                                                     std::string("Failed to move label end - the label may not exist")));
        }
        applied.push_back("end");
    }

    std::string message = "Updated: ";
    for (size_t i = 0; i < applied.size(); ++i) {
        message += applied[i];
        if (i + 1 < applied.size()) {
            message += ", ";
        }
    }
    return make_response(request, make_ret(Ret::Code::Ok, message));
}

Response AudacityCommandsController::handleApplyEffects(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!effectExecutionScenario()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("IEffectExecutionScenario not available")));
    }

    const std::string effectIds = request.query.param("effect_ids").toString();
    if (effectIds.empty()) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("Missing required 'effect_ids' argument - '|' separated")));
    }

    //! Effect ids contain spaces ("Noise reduction"), ';' and ':' (VST3 ids embed a
    //! path and a hash), and params are space separated Key=Value pairs, so '|' is
    //! used as the separator: it appears in neither.
    auto split = [](const std::string& in) {
        std::vector<std::string> out;
        std::stringstream ss(in);
        std::string item;
        while (std::getline(ss, item, '|')) {
            out.push_back(item);
        }
        //! getline yields nothing after a trailing separator, so "a|b|" reads as two
        //! fields. The last entry is empty whenever the final effect takes no
        //! parameters, and dropping it would fail the one-to-one count check.
        if (!in.empty() && in.back() == '|') {
            out.push_back(std::string());
        }
        return out;
    };

    const std::vector<std::string> ids = split(effectIds);
    std::vector<std::string> params = split(request.query.param("params_list").toString());
    if (params.empty()) {
        params.resize(ids.size());
    }
    if (params.size() != ids.size()) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("'params_list' has ") + std::to_string(params.size())
                                                 + " entries but 'effect_ids' has " + std::to_string(ids.size())
                                                 + " - they must correspond one to one (use an empty entry for no params)"));
    }

    //! Selecting once up front rather than before every effect: the selection does
    //! not change between them, and repeating it was the second half of every step
    //! a pipeline ran.
    const bool selectAll = request.query.param("select_all").isNull()
                           || request.query.param("select_all").toBool();
    if (selectAll) {
        if (!selectionController()) {
            return make_response(request, make_ret(Ret::Code::UnknownError, std::string("ISelectionController not available")));
        }
        dispatcher()->dispatch(ActionCode("select-all"));
    }

    //! Stops at the first failure rather than continuing: these are destructive
    //! edits, and pressing on would leave the audio in a state the caller cannot
    //! reason about. The response names exactly what was applied so the caller
    //! knows what to undo.
    std::vector<std::string> applied;
    for (size_t i = 0; i < ids.size(); ++i) {
        if (ids[i].empty()) {
            continue;
        }
        const muse::Ret ret = effectExecutionScenario()->performEffect(String::fromStdString(ids[i]), params[i]);
        if (!ret.success()) {
            std::string message = "Applied " + std::to_string(applied.size()) + " of " + std::to_string(ids.size())
                                  + " effects, then '" + ids[i] + "' failed: " + ret.text();
            if (!applied.empty()) {
                message += ". Already applied (undo this many times to revert): ";
                for (size_t j = 0; j < applied.size(); ++j) {
                    message += applied[j] + (j + 1 < applied.size() ? ", " : "");
                }
            }
            return make_response(request, make_ret(Ret::Code::UnknownError, message));
        }
        applied.push_back(ids[i]);
    }

    std::string message = "Applied " + std::to_string(applied.size()) + " effects: ";
    for (size_t i = 0; i < applied.size(); ++i) {
        message += applied[i] + (i + 1 < applied.size() ? ", " : "");
    }
    return make_response(request, make_ret(Ret::Code::Ok, message));
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

    project::IAudacityProjectPtr project = globalContext() ? globalContext()->currentProject() : nullptr;
    if (!project) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }

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
        start = toFiniteDouble(startVal);
        end = toFiniteDouble(endVal);
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

au::trackedit::TrackIdList AudacityCommandsController::selectedOrEmptyTracks() const
{
    if (!selectionController()) {
        return {};
    }
    return selectionController()->selectedTracks();
}

Response AudacityCommandsController::handleSelectNone(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!selectionController()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("ISelectionController not available")));
    }

    selectionController()->resetSelectedTracks();
    selectionController()->resetDataSelection();

    return make_response(request, make_ret(Ret::Code::Ok, std::string("Selection cleared")));
}

Response AudacityCommandsController::handleSelectTracks(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!selectionController()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("ISelectionController not available")));
    }

    muse::Val trackVal = request.query.param("track");
    if (trackVal.isNull()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Missing required 'track' argument (0-based index)")));
    }
    int trackIndex = 0;
    int count = 1;
    try {
        trackIndex = static_cast<int>(toFiniteDouble(trackVal));
        muse::Val countVal = request.query.param("count");
        if (!countVal.isNull()) {
            count = static_cast<int>(toFiniteDouble(countVal));
        }
    } catch (const std::exception& e) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Invalid 'track'/'count' argument: ") + e.what()));
    }

    project::IAudacityProjectPtr project = globalContext() ? globalContext()->currentProject() : nullptr;
    if (!project) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }

    au::trackedit::ITrackeditProjectPtr trackeditProject = project->trackeditProject();
    if (!trackeditProject) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No trackedit project available")));
    }

    std::vector<au::trackedit::Track> allTracks;
    for (const au::trackedit::Track& track : trackeditProject->trackList()) {
        allTracks.push_back(track);
    }

    if (trackIndex < 0 || trackIndex >= static_cast<int>(allTracks.size())) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("Track index out of range - project has ") + std::to_string(allTracks.size())
                                                 + " track(s)"));
    }

    au::trackedit::TrackIdList selected;
    for (int i = trackIndex; i < trackIndex + count && i < static_cast<int>(allTracks.size()); ++i) {
        selected.push_back(allTracks[static_cast<size_t>(i)].id);
    }

    selectionController()->setSelectedTracks(selected);

    return make_response(request, make_ret(Ret::Code::Ok,
                                             std::string("Selected ") + std::to_string(selected.size()) + " track(s) starting at index "
                                             + std::to_string(trackIndex)));
}

Response AudacityCommandsController::handleTrackAddMono(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!trackeditInteraction()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("ITrackeditInteraction not available")));
    }

    bool ok = trackeditInteraction()->newMonoTrack();
    std::string message = ok ? "Mono track added" : "Failed to add mono track";
    return make_response(request, make_ret(ok ? Ret::Code::Ok : Ret::Code::UnknownError, message));
}

Response AudacityCommandsController::handleTrackAddStereo(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!trackeditInteraction()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("ITrackeditInteraction not available")));
    }

    bool ok = trackeditInteraction()->newStereoTrack();
    std::string message = ok ? "Stereo track added" : "Failed to add stereo track";
    return make_response(request, make_ret(ok ? Ret::Code::Ok : Ret::Code::UnknownError, message));
}

Response AudacityCommandsController::handleTrackRemove(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!trackeditInteraction()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("ITrackeditInteraction not available")));
    }

    au::trackedit::TrackIdList tracks = selectedOrEmptyTracks();
    if (tracks.empty()) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("No tracks selected - call select-tracks first")));
    }

    bool ok = trackeditInteraction()->deleteTracks(tracks);
    std::string message = ok ? "Removed " + std::to_string(tracks.size()) + " track(s)" : "Failed to remove tracks";
    return make_response(request, make_ret(ok ? Ret::Code::Ok : Ret::Code::UnknownError, message));
}

Response AudacityCommandsController::handleTrackSetProperties(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!trackeditInteraction() || !trackPlaybackControl()) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("ITrackeditInteraction/ITrackPlaybackControl not available")));
    }

    muse::Val trackVal = request.query.param("track");
    if (trackVal.isNull()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Missing required 'track' argument (0-based index)")));
    }

    project::IAudacityProjectPtr project = globalContext() ? globalContext()->currentProject() : nullptr;
    if (!project) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }

    au::trackedit::ITrackeditProjectPtr trackeditProject = project->trackeditProject();
    if (!trackeditProject) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No trackedit project available")));
    }

    int trackIndex = 0;
    try {
        trackIndex = static_cast<int>(toFiniteDouble(trackVal));
    } catch (const std::exception& e) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Invalid 'track' argument: ") + e.what()));
    }

    std::vector<au::trackedit::Track> allTracks;
    for (const au::trackedit::Track& track : trackeditProject->trackList()) {
        allTracks.push_back(track);
    }
    if (trackIndex < 0 || trackIndex >= static_cast<int>(allTracks.size())) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("Track index out of range - project has ") + std::to_string(allTracks.size())
                                                 + " track(s)"));
    }
    au::trackedit::TrackId trackId = allTracks[static_cast<size_t>(trackIndex)].id;

    std::vector<std::string> applied;

    std::string name = request.query.param("name").toString();
    if (!name.empty()) {
        trackeditInteraction()->changeTrackTitle(trackId, String::fromStdString(name));
        applied.push_back("name");
    }

    muse::Val gainVal = request.query.param("gain");
    if (!gainVal.isNull()) {
        double gain = 0.0;
        try {
            gain = toFiniteDouble(gainVal);
        } catch (const std::exception& e) {
            return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Invalid 'gain' argument: ") + e.what()));
        }
        trackPlaybackControl()->setVolume(trackId, gain, true);
        applied.push_back("gain");
    }

    muse::Val panVal = request.query.param("pan");
    if (!panVal.isNull()) {
        double pan = 0.0;
        try {
            pan = toFiniteDouble(panVal);
        } catch (const std::exception& e) {
            return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Invalid 'pan' argument: ") + e.what()));
        }
        trackPlaybackControl()->setPan(trackId, pan, true);
        applied.push_back("pan");
    }

    muse::Val muteVal = request.query.param("mute");
    if (!muteVal.isNull()) {
        trackPlaybackControl()->setMuted(trackId, muteVal.toBool());
        applied.push_back("mute");
    }

    muse::Val soloVal = request.query.param("solo");
    if (!soloVal.isNull()) {
        trackPlaybackControl()->setSolo(trackId, soloVal.toBool());
        applied.push_back("solo");
    }

    if (applied.empty()) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("No properties given - pass at least one of name/gain/pan/mute/solo")));
    }

    std::string message = "Updated: ";
    for (size_t i = 0; i < applied.size(); ++i) {
        message += applied[i] + (i + 1 < applied.size() ? ", " : "");
    }
    return make_response(request, make_ret(Ret::Code::Ok, message));
}

Response AudacityCommandsController::handleTrackDuplicate(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!trackeditInteraction()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("ITrackeditInteraction not available")));
    }

    au::trackedit::TrackIdList tracks = selectedOrEmptyTracks();
    if (tracks.empty()) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("No tracks selected - call select-tracks first")));
    }

    bool ok = trackeditInteraction()->duplicateTracks(tracks);
    std::string message = ok ? "Duplicated " + std::to_string(tracks.size()) + " track(s)" : "Failed to duplicate tracks";
    return make_response(request, make_ret(ok ? Ret::Code::Ok : Ret::Code::UnknownError, message));
}

Response AudacityCommandsController::handleTrackResample(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!trackeditInteraction()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("ITrackeditInteraction not available")));
    }

    muse::Val rateVal = request.query.param("rate");
    if (rateVal.isNull()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Missing required 'rate' argument (Hz)")));
    }
    int rate = 0;
    try {
        rate = static_cast<int>(toFiniteDouble(rateVal));
    } catch (const std::exception& e) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Invalid 'rate' argument: ") + e.what()));
    }

    au::trackedit::TrackIdList tracks = selectedOrEmptyTracks();
    if (tracks.empty()) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("No tracks selected - call select-tracks first")));
    }

    bool ok = trackeditInteraction()->resampleTracks(tracks, rate);
    std::string message = ok ? "Resampled to " + std::to_string(rate) + "Hz" : "Failed to resample";
    return make_response(request, make_ret(ok ? Ret::Code::Ok : Ret::Code::UnknownError, message));
}

Response AudacityCommandsController::handleTrackMuteOrUnmuteAll(const Request& request, bool mute)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!trackPlaybackControl()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("ITrackPlaybackControl not available")));
    }

    project::IAudacityProjectPtr project = globalContext() ? globalContext()->currentProject() : nullptr;
    if (!project) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }

    au::trackedit::ITrackeditProjectPtr trackeditProject = project->trackeditProject();
    if (!trackeditProject) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No trackedit project available")));
    }

    // NOTE: iterates the real track list directly by id rather than a 0-based
    // index, deliberately avoiding the mismatch risk that blocked this
    // command earlier: trackeditProject->trackList() includes label tracks,
    // but project-get-info's (index-facing) track list does not - looping
    // "0..count" and calling the index-based track-set-properties would
    // silently hit the wrong track whenever a label track sits before an
    // audio track.
    //
    //! Label tracks are skipped rather than passed through. An earlier note here
    //! claimed muting one was a harmless no-op; it is not. setMuted() reaches
    //! Au3TrackPlaybackControl::setMuteOrSolo(), which resolves the id with
    //! DomAccessor::findWaveTrack() - that returns null for a label track and trips
    //! its IF_ASSERT_FAILED, taking the application down in a debug build
    //! (confirmed live: adding a label and then calling this command crashed it).
    int count = 0;
    int skipped = 0;
    for (const au::trackedit::Track& track : trackeditProject->trackList()) {
        if (track.type == au::trackedit::TrackType::Label) {
            ++skipped;
            continue;
        }
        trackPlaybackControl()->setMuted(track.id, mute);
        ++count;
    }

    std::string message = (mute ? "Muted " : "Unmuted ") + std::to_string(count) + " track(s)";
    if (skipped > 0) {
        message += " (" + std::to_string(skipped) + " label track(s) skipped - they have no mute state)";
    }
    return make_response(request, make_ret(Ret::Code::Ok, message));
}

Response AudacityCommandsController::handleTrackMuteAll(const Request& request)
{
    return handleTrackMuteOrUnmuteAll(request, true);
}

Response AudacityCommandsController::handleTrackUnmuteAll(const Request& request)
{
    return handleTrackMuteOrUnmuteAll(request, false);
}

Response AudacityCommandsController::handleProjectGetMetadata(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!metadata()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("IMetadata not available")));
    }

    au::project::ProjectMeta meta = metadata()->tags();

    JsonObject root;
    root["artist"] = meta.artist;
    root["trackTitle"] = meta.trackTitle;
    root["album"] = meta.album;
    root["trackNumber"] = meta.trackNumber;
    root["year"] = meta.year;
    root["comments"] = meta.comments;

    JsonDocument doc(root);
    std::string json(doc.toJson(JsonDocument::Format::Compact).constChar());

    Response response = make_response(request, make_ret(Ret::Code::Ok, std::string("project-get-metadata")));
    response.data = json;
    return response;
}

Response AudacityCommandsController::handleProjectSetMetadata(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!metadata()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("IMetadata not available")));
    }

    au::project::ProjectMeta meta = metadata()->tags();
    std::vector<std::string> applied;

    muse::Val artistVal = request.query.param("artist");
    if (!artistVal.isNull()) {
        meta.artist = artistVal.toString();
        applied.push_back("artist");
    }
    muse::Val titleVal = request.query.param("track_title");
    if (!titleVal.isNull()) {
        meta.trackTitle = titleVal.toString();
        applied.push_back("track_title");
    }
    muse::Val albumVal = request.query.param("album");
    if (!albumVal.isNull()) {
        meta.album = albumVal.toString();
        applied.push_back("album");
    }
    muse::Val trackNumVal = request.query.param("track_number");
    if (!trackNumVal.isNull()) {
        meta.trackNumber = trackNumVal.toString();
        applied.push_back("track_number");
    }
    muse::Val yearVal = request.query.param("year");
    if (!yearVal.isNull()) {
        meta.year = yearVal.toString();
        applied.push_back("year");
    }
    muse::Val commentsVal = request.query.param("comments");
    if (!commentsVal.isNull()) {
        meta.comments = commentsVal.toString();
        applied.push_back("comments");
    }

    if (applied.empty()) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("No properties given - pass at least one of "
                                                              "artist/track_title/album/track_number/year/comments")));
    }

    metadata()->setTags(meta);

    std::string message = "Updated: ";
    for (size_t i = 0; i < applied.size(); ++i) {
        message += applied[i];
        if (i + 1 < applied.size()) {
            message += ", ";
        }
    }
    return make_response(request, make_ret(Ret::Code::Ok, message));
}

Response AudacityCommandsController::handleSetClipPitch(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!trackeditInteraction()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("ITrackeditInteraction not available")));
    }

    au::trackedit::ClipKey key = parseLabelKey(request.query.param("key").toString());
    if (!key.isValid()) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("Invalid or missing 'key' argument - expected format \"trackId:itemId\"")));
    }
    muse::Val pitchVal = request.query.param("semitones");
    if (pitchVal.isNull()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Missing required 'semitones' argument")));
    }
    int semitones = 0;
    try {
        semitones = static_cast<int>(toFiniteDouble(pitchVal));
    } catch (const std::exception& e) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Invalid 'semitones' argument: ") + e.what()));
    }

    bool ok = trackeditInteraction()->changeClipPitch(key, semitones);
    return make_response(request, make_ret(ok ? Ret::Code::Ok : Ret::Code::UnknownError,
                                             std::string(ok ? "Clip pitch set" : "Failed to set clip pitch - the clip may not exist")));
}

Response AudacityCommandsController::handleResetClipPitch(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!trackeditInteraction()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("ITrackeditInteraction not available")));
    }

    au::trackedit::ClipKey key = parseLabelKey(request.query.param("key").toString());
    if (!key.isValid()) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("Invalid or missing 'key' argument - expected format \"trackId:itemId\"")));
    }

    bool ok = trackeditInteraction()->resetClipPitch(key);
    return make_response(request, make_ret(ok ? Ret::Code::Ok : Ret::Code::UnknownError,
                                             std::string(ok ? "Clip pitch reset" : "Failed to reset clip pitch - the clip may not exist")));
}

Response AudacityCommandsController::handleSetClipSpeed(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!trackeditInteraction()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("ITrackeditInteraction not available")));
    }

    au::trackedit::ClipKey key = parseLabelKey(request.query.param("key").toString());
    if (!key.isValid()) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("Invalid or missing 'key' argument - expected format \"trackId:itemId\"")));
    }
    muse::Val speedVal = request.query.param("speed");
    if (speedVal.isNull()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Missing required 'speed' argument")));
    }
    double speed = 1.0;
    try {
        speed = toFiniteDouble(speedVal);
    } catch (const std::exception& e) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Invalid 'speed' argument: ") + e.what()));
    }

    bool ok = trackeditInteraction()->changeClipSpeed(key, speed);
    return make_response(request, make_ret(ok ? Ret::Code::Ok : Ret::Code::UnknownError,
                                             std::string(ok ? "Clip speed set" : "Failed to set clip speed - the clip may not exist")));
}

Response AudacityCommandsController::handleResetClipSpeed(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!trackeditInteraction()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("ITrackeditInteraction not available")));
    }

    au::trackedit::ClipKey key = parseLabelKey(request.query.param("key").toString());
    if (!key.isValid()) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("Invalid or missing 'key' argument - expected format \"trackId:itemId\"")));
    }

    bool ok = trackeditInteraction()->resetClipSpeed(key);
    return make_response(request, make_ret(ok ? Ret::Code::Ok : Ret::Code::UnknownError,
                                             std::string(ok ? "Clip speed reset" : "Failed to reset clip speed - the clip may not exist")));
}

Response AudacityCommandsController::handleRenderClipPitchSpeed(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!trackeditInteraction()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("ITrackeditInteraction not available")));
    }

    au::trackedit::ClipKey key = parseLabelKey(request.query.param("key").toString());
    if (!key.isValid()) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("Invalid or missing 'key' argument - expected format \"trackId:itemId\"")));
    }

    bool ok = trackeditInteraction()->renderClipPitchAndSpeed(key);
    return make_response(request, make_ret(ok ? Ret::Code::Ok : Ret::Code::UnknownError,
                                             std::string(ok ? "Clip pitch/speed rendered permanently" : "Failed to render - the clip may not exist")));
}

Response AudacityCommandsController::handleResetClipPitchSpeed(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!trackeditInteraction()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("ITrackeditInteraction not available")));
    }

    au::trackedit::ClipKey key = parseLabelKey(request.query.param("key").toString());
    if (!key.isValid()) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("Invalid or missing 'key' argument - expected format \"trackId:itemId\"")));
    }

    bool ok = trackeditInteraction()->resetClipPitchAndSpeed(key);
    return make_response(request, make_ret(ok ? Ret::Code::Ok : Ret::Code::UnknownError,
                                             std::string(ok ? "Clip pitch/speed reset" : "Failed to reset - the clip may not exist")));
}

Response AudacityCommandsController::handleSplitClipAtSilences(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!trackeditInteraction()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("ITrackeditInteraction not available")));
    }

    au::trackedit::ClipKey key = parseLabelKey(request.query.param("key").toString());
    if (!key.isValid()) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("Invalid or missing 'key' argument - expected format \"trackId:itemId\"")));
    }

    bool ok = trackeditInteraction()->splitClipsAtSilences({ key });
    return make_response(request, make_ret(ok ? Ret::Code::Ok : Ret::Code::UnknownError,
                                             std::string(ok ? "Clip split at detected silences" : "Failed to split - the clip may not exist")));
}

Response AudacityCommandsController::handleSplitRangeAtSilences(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!trackeditInteraction()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("ITrackeditInteraction not available")));
    }

    muse::Val startVal = request.query.param("start");
    muse::Val endVal = request.query.param("end");
    if (startVal.isNull() || endVal.isNull()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Missing required 'start'/'end' arguments")));
    }
    double start = 0.0;
    double end = 0.0;
    try {
        start = toFiniteDouble(startVal);
        end = toFiniteDouble(endVal);
    } catch (const std::exception& e) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Invalid 'start'/'end' argument: ") + e.what()));
    }

    au::trackedit::TrackIdList tracks = selectedOrEmptyTracks();
    if (tracks.empty()) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("No tracks selected - call select-tracks first")));
    }

    bool ok = trackeditInteraction()->splitRangeSelectionAtSilences(tracks, start, end);
    return make_response(request, make_ret(ok ? Ret::Code::Ok : Ret::Code::UnknownError,
                                             std::string(ok ? "Selection split at detected silences" : "Failed to split")));
}

Response AudacityCommandsController::handleTrimClip(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!trackeditInteraction()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("ITrackeditInteraction not available")));
    }

    au::trackedit::ClipKey key = parseLabelKey(request.query.param("key").toString());
    if (!key.isValid()) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("Invalid or missing 'key' argument - expected format \"trackId:itemId\"")));
    }
    std::string side = request.query.param("side").toString();
    if (side != "left" && side != "right") {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("'side' must be \"left\" or \"right\"")));
    }
    double delta = 0.0;
    double minDur = 0.0;
    std::string argError;
    if (!tryParseFiniteDouble(request.query.param("delta_sec"), "delta_sec", true, 0.0, delta, argError)
        || !tryParseFiniteDouble(request.query.param("min_clip_duration"), "min_clip_duration", false, 0.0, minDur, argError)) {
        return make_response(request, make_ret(Ret::Code::UnknownError, argError));
    }

    bool ok = side == "left"
              ? trackeditInteraction()->trimClipsLeft({ key }, delta, minDur, true, au::trackedit::UndoPushType::NONE)
              : trackeditInteraction()->trimClipsRight({ key }, delta, minDur, true, au::trackedit::UndoPushType::NONE);
    return make_response(request, make_ret(ok ? Ret::Code::Ok : Ret::Code::UnknownError,
                                             std::string(ok ? "Clip trimmed" : "Failed to trim - the clip may not exist")));
}

Response AudacityCommandsController::handleStretchClip(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!trackeditInteraction()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("ITrackeditInteraction not available")));
    }

    au::trackedit::ClipKey key = parseLabelKey(request.query.param("key").toString());
    if (!key.isValid()) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("Invalid or missing 'key' argument - expected format \"trackId:itemId\"")));
    }
    std::string side = request.query.param("side").toString();
    if (side != "left" && side != "right") {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("'side' must be \"left\" or \"right\"")));
    }
    double delta = 0.0;
    double minDur = 0.0;
    std::string argError;
    if (!tryParseFiniteDouble(request.query.param("delta_sec"), "delta_sec", true, 0.0, delta, argError)
        || !tryParseFiniteDouble(request.query.param("min_clip_duration"), "min_clip_duration", false, 0.0, minDur, argError)) {
        return make_response(request, make_ret(Ret::Code::UnknownError, argError));
    }

    bool ok = side == "left"
              ? trackeditInteraction()->stretchClipsLeft({ key }, delta, minDur, true, au::trackedit::UndoPushType::NONE)
              : trackeditInteraction()->stretchClipsRight({ key }, delta, minDur, true, au::trackedit::UndoPushType::NONE);
    return make_response(request, make_ret(ok ? Ret::Code::Ok : Ret::Code::UnknownError,
                                             std::string(ok ? "Clip stretched" : "Failed to stretch - the clip may not exist")));
}

Response AudacityCommandsController::handleNearestZeroCrossing(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!trackeditInteraction()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("ITrackeditInteraction not available")));
    }

    muse::Val timeVal = request.query.param("time");
    if (timeVal.isNull()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Missing required 'time' argument")));
    }
    double time = 0.0;
    try {
        time = toFiniteDouble(timeVal);
    } catch (const std::exception& e) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Invalid 'time' argument: ") + e.what()));
    }

    double nearest = trackeditInteraction()->nearestZeroCrossing(time);

    JsonObject root;
    root["time"] = nearest;
    JsonDocument doc(root);
    std::string json(doc.toJson(JsonDocument::Format::Compact).constChar());

    Response response = make_response(request, make_ret(Ret::Code::Ok, std::string("nearest-zero-crossing")));
    response.data = json;
    return response;
}

Response AudacityCommandsController::handleSetClipColor(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!trackeditInteraction()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("ITrackeditInteraction not available")));
    }

    au::trackedit::ClipKey key = parseLabelKey(request.query.param("key").toString());
    if (!key.isValid()) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("Invalid or missing 'key' argument - expected format \"trackId:itemId\"")));
    }
    muse::Val colorVal = request.query.param("color_index");
    if (colorVal.isNull()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Missing required 'color_index' argument (0-9)")));
    }
    int colorIndex = 0;
    try {
        colorIndex = static_cast<int>(toFiniteDouble(colorVal));
    } catch (const std::exception& e) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Invalid 'color_index' argument: ") + e.what()));
    }
    if (colorIndex < 0 || colorIndex > au::trackedit::CLIP_COLOR_COUNT) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("'color_index' must be 0-") + std::to_string(au::trackedit::CLIP_COLOR_COUNT)));
    }

    bool ok = trackeditInteraction()->changeClipColor(key, colorIndex);
    return make_response(request, make_ret(ok ? Ret::Code::Ok : Ret::Code::UnknownError,
                                             std::string(ok ? "Clip color set" : "Failed to set clip color - the clip may not exist")));
}

Response AudacityCommandsController::handleSetTrackColor(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!trackeditInteraction()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("ITrackeditInteraction not available")));
    }

    muse::Val colorVal = request.query.param("color_index");
    if (colorVal.isNull()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Missing required 'color_index' argument (0-9)")));
    }
    int colorIndex = 0;
    try {
        colorIndex = static_cast<int>(toFiniteDouble(colorVal));
    } catch (const std::exception& e) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Invalid 'color_index' argument: ") + e.what()));
    }
    if (colorIndex < 0 || colorIndex > au::trackedit::CLIP_COLOR_COUNT) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("'color_index' must be 0-") + std::to_string(au::trackedit::CLIP_COLOR_COUNT)));
    }

    au::trackedit::TrackIdList tracks = selectedOrEmptyTracks();
    if (tracks.empty()) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("No tracks selected - call select-tracks first")));
    }

    bool ok = trackeditInteraction()->changeTracksColor(tracks, colorIndex);
    return make_response(request, make_ret(ok ? Ret::Code::Ok : Ret::Code::UnknownError,
                                             std::string(ok ? "Track color set" : "Failed to set track color")));
}

Response AudacityCommandsController::handleEditCut(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!trackeditInteraction() || !selectionController()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Trackedit/selection interfaces not available")));
    }

    au::trackedit::TrackIdList tracks = selectedOrEmptyTracks();
    if (tracks.empty()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No tracks selected")));
    }
    secs_t begin = selectionController()->dataSelectedStartTime();
    secs_t end = selectionController()->dataSelectedEndTime();

    bool ok = trackeditInteraction()->cutItemDataIntoClipboard(tracks, begin, end, true, true);
    std::string message = ok ? "Cut to clipboard" : "Failed to cut - check that a time range is selected";
    return make_response(request, make_ret(ok ? Ret::Code::Ok : Ret::Code::UnknownError, message));
}

Response AudacityCommandsController::handleEditCopy(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!trackeditInteraction() || !selectionController()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Trackedit/selection interfaces not available")));
    }

    au::trackedit::TrackIdList tracks = selectedOrEmptyTracks();
    if (tracks.empty()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No tracks selected")));
    }
    secs_t begin = selectionController()->dataSelectedStartTime();
    secs_t end = selectionController()->dataSelectedEndTime();

    bool ok = trackeditInteraction()->copyContinuousTrackDataIntoClipboard(tracks.front(), begin, end);
    std::string message = ok ? "Copied to clipboard" : "Failed to copy - check that a time range is selected";
    return make_response(request, make_ret(ok ? Ret::Code::Ok : Ret::Code::UnknownError, message));
}

Response AudacityCommandsController::handleEditPaste(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!trackeditInteraction() || !selectionController()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Trackedit/selection interfaces not available")));
    }

    secs_t begin = selectionController()->dataSelectedStartTime();

    muse::Ret ret = trackeditInteraction()->pasteFromClipboard(begin, true);
    return make_response(request, ret);
}

Response AudacityCommandsController::handleEditDelete(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!trackeditInteraction() || !selectionController()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Trackedit/selection interfaces not available")));
    }

    au::trackedit::TrackIdList tracks = selectedOrEmptyTracks();
    if (tracks.empty()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No tracks selected")));
    }
    secs_t begin = selectionController()->dataSelectedStartTime();
    secs_t end = selectionController()->dataSelectedEndTime();

    bool ok = trackeditInteraction()->removeTracksData(tracks, begin, end, true);
    std::string message = ok ? "Deleted selection" : "Failed to delete - check that a time range is selected";
    return make_response(request, make_ret(ok ? Ret::Code::Ok : Ret::Code::UnknownError, message));
}

Response AudacityCommandsController::handleEditSplit(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!trackeditInteraction() || !selectionController()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Trackedit/selection interfaces not available")));
    }

    au::trackedit::TrackIdList tracks = selectedOrEmptyTracks();
    if (tracks.empty()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No tracks selected")));
    }
    secs_t begin = selectionController()->dataSelectedStartTime();
    secs_t end = selectionController()->dataSelectedEndTime();

    std::vector<secs_t> pivots;
    pivots.push_back(begin);
    if (end > begin) {
        pivots.push_back(end);
    }

    bool ok = trackeditInteraction()->splitTracksAt(tracks, pivots);
    std::string message = ok ? "Split at selection" : "Failed to split";
    return make_response(request, make_ret(ok ? Ret::Code::Ok : Ret::Code::UnknownError, message));
}

Response AudacityCommandsController::handleEditTrim(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!trackeditInteraction() || !selectionController()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Trackedit/selection interfaces not available")));
    }

    au::trackedit::TrackIdList tracks = selectedOrEmptyTracks();
    if (tracks.empty()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No tracks selected")));
    }
    secs_t begin = selectionController()->dataSelectedStartTime();
    secs_t end = selectionController()->dataSelectedEndTime();

    bool ok = trackeditInteraction()->trimTracksData(tracks, begin, end);
    std::string message = ok ? "Trimmed to selection" : "Failed to trim - check that a time range is selected";
    return make_response(request, make_ret(ok ? Ret::Code::Ok : Ret::Code::UnknownError, message));
}

Response AudacityCommandsController::handleEditSilence(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!trackeditInteraction() || !selectionController()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Trackedit/selection interfaces not available")));
    }

    au::trackedit::TrackIdList tracks = selectedOrEmptyTracks();
    if (tracks.empty()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No tracks selected")));
    }
    secs_t begin = selectionController()->dataSelectedStartTime();
    secs_t end = selectionController()->dataSelectedEndTime();

    bool ok = trackeditInteraction()->silenceTracksData(tracks, begin, end);
    std::string message = ok ? "Replaced selection with silence" : "Failed to silence - check that a time range is selected";
    return make_response(request, make_ret(ok ? Ret::Code::Ok : Ret::Code::UnknownError, message));
}

Response AudacityCommandsController::handleEditDuplicate(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!trackeditInteraction() || !selectionController()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Trackedit/selection interfaces not available")));
    }

    au::trackedit::TrackIdList tracks = selectedOrEmptyTracks();
    if (tracks.empty()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No tracks selected")));
    }
    secs_t begin = selectionController()->dataSelectedStartTime();
    secs_t end = selectionController()->dataSelectedEndTime();

    bool ok = trackeditInteraction()->duplicateSelectedOnTracks(tracks, begin, end);
    std::string message = ok ? "Duplicated selection" : "Failed to duplicate - check that a time range is selected";
    return make_response(request, make_ret(ok ? Ret::Code::Ok : Ret::Code::UnknownError, message));
}

Response AudacityCommandsController::handleEditUndo(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!trackeditInteraction()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("ITrackeditInteraction not available")));
    }

    if (!trackeditInteraction()->canUndo()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Nothing to undo")));
    }
    bool ok = trackeditInteraction()->undo();
    return make_response(request, make_ret(ok ? Ret::Code::Ok : Ret::Code::UnknownError, std::string(ok ? "Undone" : "Undo failed")));
}

Response AudacityCommandsController::handleEditRedo(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!trackeditInteraction()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("ITrackeditInteraction not available")));
    }

    if (!trackeditInteraction()->canRedo()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Nothing to redo")));
    }
    bool ok = trackeditInteraction()->redo();
    return make_response(request, make_ret(ok ? Ret::Code::Ok : Ret::Code::UnknownError, std::string(ok ? "Redone" : "Redo failed")));
}

Response AudacityCommandsController::handleEditSplitNew(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!trackeditInteraction() || !selectionController()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Trackedit/selection interfaces not available")));
    }

    au::trackedit::TrackIdList tracks = selectedOrEmptyTracks();
    if (tracks.empty()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No tracks selected")));
    }
    secs_t begin = selectionController()->dataSelectedStartTime();
    secs_t end = selectionController()->dataSelectedEndTime();

    bool ok = trackeditInteraction()->splitRangeSelectionIntoNewTracks(tracks, begin, end);
    std::string message = ok ? "Split selection into new track(s)" : "Failed to split into new track - check that a time range is selected";
    return make_response(request, make_ret(ok ? Ret::Code::Ok : Ret::Code::UnknownError, message));
}

Response AudacityCommandsController::handleEditSplitCut(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!trackeditInteraction() || !selectionController()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Trackedit/selection interfaces not available")));
    }

    au::trackedit::TrackIdList tracks = selectedOrEmptyTracks();
    if (tracks.empty()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No tracks selected")));
    }
    secs_t begin = selectionController()->dataSelectedStartTime();
    secs_t end = selectionController()->dataSelectedEndTime();

    bool ok = trackeditInteraction()->splitCutSelectedOnTracks(tracks, begin, end);
    std::string message = ok ? "Cut selection, leaving a gap" : "Failed to split-cut - check that a time range is selected";
    return make_response(request, make_ret(ok ? Ret::Code::Ok : Ret::Code::UnknownError, message));
}

Response AudacityCommandsController::handleEditSplitDelete(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!trackeditInteraction() || !selectionController()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Trackedit/selection interfaces not available")));
    }

    au::trackedit::TrackIdList tracks = selectedOrEmptyTracks();
    if (tracks.empty()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No tracks selected")));
    }
    secs_t begin = selectionController()->dataSelectedStartTime();
    secs_t end = selectionController()->dataSelectedEndTime();

    bool ok = trackeditInteraction()->splitDeleteSelectedOnTracks(tracks, begin, end);
    std::string message = ok ? "Deleted selection, leaving a gap" : "Failed to split-delete - check that a time range is selected";
    return make_response(request, make_ret(ok ? Ret::Code::Ok : Ret::Code::UnknownError, message));
}

Response AudacityCommandsController::handleEditDisjoin(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!trackeditInteraction() || !selectionController()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Trackedit/selection interfaces not available")));
    }

    au::trackedit::TrackIdList tracks = selectedOrEmptyTracks();
    if (tracks.empty()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No tracks selected")));
    }
    secs_t begin = selectionController()->dataSelectedStartTime();
    secs_t end = selectionController()->dataSelectedEndTime();

    bool ok = trackeditInteraction()->splitRangeSelectionAtSilences(tracks, begin, end);
    std::string message = ok ? "Split at detected silences" : "Failed to disjoin - check that a time range is selected";
    return make_response(request, make_ret(ok ? Ret::Code::Ok : Ret::Code::UnknownError, message));
}

Response AudacityCommandsController::handleEditJoin(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!trackeditInteraction() || !selectionController()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Trackedit/selection interfaces not available")));
    }

    au::trackedit::TrackIdList tracks = selectedOrEmptyTracks();
    if (tracks.empty()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No tracks selected")));
    }
    secs_t begin = selectionController()->dataSelectedStartTime();
    secs_t end = selectionController()->dataSelectedEndTime();

    bool ok = trackeditInteraction()->mergeSelectedOnTracks(tracks, begin, end);
    std::string message = ok ? "Joined selected clips" : "Failed to join - check that a time range is selected";
    return make_response(request, make_ret(ok ? Ret::Code::Ok : Ret::Code::UnknownError, message));
}

Response AudacityCommandsController::handleSelectZeroCrossing(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!trackeditInteraction() || !selectionController()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Trackedit/selection interfaces not available")));
    }

    secs_t begin = selectionController()->dataSelectedStartTime();
    secs_t end = selectionController()->dataSelectedEndTime();

    double newBegin = trackeditInteraction()->nearestZeroCrossing(begin);
    double newEnd = (end > begin) ? trackeditInteraction()->nearestZeroCrossing(end) : newBegin;

    selectionController()->setSelectedAllAudioData(newBegin, newEnd);

    std::string message = "Adjusted selection to zero crossings: " + std::to_string(newBegin) + "-" + std::to_string(newEnd) + "s";
    return make_response(request, make_ret(Ret::Code::Ok, message));
}

Response AudacityCommandsController::handleProjectOpen(const Request& request)
{
    if (!projectFilesController()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("IProjectFilesController not available")));
    }

    std::string path = request.query.param("path").toString();
    if (path.empty()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Missing required 'path' argument")));
    }

    muse::io::path_t projectPath(path);
    project::ProjectFile file(projectPath);
    muse::Ret ret = projectFilesController()->openProject(file);
    return make_response(request, ret);
}

Response AudacityCommandsController::handleProjectImport(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!importer()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("IImporter not available")));
    }

    std::string path = request.query.param("path").toString();
    if (path.empty()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Missing required 'path' argument")));
    }

    bool ok = importer()->import(muse::io::path_t(path));
    std::string message = ok ? "Imported " + path : "Failed to import " + path;
    return make_response(request, make_ret(ok ? Ret::Code::Ok : Ret::Code::UnknownError, message));
}

Response AudacityCommandsController::handleProjectClose(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }

    project::IAudacityProjectPtr project = globalContext() ? globalContext()->currentProject() : nullptr;
    if (!project) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }

    //! NOTE ProjectActionsController::closeOpenedProject() unconditionally calls
    //! interactive()->closeAllDialogsSync() as its last step, regardless of whether the project
    //! had unsaved changes. That call opens a nested QEventLoop waiting for every tracked
    //! dialog/page object to report itself closed - live-tested and confirmed (via WinDbg thread
    //! dump: main thread stuck in Interactive::closeAllDialogsSync -> closeObjectsSync ->
    //! QEventLoop::exec at interactive.cpp:804/880) that this never resolves when called from
    //! the MCP command-dispatch call path, hanging the connection indefinitely. This is not
    //! specific to unsaved changes - it reproduces even right after a clean save. So we bypass
    //! IProjectFilesController::closeOpenedProject() entirely here and do the same close
    //! sequence it performs before that call (refuse-if-dirty, then project->close() +
    //! globalContext()->setCurrentProject(nullptr)) without going through the interactive-dialog
    //! cleanup step.
    if (project->hasUnsavedChanges()) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("Project has unsaved changes - call project-save-as first to "
                                                              "save, then close.")));
    }

    stopPlaybackIfRunning();

    project->close();
    globalContext()->setCurrentProject(nullptr);

    return make_response(request, make_ret(Ret::Code::Ok, std::string("Project closed")));
}

Response AudacityCommandsController::handleProjectSaveAs(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!projectFilesController()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("IProjectFilesController not available")));
    }

    std::string path = request.query.param("path").toString();
    if (path.empty()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Missing required 'path' argument")));
    }

    stopPlaybackIfRunning();

    bool ok = projectFilesController()->saveProjectLocally(muse::io::path_t(path));
    std::string message = ok ? "Project saved to " + path : "Failed to save project to " + path;
    return make_response(request, make_ret(ok ? Ret::Code::Ok : Ret::Code::UnknownError, message));
}

Response AudacityCommandsController::handleProjectExport(const Request& request)
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

    bool overwrite = request.query.param("overwrite").toBool();
    if (!overwrite && fileSystem() && fileSystem()->exists(muse::io::path_t(path))) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("File already exists at '") + path
                                                 + "' - pass overwrite=true to replace it"));
    }

    au::importexport::IExporter::Options options;
    options[au::importexport::IExporter::OptionKey::Format] = muse::Val(std::string("WAV (Microsoft)"));
    options[au::importexport::IExporter::OptionKey::ProcessType]
        = muse::Val(static_cast<int>(au::importexport::ExportProcessType::FULL_PROJECT_AUDIO));
    options[au::importexport::IExporter::OptionKey::ExportChannelsType]
        = muse::Val(static_cast<int>(au::importexport::ExportChannelsPref::ExportChannels::STEREO));
    options[au::importexport::IExporter::OptionKey::ExportSampleRate] = muse::Val(44100);

    muse::Ret ret = exporter()->exportData(muse::io::path_t(path), options);
    return make_response(request, ret);
}

Response AudacityCommandsController::handleTransportRecord(const Request& request)
{
    dispatcher()->dispatch(ActionQuery("action://record/start"));

    PlaybackStatus status = playbackController() ? playbackController()->playbackStatus() : PlaybackStatus::Stopped;
    std::string message = "transport-record: playback is now " + playbackStatusToString(status);
    return make_response(request, make_ret(Ret::Code::Ok, message));
}

Response AudacityCommandsController::handleCursorSet(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!selectionController()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("ISelectionController not available")));
    }

    muse::Val timeVal = request.query.param("time");
    if (timeVal.isNull()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Missing required 'time' argument (seconds)")));
    }
    double time = 0.0;
    try {
        time = toFiniteDouble(timeVal);
    } catch (const std::exception& e) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Invalid 'time' argument: ") + e.what()));
    }

    selectionController()->setSelectedAllAudioData(time, time);

    return make_response(request, make_ret(Ret::Code::Ok, "Cursor set to " + std::to_string(time) + "s"));
}

Response AudacityCommandsController::handleProjectGetInfo(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }

    project::IAudacityProjectPtr project = globalContext() ? globalContext()->currentProject() : nullptr;
    if (!project) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }

    au::trackedit::ITrackeditProjectPtr trackeditProject = project->trackeditProject();
    if (!trackeditProject) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No trackedit project available")));
    }

    JsonArray tracksArr;
    int labelTrackCount = 0;
    int labelCount = 0;
    for (const au::trackedit::Track& track : trackeditProject->trackList()) {
        if (track.type == au::trackedit::TrackType::Label) {
            labelTrackCount++;
            labelCount += static_cast<int>(trackeditProject->labelList(track.id).size());
            continue;
        }

        JsonObject obj;
        obj["id"] = static_cast<int>(track.id);
        obj["title"] = track.title.toStdString();
        obj["type"] = trackTypeToString(track.type);
        obj["rate"] = static_cast<double>(track.rate);
        obj["mute"] = track.mute;
        obj["solo"] = track.solo;
        obj["clipCount"] = static_cast<int>(trackeditProject->clipList(track.id).size());
        tracksArr << obj;
    }

    const double durationSec = playbackController() ? static_cast<double>(playbackController()->totalPlayTime()) : 0.0;

    JsonObject root;
    root["path"] = project->path().toString().toStdString();
    root["displayName"] = project->displayName().toStdString();
    root["isNewlyCreated"] = project->isNewlyCreated();
    root["hasUnsavedChanges"] = project->hasUnsavedChanges();
    root["durationSec"] = durationSec;
    root["trackCount"] = static_cast<int>(tracksArr.size());
    root["tracks"] = tracksArr;
    root["labelTrackCount"] = labelTrackCount;
    root["labelCount"] = labelCount;

    JsonDocument doc(root);
    std::string json(doc.toJson(JsonDocument::Format::Compact).constChar());

    Response response = make_response(request, make_ret(Ret::Code::Ok, std::string("project-get-info")));
    response.data = json;
    return response;
}

Response AudacityCommandsController::handleTrackGetInfo(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }

    project::IAudacityProjectPtr project = globalContext() ? globalContext()->currentProject() : nullptr;
    if (!project) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }

    au::trackedit::ITrackeditProjectPtr trackeditProject = project->trackeditProject();
    if (!trackeditProject) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No trackedit project available")));
    }

    muse::Val trackIdVal = request.query.param("track_id");
    if (trackIdVal.isNull()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Missing required 'track_id' argument")));
    }
    au::trackedit::TrackId trackId = au::trackedit::INVALID_TRACK;
    try {
        trackId = static_cast<au::trackedit::TrackId>(trackIdVal.toInt64());
    } catch (const std::exception& e) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Invalid 'track_id' argument: ") + e.what()));
    }

    const au::trackedit::Track* found = nullptr;
    std::vector<au::trackedit::Track> tracks = trackeditProject->trackList();
    for (const au::trackedit::Track& track : tracks) {
        if (track.id == trackId) {
            found = &track;
            break;
        }
    }
    if (!found) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No track with id ") + std::to_string(trackId)));
    }

    JsonObject root;
    root["id"] = static_cast<int>(found->id);
    root["title"] = found->title.toStdString();
    root["type"] = trackTypeToString(found->type);
    root["rate"] = static_cast<double>(found->rate);
    root["mute"] = found->mute;
    root["solo"] = found->solo;

    if (found->type == au::trackedit::TrackType::Label) {
        JsonArray labelsArr;
        for (const au::trackedit::Label& label : trackeditProject->labelList(trackId)) {
            JsonObject obj;
            obj["key"] = labelKeyToString(label.key);
            obj["title"] = label.title.toStdString();
            obj["start"] = label.startTime;
            obj["end"] = label.endTime;
            labelsArr << obj;
        }
        root["labelCount"] = static_cast<int>(labelsArr.size());
        root["labels"] = labelsArr;
    } else {
        JsonArray clipsArr;
        for (const au::trackedit::Clip& clip : trackeditProject->clipList(trackId)) {
            JsonObject obj;
            obj["key"] = clipKeyToString(clip.key);
            obj["title"] = clip.title.toStdString();
            obj["start"] = clip.startTime;
            obj["end"] = clip.endTime;
            obj["stereo"] = clip.stereo;
            clipsArr << obj;
        }
        root["clipCount"] = static_cast<int>(clipsArr.size());
        root["clips"] = clipsArr;
    }

    JsonDocument doc(root);
    std::string json(doc.toJson(JsonDocument::Format::Compact).constChar());

    Response response = make_response(request, make_ret(Ret::Code::Ok, std::string("track-get-info")));
    response.data = json;
    return response;
}

Response AudacityCommandsController::handleSelectClip(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!selectionController()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("ISelectionController not available")));
    }

    std::string keyStr = request.query.param("key").toString();
    if (keyStr.empty()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Missing required 'key' argument")));
    }
    au::trackedit::ClipKey key = parseLabelKey(keyStr);
    if (!key.isValid()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Invalid 'key' - expected \"trackId:itemId\"")));
    }

    project::IAudacityProjectPtr project = globalContext() ? globalContext()->currentProject() : nullptr;
    if (!project) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }

    au::trackedit::ITrackeditProjectPtr trackeditProject = project->trackeditProject();
    if (!trackeditProject) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No trackedit project available")));
    }

    const au::trackedit::Clip* found = nullptr;
    for (const au::trackedit::Clip& clip : trackeditProject->clipList(key.trackId)) {
        if (clip.key.itemId == key.itemId) {
            found = &clip;
            break;
        }
    }
    if (!found) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No clip with key ") + keyStr));
    }

    selectionController()->setSelectedTracks({ key.trackId });
    selectionController()->setSelectedClips({ key });
    selectionController()->setSelectedAllAudioData(found->startTime, found->endTime);

    return make_response(request, make_ret(Ret::Code::Ok, std::string("Selected clip ") + keyStr));
}

Response AudacityCommandsController::handleTransportGetPlayPosition(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!selectionController()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("ISelectionController not available")));
    }

    //! NOTE playbackState() is reached via globalContext(), not a new
    //! ContextInject - IPlaybackState is deliberately the only sanctioned way
    //! to read the live player's position (see its own header comment: direct
    //! IPlayer access is reserved for playback control, not queries).
    double playPosition = 0.0;
    bool isPlaying = false;
    if (globalContext() && globalContext()->playbackState()) {
        playPosition = static_cast<double>(globalContext()->playbackState()->playbackPosition());
        isPlaying = globalContext()->playbackState()->isPlaying();
    }

    JsonObject root;
    root["playPosition"] = playPosition;
    root["isPlaying"] = isPlaying;
    root["selectionStart"] = static_cast<double>(selectionController()->dataSelectedStartTime());
    root["selectionEnd"] = static_cast<double>(selectionController()->dataSelectedEndTime());

    JsonDocument doc(root);
    std::string json(doc.toJson(JsonDocument::Format::Compact).constChar());

    Response response = make_response(request, make_ret(Ret::Code::Ok, std::string("transport-get-play-position")));
    response.data = json;
    return response;
}

Response AudacityCommandsController::handleListEffects(const Request& request)
{
    if (!effectsProvider()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("IEffectsProvider not available")));
    }

    const std::string categoryFilter = toLowerAscii(request.query.param("category").toString());
    const std::string familyFilter = toLowerAscii(request.query.param("family").toString());
    const std::string searchFilter = toLowerAscii(request.query.param("search").toString());

    muse::Val limitVal = request.query.param("limit");
    int limit = 100;
    if (!limitVal.isNull()) {
        try {
            limit = static_cast<int>(toFiniteDouble(limitVal));
        } catch (const std::exception&) {
            limit = 100;
        }
    }
    if (limit < 1) {
        limit = 1;
    }

    JsonArray effectsArr;
    int totalMatched = 0;
    for (const au::effects::EffectMeta& meta : effectsProvider()->effectMetaList()) {
        const std::string title = meta.title.toStdString();
        const std::string category = meta.category.toStdString();
        const std::string family = effectFamilyToString(meta.family);

        if (!categoryFilter.empty() && toLowerAscii(category).find(categoryFilter) == std::string::npos) {
            continue;
        }
        if (!familyFilter.empty() && toLowerAscii(family) != familyFilter) {
            continue;
        }
        if (!searchFilter.empty() && toLowerAscii(title).find(searchFilter) == std::string::npos) {
            continue;
        }

        totalMatched++;
        if (static_cast<int>(effectsArr.size()) >= limit) {
            continue;
        }

        JsonObject obj;
        obj["title"] = title;
        obj["id"] = meta.id.toStdString();
        obj["family"] = family;
        obj["type"] = effectTypeToString(meta.type);
        obj["category"] = category;
        obj["vendor"] = meta.vendor.toStdString();
        obj["isRealtimeCapable"] = meta.isRealtimeCapable;
        obj["isActivated"] = meta.isActivated;
        effectsArr << obj;
    }

    JsonObject root;
    root["effects"] = effectsArr;
    root["returnedCount"] = static_cast<int>(effectsArr.size());
    root["totalMatched"] = totalMatched;

    JsonDocument doc(root);
    std::string json(doc.toJson(JsonDocument::Format::Compact).constChar());

    Response response = make_response(request, make_ret(Ret::Code::Ok, std::string("list-effects")));
    response.data = json;
    return response;
}

au::effects::RealtimeEffectStatePtr AudacityCommandsController::realtimeEffectAt(au::trackedit::TrackId trackId, int index) const
{
    if (!realtimeEffectService()) {
        return nullptr;
    }
    std::optional<std::vector<au::effects::RealtimeEffectStatePtr> > stack = realtimeEffectService()->effectStack(trackId);
    if (!stack.has_value() || index < 0 || index >= static_cast<int>(stack->size())) {
        return nullptr;
    }
    return (*stack)[static_cast<size_t>(index)];
}

Response AudacityCommandsController::handleAddRealtimeEffects(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!realtimeEffectService()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("IRealtimeEffectService not available")));
    }

    muse::Val trackIdVal = request.query.param("track_id");
    const std::string effectIds = request.query.param("effect_ids").toString();
    if (trackIdVal.isNull() || effectIds.empty()) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("Missing required 'track_id'/'effect_ids' arguments "
                                                              "('effect_ids' is '|' separated; -2 is the Master bus)")));
    }

    au::trackedit::TrackId trackId = au::trackedit::INVALID_TRACK;
    try {
        trackId = static_cast<au::trackedit::TrackId>(trackIdVal.toInt64());
    } catch (const std::exception& e) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Invalid 'track_id' argument: ") + e.what()));
    }

    auto split = [](const std::string& in, char sep) {
        std::vector<std::string> out;
        std::stringstream ss(in);
        std::string item;
        while (std::getline(ss, item, sep)) {
            out.push_back(item);
        }
        //! getline yields nothing after a trailing separator, so "a|b|" reads as two
        //! fields. The last entry is empty whenever the final effect takes no
        //! parameters, and dropping it would fail the one-to-one count check.
        if (!in.empty() && in.back() == sep) {
            out.push_back(std::string());
        }
        return out;
    };

    //! Effect ids embed a path and a hash, so '|' separates the effects and ';'
    //! the "id=value" pairs within one effect's parameters - neither appears in an
    //! effect id or in a parameter id.
    const std::vector<std::string> ids = split(effectIds, '|');
    std::vector<std::string> paramSets = split(request.query.param("parameters_list").toString(), '|');
    if (paramSets.empty()) {
        paramSets.resize(ids.size());
    }
    if (paramSets.size() != ids.size()) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("'parameters_list' has ") + std::to_string(paramSets.size())
                                                 + " entries but 'effect_ids' has " + std::to_string(ids.size())
                                                 + " - they must correspond one to one (use an empty entry for none)"));
    }

    std::vector<std::string> added;
    for (size_t i = 0; i < ids.size(); ++i) {
        if (ids[i].empty()) {
            continue;
        }

        au::effects::RealtimeEffectStatePtr state
            =realtimeEffectService()->addRealtimeEffect(trackId, String::fromStdString(ids[i]));
        if (!state) {
            std::string message = "Added " + std::to_string(added.size()) + " of " + std::to_string(ids.size())
                                  + " effects, then '" + ids[i] + "' failed - check the effect_id is exact "
                                  "(the \"id\" field from list-effects, not the title) and that it is realtime capable";
            if (!added.empty()) {
                message += ". Already added: ";
                for (size_t j = 0; j < added.size(); ++j) {
                    message += added[j] + (j + 1 < added.size() ? ", " : "");
                }
            }
            return make_response(request, make_ret(Ret::Code::UnknownError, message));
        }

        std::optional<std::vector<au::effects::RealtimeEffectStatePtr> > stack = realtimeEffectService()->effectStack(trackId);
        const int index = stack.has_value() ? static_cast<int>(stack->size()) - 1 : -1;
        added.push_back(ids[i]);

        if (paramSets[i].empty() || index < 0) {
            continue;
        }

        //! Parameters are applied here, while this effect is freshly added and its
        //! editor cannot yet be open. That ordering matters: a write made while the
        //! plug-in's own editor is open is reverted (its view pushes the stored
        //! settings back over it), so building the chain and configuring it in one
        //! call is what makes the configuration stick.
        au::effects::EffectInstanceId instanceId = realtimeEffectInstanceId(state);
        if (instanceId == au::effects::EffectInstanceId() || !effectParametersProvider()) {
            continue;
        }

        std::vector<std::pair<muse::String, double> > writes;
        bool parsed = true;
        for (const std::string& pair : split(paramSets[i], ';')) {
            if (pair.empty()) {
                continue;
            }
            const auto eq = pair.find('=');
            if (eq == std::string::npos || eq == 0 || eq + 1 >= pair.size()) {
                parsed = false;
                break;
            }
            try {
                writes.emplace_back(String::fromStdString(pair.substr(0, eq)),
                                    toFiniteDouble(muse::Val(pair.substr(eq + 1))));
            } catch (const std::exception&) {
                parsed = false;
                break;
            }
        }
        if (!parsed) {
            return make_response(request, make_ret(Ret::Code::UnknownError,
                                                     std::string("Added '") + ids[i] + "' but its parameters are malformed - "
                                                     "expected \"id=value\" pairs separated by ';'"));
        }

        //! Same shape as set-effect-parameters: open every gesture, write every
        //! value, then close them, so the whole set lands in one commit.
        for (const auto& w : writes) {
            effectParametersProvider()->beginParameterGesture(instanceId, w.first);
        }
        for (const auto& w : writes) {
            effectParametersProvider()->setParameterValue(instanceId, w.first, w.second);
        }
        for (const auto& w : writes) {
            effectParametersProvider()->endParameterGesture(instanceId, w.first);
        }
    }

    std::string message = "Added " + std::to_string(added.size()) + " realtime effects: ";
    for (size_t i = 0; i < added.size(); ++i) {
        message += added[i] + (i + 1 < added.size() ? ", " : "");
    }
    return make_response(request, make_ret(Ret::Code::Ok, message));
}

Response AudacityCommandsController::handleAddRealtimeEffect(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!realtimeEffectService()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("IRealtimeEffectService not available")));
    }

    muse::Val trackIdVal = request.query.param("track_id");
    if (trackIdVal.isNull()) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("Missing required 'track_id' argument (use -2 for the Master bus)")));
    }
    std::string effectId = request.query.param("effect_id").toString();
    if (effectId.empty()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Missing required 'effect_id' argument")));
    }

    au::trackedit::TrackId trackId = au::trackedit::INVALID_TRACK;
    try {
        trackId = static_cast<au::trackedit::TrackId>(trackIdVal.toInt64());
    } catch (const std::exception& e) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Invalid 'track_id' argument: ") + e.what()));
    }

    au::effects::RealtimeEffectStatePtr state
        =realtimeEffectService()->addRealtimeEffect(trackId, String::fromStdString(effectId));
    if (!state) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("Failed to add realtime effect '") + effectId + "' - check the "
                                                 "effect_id is exact (from list-effects) and the track_id is valid"));
    }

    std::optional<std::vector<au::effects::RealtimeEffectStatePtr> > stack = realtimeEffectService()->effectStack(trackId);
    int index = stack.has_value() ? static_cast<int>(stack->size()) - 1 : -1;

    JsonObject root;
    root["trackId"] = static_cast<int>(trackId);
    root["index"] = index;
    root["effectId"] = effectId;

    JsonDocument doc(root);
    std::string json(doc.toJson(JsonDocument::Format::Compact).constChar());

    Response response = make_response(request, make_ret(Ret::Code::Ok, std::string("Added realtime effect ") + effectId));
    response.data = json;
    return response;
}

Response AudacityCommandsController::handleListRealtimeEffects(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!realtimeEffectService()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("IRealtimeEffectService not available")));
    }

    muse::Val trackIdVal = request.query.param("track_id");
    if (trackIdVal.isNull()) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("Missing required 'track_id' argument (use -2 for the Master bus)")));
    }
    au::trackedit::TrackId trackId = au::trackedit::INVALID_TRACK;
    try {
        trackId = static_cast<au::trackedit::TrackId>(trackIdVal.toInt64());
    } catch (const std::exception& e) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Invalid 'track_id' argument: ") + e.what()));
    }

    std::optional<std::vector<au::effects::RealtimeEffectStatePtr> > stack = realtimeEffectService()->effectStack(trackId);

    JsonArray arr;
    if (stack.has_value()) {
        for (size_t i = 0; i < stack->size(); ++i) {
            const au::effects::RealtimeEffectStatePtr& state = (*stack)[i];
            std::optional<std::string> name = realtimeEffectService()->effectName(state);

            JsonObject obj;
            obj["index"] = static_cast<int>(i);
            obj["effectName"] = name.value_or("Unknown");
            obj["isActive"] = realtimeEffectService()->isActive(state);
            arr << obj;
        }
    }

    JsonObject root;
    root["trackId"] = static_cast<int>(trackId);
    root["effects"] = arr;

    JsonDocument doc(root);
    std::string json(doc.toJson(JsonDocument::Format::Compact).constChar());

    Response response = make_response(request, make_ret(Ret::Code::Ok, std::string("list-realtime-effects")));
    response.data = json;
    return response;
}

Response AudacityCommandsController::handleRemoveRealtimeEffect(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!realtimeEffectService()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("IRealtimeEffectService not available")));
    }

    muse::Val trackIdVal = request.query.param("track_id");
    muse::Val indexVal = request.query.param("index");
    if (trackIdVal.isNull() || indexVal.isNull()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Missing required 'track_id'/'index' arguments")));
    }
    au::trackedit::TrackId trackId = au::trackedit::INVALID_TRACK;
    int index = -1;
    try {
        trackId = static_cast<au::trackedit::TrackId>(trackIdVal.toInt64());
        index = static_cast<int>(toFiniteDouble(indexVal));
    } catch (const std::exception& e) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Invalid 'track_id'/'index' argument: ") + e.what()));
    }

    au::effects::RealtimeEffectStatePtr state = realtimeEffectAt(trackId, index);
    if (!state) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("No realtime effect at index ") + std::to_string(index)
                                                 + " on track " + std::to_string(trackId)));
    }

    realtimeEffectService()->removeRealtimeEffect(trackId, state);

    return make_response(request, make_ret(Ret::Code::Ok, std::string("Removed realtime effect at index ") + std::to_string(index)));
}

Response AudacityCommandsController::handleSetRealtimeEffectActive(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!realtimeEffectService()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("IRealtimeEffectService not available")));
    }

    muse::Val trackIdVal = request.query.param("track_id");
    muse::Val indexVal = request.query.param("index");
    muse::Val activeVal = request.query.param("active");
    if (trackIdVal.isNull() || indexVal.isNull() || activeVal.isNull()) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("Missing required 'track_id'/'index'/'active' arguments")));
    }
    au::trackedit::TrackId trackId = au::trackedit::INVALID_TRACK;
    int index = -1;
    try {
        trackId = static_cast<au::trackedit::TrackId>(trackIdVal.toInt64());
        index = static_cast<int>(toFiniteDouble(indexVal));
    } catch (const std::exception& e) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Invalid 'track_id'/'index' argument: ") + e.what()));
    }
    bool active = activeVal.toBool();

    au::effects::RealtimeEffectStatePtr state = realtimeEffectAt(trackId, index);
    if (!state) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("No realtime effect at index ") + std::to_string(index)
                                                 + " on track " + std::to_string(trackId)));
    }

    realtimeEffectService()->setIsActive(state, active);

    return make_response(request, make_ret(Ret::Code::Ok,
                                            std::string(active ? "Enabled" : "Bypassed") + " realtime effect at index "
                                            + std::to_string(index)));
}

au::effects::EffectInstanceId AudacityCommandsController::realtimeEffectInstanceId(
    const au::effects::RealtimeEffectStatePtr& state) const
{
    if (!state || !effectInstancesRegister()) {
        return au::effects::EffectInstanceId();
    }
    const auto instance = std::dynamic_pointer_cast<au::effects::EffectInstance>(state->GetInstance());
    if (!instance) {
        return au::effects::EffectInstanceId();
    }

    //! NOTE Confirmed live: instance->id() alone is NOT enough - it's just the
    //! raw AU3 instance identity, meaningless to EffectParametersProvider until
    //! actually registered via IEffectInstancesRegister::regInstance() (fails
    //! with "Effect instance not found" otherwise). Realtime effects don't get
    //! auto-registered by addRealtimeEffect - the UI only does this when a
    //! settings panel is opened (RealtimeEffectViewerDialogModel::reload(),
    //! effects/effects_base/view/realtimeeffectviewerdialogmodel.cpp), which
    //! never happens for an MCP-only session. Register on first use here
    //! instead - regInstance() is safe to call again for an already-registered
    //! id (effectinstancesregister.cpp's regInstance() is a plain map insert,
    //! a no-op if the key already exists).
    if (effectInstancesRegister()->instanceById(instance->id())) {
        return instance->id();
    }
    return effectInstancesRegister()->regInstance(String::fromStdString(state->GetID().ToStdString()), instance, state->GetAccess());
}

Response AudacityCommandsController::handleListEffectParameters(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!effectParametersProvider()) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("IEffectParametersProvider not available")));
    }

    muse::Val trackIdVal = request.query.param("track_id");
    muse::Val indexVal = request.query.param("index");
    if (trackIdVal.isNull() || indexVal.isNull()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Missing required 'track_id'/'index' arguments")));
    }
    au::trackedit::TrackId trackId = au::trackedit::INVALID_TRACK;
    int index = -1;
    try {
        trackId = static_cast<au::trackedit::TrackId>(trackIdVal.toInt64());
        index = static_cast<int>(toFiniteDouble(indexVal));
    } catch (const std::exception& e) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Invalid 'track_id'/'index' argument: ") + e.what()));
    }

    au::effects::RealtimeEffectStatePtr state = realtimeEffectAt(trackId, index);
    if (!state) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("No realtime effect at index ") + std::to_string(index)
                                                 + " on track " + std::to_string(trackId)));
    }

    au::effects::EffectInstanceId instanceId = realtimeEffectInstanceId(state);
    if (instanceId == au::effects::EffectInstanceId()) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("Could not resolve an instance for this effect - "
                                                              "it may not support parameter extraction")));
    }

    JsonArray arr;
    for (const au::effects::ParameterInfo& p : effectParametersProvider()->parameters(instanceId)) {
        JsonObject obj;
        obj["id"] = p.id.toStdString();
        obj["name"] = p.name.toStdString();
        obj["units"] = p.units.toStdString();
        obj["type"] = parameterTypeToString(p.type);
        obj["minValue"] = p.minValue;
        obj["maxValue"] = p.maxValue;
        obj["defaultValue"] = p.defaultValue;
        obj["currentValue"] = p.currentValue;
        obj["currentValueString"] = p.currentValueString.toStdString();
        if (!p.enumValues.empty()) {
            JsonArray enumArr;
            for (const muse::String& v : p.enumValues) {
                enumArr << v.toStdString();
            }
            obj["enumValues"] = enumArr;
        }
        arr << obj;
    }

    JsonObject root;
    root["trackId"] = static_cast<int>(trackId);
    root["index"] = index;
    root["parameters"] = arr;

    JsonDocument doc(root);
    std::string json(doc.toJson(JsonDocument::Format::Compact).constChar());

    Response response = make_response(request, make_ret(Ret::Code::Ok, std::string("list-effect-parameters")));
    response.data = json;
    return response;
}

Response AudacityCommandsController::handleSetEffectParameters(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!effectParametersProvider()) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("IEffectParametersProvider not available")));
    }

    muse::Val trackIdVal = request.query.param("track_id");
    muse::Val indexVal = request.query.param("index");
    const std::string pairs = request.query.param("parameters").toString();
    if (trackIdVal.isNull() || indexVal.isNull() || pairs.empty()) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("Missing required 'track_id'/'index'/'parameters' arguments. "
                                                              "'parameters' is \"id=value;id=value;...\"")));
    }

    au::trackedit::TrackId trackId = au::trackedit::INVALID_TRACK;
    int index = -1;
    try {
        trackId = static_cast<au::trackedit::TrackId>(trackIdVal.toInt64());
        index = static_cast<int>(toFiniteDouble(indexVal));
    } catch (const std::exception& e) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("Invalid 'track_id'/'index' argument: ") + e.what()));
    }

    //! Parsed up front so a malformed entry is rejected before anything is written -
    //! a partially applied batch is worse than none at all.
    std::vector<std::pair<std::string, double> > writes;
    std::stringstream ss(pairs);
    std::string item;
    while (std::getline(ss, item, ';')) {
        if (item.empty()) {
            continue;
        }
        const auto eq = item.find('=');
        if (eq == std::string::npos || eq == 0 || eq + 1 >= item.size()) {
            return make_response(request, make_ret(Ret::Code::UnknownError,
                                                     std::string("Malformed entry '") + item
                                                     + "' - expected \"id=value\" separated by ';'"));
        }
        const std::string id = item.substr(0, eq);
        double value = 0.0;
        try {
            value = toFiniteDouble(muse::Val(item.substr(eq + 1)));
        } catch (const std::exception& e) {
            return make_response(request, make_ret(Ret::Code::UnknownError,
                                                     std::string("Invalid value for parameter '") + id + "': " + e.what()));
        }
        writes.emplace_back(id, value);
    }
    if (writes.empty()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("'parameters' contained no entries")));
    }

    au::effects::RealtimeEffectStatePtr state = realtimeEffectAt(trackId, index);
    if (!state) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("No realtime effect at index ") + std::to_string(index)
                                                 + " on track " + std::to_string(trackId)));
    }
    au::effects::EffectInstanceId instanceId = realtimeEffectInstanceId(state);
    if (instanceId == au::effects::EffectInstanceId()) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("Could not resolve an instance for this effect - "
                                                              "it may not support parameter extraction")));
    }

    //! Every gesture is opened, then every value written, then the gestures closed.
    //! endParameterGesture() is what flushes the accumulated changes and stores the
    //! settings, and it clears the per-instance gesture state as it does so, so only
    //! the first close actually flushes - by which point all of the values are in.
    //! One commit for the whole batch means callers cannot observe (or hear) a
    //! half-configured state, such as an EQ band that has been enabled but whose
    //! frequency has not been set yet.
    std::vector<muse::String> paramIds;
    paramIds.reserve(writes.size());
    for (const auto& w : writes) {
        paramIds.push_back(String::fromStdString(w.first));
    }

    for (const auto& id : paramIds) {
        effectParametersProvider()->beginParameterGesture(instanceId, id);
    }

    std::vector<std::string> failed;
    for (size_t i = 0; i < writes.size(); ++i) {
        if (!effectParametersProvider()->setParameterValue(instanceId, paramIds[i], writes[i].second)) {
            failed.push_back(writes[i].first);
        }
    }

    for (const auto& id : paramIds) {
        effectParametersProvider()->endParameterGesture(instanceId, id);
    }

    const size_t applied = writes.size() - failed.size();
    std::string message = "Set " + std::to_string(applied) + " of " + std::to_string(writes.size()) + " parameters";
    if (!failed.empty()) {
        message += " (failed: ";
        for (size_t i = 0; i < failed.size(); ++i) {
            message += failed[i] + (i + 1 < failed.size() ? ", " : "");
        }
        message += ")";
    }

    return make_response(request, make_ret(failed.empty() ? Ret::Code::Ok : Ret::Code::UnknownError, message));
}

Response AudacityCommandsController::handleSetEffectParameter(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!effectParametersProvider()) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("IEffectParametersProvider not available")));
    }

    muse::Val trackIdVal = request.query.param("track_id");
    muse::Val indexVal = request.query.param("index");
    std::string parameterId = request.query.param("parameter_id").toString();
    muse::Val valueVal = request.query.param("value");
    if (trackIdVal.isNull() || indexVal.isNull() || parameterId.empty() || valueVal.isNull()) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("Missing required 'track_id'/'index'/'parameter_id'/'value' arguments")));
    }
    au::trackedit::TrackId trackId = au::trackedit::INVALID_TRACK;
    int index = -1;
    double value = 0.0;
    try {
        trackId = static_cast<au::trackedit::TrackId>(trackIdVal.toInt64());
        index = static_cast<int>(toFiniteDouble(indexVal));
        value = toFiniteDouble(valueVal);
    } catch (const std::exception& e) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("Invalid 'track_id'/'index'/'value' argument: ") + e.what()));
    }

    au::effects::RealtimeEffectStatePtr state = realtimeEffectAt(trackId, index);
    if (!state) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("No realtime effect at index ") + std::to_string(index)
                                                 + " on track " + std::to_string(trackId)));
    }

    au::effects::EffectInstanceId instanceId = realtimeEffectInstanceId(state);
    if (instanceId == au::effects::EffectInstanceId()) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("Could not resolve an instance for this effect - "
                                                              "it may not support parameter extraction")));
    }

    //! NOTE Confirmed live: a bare setParameterValue() call (no gesture) reports
    //! success but the write does not stick - re-reading the parameter
    //! afterward (via a fresh list-effect-parameters call, not just this
    //! function's own parameterValueString echo) showed the value unchanged.
    //! VST3 plugins commonly expect a begin/end "gesture" bracketing a
    //! parameter write to distinguish real automation from a spurious set -
    //! IEffectParametersProvider exposes exactly this pairing.
    const muse::String paramIdStr = String::fromStdString(parameterId);
    effectParametersProvider()->beginParameterGesture(instanceId, paramIdStr);
    bool ok = effectParametersProvider()->setParameterValue(instanceId, paramIdStr, value);
    effectParametersProvider()->endParameterGesture(instanceId, paramIdStr);
    if (!ok) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("Failed to set parameter '") + parameterId + "' - check the "
                                                 "parameter_id is exact (from list-effect-parameters) and value is in range"));
    }

    muse::String formatted = effectParametersProvider()->parameterValueString(instanceId, paramIdStr, value);

    return make_response(request, make_ret(Ret::Code::Ok,
                                            std::string("Set ") + parameterId + " = " + formatted.toStdString()));
}

Response AudacityCommandsController::handleListEffectPresets(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!effectPresetsProvider()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("IEffectPresetsProvider not available")));
    }

    muse::Val trackIdVal = request.query.param("track_id");
    muse::Val indexVal = request.query.param("index");
    if (trackIdVal.isNull() || indexVal.isNull()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Missing required 'track_id'/'index' arguments")));
    }
    au::trackedit::TrackId trackId = au::trackedit::INVALID_TRACK;
    int index = -1;
    try {
        trackId = static_cast<au::trackedit::TrackId>(trackIdVal.toInt64());
        index = static_cast<int>(toFiniteDouble(indexVal));
    } catch (const std::exception& e) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Invalid 'track_id'/'index' argument: ") + e.what()));
    }

    au::effects::RealtimeEffectStatePtr state = realtimeEffectAt(trackId, index);
    if (!state) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("No realtime effect at index ") + std::to_string(index)
                                                 + " on track " + std::to_string(trackId)));
    }

    const au::effects::EffectId effectId = String::fromStdString(state->GetID().ToStdString());

    JsonArray arr;
    for (const au::effects::PresetId& presetId : effectPresetsProvider()->factoryPresets(effectId)) {
        arr << presetId.ToStdString();
    }

    JsonObject root;
    root["trackId"] = static_cast<int>(trackId);
    root["index"] = index;
    root["factoryPresets"] = arr;

    JsonDocument doc(root);
    std::string json(doc.toJson(JsonDocument::Format::Compact).constChar());

    Response response = make_response(request, make_ret(Ret::Code::Ok, std::string("list-effect-presets")));
    response.data = json;
    return response;
}

Response AudacityCommandsController::handleApplyEffectPreset(const Request& request)
{
    if (!hasOpenProject()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("No project is currently open")));
    }
    if (!effectPresetsProvider()) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("IEffectPresetsProvider not available")));
    }

    muse::Val trackIdVal = request.query.param("track_id");
    muse::Val indexVal = request.query.param("index");
    std::string presetId = request.query.param("preset_id").toString();
    if (trackIdVal.isNull() || indexVal.isNull() || presetId.empty()) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("Missing required 'track_id'/'index'/'preset_id' arguments")));
    }
    au::trackedit::TrackId trackId = au::trackedit::INVALID_TRACK;
    int index = -1;
    try {
        trackId = static_cast<au::trackedit::TrackId>(trackIdVal.toInt64());
        index = static_cast<int>(toFiniteDouble(indexVal));
    } catch (const std::exception& e) {
        return make_response(request, make_ret(Ret::Code::UnknownError, std::string("Invalid 'track_id'/'index' argument: ") + e.what()));
    }

    au::effects::RealtimeEffectStatePtr state = realtimeEffectAt(trackId, index);
    if (!state) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("No realtime effect at index ") + std::to_string(index)
                                                 + " on track " + std::to_string(trackId)));
    }

    au::effects::EffectInstanceId instanceId = realtimeEffectInstanceId(state);
    if (instanceId == au::effects::EffectInstanceId()) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("Could not resolve an instance for this effect")));
    }

    muse::Ret ret = effectPresetsProvider()->applyPreset(instanceId, wxString::FromUTF8(presetId));
    if (!ret) {
        return make_response(request, make_ret(Ret::Code::UnknownError,
                                                 std::string("Failed to apply preset '") + presetId + "': " + ret.text()));
    }

    return make_response(request, make_ret(Ret::Code::Ok, std::string("Applied preset ") + presetId));
}
