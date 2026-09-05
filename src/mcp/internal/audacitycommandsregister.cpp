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
        Command("command://mcp/play-selection"),
        TranslatableString("mcp", "Play selection"),
        TranslatableString("mcp", "Play the current time selection, starting from its beginning - "
                                    "unlike play-stop, which resumes from wherever playback last was"),
        InputSchema(),
        Decoration()
    },
    //! NOTE mcp/new-project deliberately not listed - see the comment in
    //! AudacityCommandsController::init() for why.
    CommandInfo{
        Command("command://mcp/save-project"),
        TranslatableString("mcp", "Save project"),
        TranslatableString("mcp", "Save the current project"),
        InputSchema(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/recent-commands"),
        TranslatableString("mcp", "Recent commands"),
        TranslatableString("mcp", "List recently executed MCP commands with their id, timestamp, and result"),
        InputSchema(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/command-status"),
        TranslatableString("mcp", "Command status"),
        TranslatableString("mcp", "Look up the recorded result of a previously executed command by its id"),
        []() {
            InputSchema schema;
            schema.args["id"] = Arg(DataType::String, u"The command id returned when it was originally executed");
            return schema;
        }(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/list-labels"),
        TranslatableString("mcp", "List labels"),
        TranslatableString("mcp", "List all labels on the project's (first) label track, with their key, text, and time range"),
        InputSchema(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/add-label-track"),
        TranslatableString("mcp", "Add label track"),
        TranslatableString("mcp", "Create a new, empty label track - needed before add-label can be used for the first time"),
        InputSchema(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/add-label"),
        TranslatableString("mcp", "Add label"),
        TranslatableString("mcp", "Add a label at the current selection/playback position on the first label track"),
        []() {
            InputSchema schema;
            schema.args["text"] = Arg(DataType::String, u"Optional text for the new label");
            return schema;
        }(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/remove-label"),
        TranslatableString("mcp", "Remove label"),
        TranslatableString("mcp", "Remove a label by its key"),
        []() {
            InputSchema schema;
            schema.args["key"] = Arg(DataType::String, u"The label's key, in \"trackId:itemId\" format, as returned by list-labels or add-label");
            return schema;
        }(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/update-label-text"),
        TranslatableString("mcp", "Update label text"),
        TranslatableString("mcp", "Change the text of an existing label"),
        []() {
            InputSchema schema;
            schema.args["key"] = Arg(DataType::String, u"The label's key, in \"trackId:itemId\" format, as returned by list-labels or add-label");
            schema.args["text"] = Arg(DataType::String, u"The new text for the label");
            return schema;
        }(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/update-label-time"),
        TranslatableString("mcp", "Update label time"),
        TranslatableString("mcp", "Move an existing label's start and/or end time"),
        []() {
            InputSchema schema;
            schema.args["key"] = Arg(DataType::String, u"The label's key, in \"trackId:itemId\" format, as returned by list-labels or add-label");
            schema.args["start"] = Arg(DataType::Float, u"New start time in seconds");
            schema.args["end"] = Arg(DataType::Float, u"New end time in seconds");
            return schema;
        }(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/apply-effect"),
        TranslatableString("mcp", "Apply effect"),
        TranslatableString("mcp", "Apply a built-in effect to the current selection by name, with optional parameters"),
        []() {
            InputSchema schema;
            schema.args["effect_id"] = Arg(DataType::String,
                                            u"The effect's title (e.g. \"Compressor\", \"Normalize\", \"Noise reduction\", "
                                            u"\"Limiter\", \"Loudness Normalization\")");
            schema.args["params"] = Arg(DataType::String,
                                         u"Optional automation parameters as \"Key=Value Key2=\\\"quoted value\\\"\" pairs");
            return schema;
        }(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/select-all"),
        TranslatableString("mcp", "Select all"),
        TranslatableString("mcp", "Select all tracks and all audio data in the current project"),
        InputSchema(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/select-time"),
        TranslatableString("mcp", "Select time range"),
        TranslatableString("mcp", "Set the time selection without changing track selection"),
        []() {
            InputSchema schema;
            schema.args["start"] = Arg(DataType::Float, u"Start time in seconds");
            schema.args["end"] = Arg(DataType::Float, u"End time in seconds");
            return schema;
        }(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/export-wav"),
        TranslatableString("mcp", "Export WAV"),
        TranslatableString("mcp", "Export the current selection to a mono WAV file at the given path"),
        []() {
            InputSchema schema;
            schema.args["path"] = Arg(DataType::String, u"Filesystem path to write the WAV file to");
            schema.args["overwrite"] = Arg(DataType::Boolean,
                                            u"Set true to replace an existing file at path - refused by default to "
                                            u"avoid clobbering a file the caller didn't create");
            return schema;
        }(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/select-none"),
        TranslatableString("mcp", "Select none"),
        TranslatableString("mcp", "Clear all track and time selection"),
        InputSchema(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/select-tracks"),
        TranslatableString("mcp", "Select tracks"),
        TranslatableString("mcp", "Select one or more tracks by 0-based index"),
        []() {
            InputSchema schema;
            schema.args["track"] = Arg(DataType::Float, u"Starting track index (0-based)");
            schema.args["count"] = Arg(DataType::Float, u"Number of tracks to select (default 1)");
            return schema;
        }(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/track-add-mono"),
        TranslatableString("mcp", "Add mono track"),
        TranslatableString("mcp", "Add a new mono audio track to the project"),
        InputSchema(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/track-add-stereo"),
        TranslatableString("mcp", "Add stereo track"),
        TranslatableString("mcp", "Add a new stereo audio track to the project"),
        InputSchema(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/track-remove"),
        TranslatableString("mcp", "Remove track"),
        TranslatableString("mcp", "Remove the currently selected track(s) - call select-tracks first"),
        InputSchema(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/track-set-properties"),
        TranslatableString("mcp", "Set track properties"),
        TranslatableString("mcp", "Set a track's name, gain, pan, mute, and/or solo by 0-based index"),
        []() {
            InputSchema schema;
            schema.args["track"] = Arg(DataType::Float, u"Track index (0-based)");
            schema.args["name"] = Arg(DataType::String, u"New track name");
            schema.args["gain"] = Arg(DataType::Float, u"Track gain in dB");
            schema.args["pan"] = Arg(DataType::Float, u"Track pan (-1.0=left to 1.0=right)");
            schema.args["mute"] = Arg(DataType::Boolean, u"Mute the track");
            schema.args["solo"] = Arg(DataType::Boolean, u"Solo the track");
            return schema;
        }(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/track-duplicate"),
        TranslatableString("mcp", "Duplicate track"),
        TranslatableString("mcp", "Duplicate the currently selected track(s) - call select-tracks first"),
        InputSchema(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/track-resample"),
        TranslatableString("mcp", "Resample track"),
        TranslatableString("mcp", "Resample the currently selected track(s) to a new sample rate - call select-tracks first"),
        []() {
            InputSchema schema;
            schema.args["rate"] = Arg(DataType::Float, u"Target sample rate in Hz (e.g. 44100, 48000)");
            return schema;
        }(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/track-mute-all"),
        TranslatableString("mcp", "Mute all tracks"),
        TranslatableString("mcp", "Mute every track in the project, including any not currently selected"),
        InputSchema(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/track-unmute-all"),
        TranslatableString("mcp", "Unmute all tracks"),
        TranslatableString("mcp", "Unmute every track in the project, including any not currently selected"),
        InputSchema(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/set-clip-pitch"),
        TranslatableString("mcp", "Set clip pitch"),
        TranslatableString("mcp", "Non-destructively change one clip's pitch in semitones, without processing any audio - reversible via reset-clip-pitch"),
        []() {
            InputSchema schema;
            schema.args["key"] = Arg(DataType::String, u"The clip's key, in \"trackId:itemId\" format, as returned by track-get-info");
            schema.args["semitones"] = Arg(DataType::Float, u"Pitch shift in semitones (can be negative)");
            return schema;
        }(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/reset-clip-pitch"),
        TranslatableString("mcp", "Reset clip pitch"),
        TranslatableString("mcp", "Revert a clip's pitch change made via set-clip-pitch"),
        []() {
            InputSchema schema;
            schema.args["key"] = Arg(DataType::String, u"The clip's key, in \"trackId:itemId\" format");
            return schema;
        }(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/set-clip-speed"),
        TranslatableString("mcp", "Set clip speed"),
        TranslatableString("mcp", "Non-destructively change one clip's playback speed, without processing any audio - reversible via reset-clip-speed"),
        []() {
            InputSchema schema;
            schema.args["key"] = Arg(DataType::String, u"The clip's key, in \"trackId:itemId\" format");
            schema.args["speed"] = Arg(DataType::Float, u"Duration multiplier, NOT a playback-rate multiplier - 1.0 is normal, 2.0 makes the clip play twice as long (slower), 0.5 makes it half as long (faster). Confirmed live: new_duration = original_duration * speed.");
            return schema;
        }(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/reset-clip-speed"),
        TranslatableString("mcp", "Reset clip speed"),
        TranslatableString("mcp", "Revert a clip's speed change made via set-clip-speed"),
        []() {
            InputSchema schema;
            schema.args["key"] = Arg(DataType::String, u"The clip's key, in \"trackId:itemId\" format");
            return schema;
        }(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/render-clip-pitch-speed"),
        TranslatableString("mcp", "Render clip pitch/speed"),
        TranslatableString("mcp", "Permanently bake a clip's pitch/speed changes into its audio - use once you're happy with a non-destructive preview from set-clip-pitch/set-clip-speed"),
        []() {
            InputSchema schema;
            schema.args["key"] = Arg(DataType::String, u"The clip's key, in \"trackId:itemId\" format");
            return schema;
        }(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/reset-clip-pitch-speed"),
        TranslatableString("mcp", "Reset clip pitch/speed"),
        TranslatableString("mcp", "Revert both pitch and speed changes on a clip in one call"),
        []() {
            InputSchema schema;
            schema.args["key"] = Arg(DataType::String, u"The clip's key, in \"trackId:itemId\" format");
            return schema;
        }(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/split-clip-at-silences"),
        TranslatableString("mcp", "Split clip at silences"),
        TranslatableString("mcp", "Automatically split one clip into multiple clips at its detected silence boundaries"),
        []() {
            InputSchema schema;
            schema.args["key"] = Arg(DataType::String, u"The clip's key, in \"trackId:itemId\" format");
            return schema;
        }(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/split-range-at-silences"),
        TranslatableString("mcp", "Split selection at silences"),
        TranslatableString("mcp", "Automatically split every clip on the selected track(s) within a time range at detected silence boundaries - call select-tracks first"),
        []() {
            InputSchema schema;
            schema.args["start"] = Arg(DataType::Float, u"Start time in seconds");
            schema.args["end"] = Arg(DataType::Float, u"End time in seconds");
            return schema;
        }(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/trim-clip"),
        TranslatableString("mcp", "Trim clip"),
        TranslatableString("mcp", "Trim a clip's left or right edge inward by a delta in seconds, discarding that audio. Positive delta_sec shrinks the clip from that edge (confirmed live), for both left and right."),
        []() {
            InputSchema schema;
            schema.args["key"] = Arg(DataType::String, u"The clip's key, in \"trackId:itemId\" format");
            schema.args["side"] = Arg(DataType::String, u"\"left\" or \"right\"");
            schema.args["delta_sec"] = Arg(DataType::Float, u"Positive shrinks the clip inward from this edge; negative grows it outward. Confirmed live for both sides.");
            schema.args["min_clip_duration"] = Arg(DataType::Float, u"Minimum duration the clip must retain, in seconds. Default: 0");
            return schema;
        }(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/stretch-clip"),
        TranslatableString("mcp", "Stretch clip"),
        TranslatableString("mcp", "Grow or shrink a clip's left or right edge, revealing previously-trimmed audio when growing (or time-stretching if none remains). Same delta_sec sign convention as trim-clip - positive shrinks, negative grows - despite the name suggesting the opposite; confirmed live, not assumed."),
        []() {
            InputSchema schema;
            schema.args["key"] = Arg(DataType::String, u"The clip's key, in \"trackId:itemId\" format");
            schema.args["side"] = Arg(DataType::String, u"\"left\" or \"right\"");
            schema.args["delta_sec"] = Arg(DataType::Float, u"Positive shrinks the clip inward from this edge; negative grows/reveals outward. Confirmed live for both sides - same sign convention as trim-clip.");
            schema.args["min_clip_duration"] = Arg(DataType::Float, u"Minimum duration the clip must retain, in seconds. Default: 0");
            return schema;
        }(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/nearest-zero-crossing"),
        TranslatableString("mcp", "Nearest zero crossing"),
        TranslatableString("mcp", "Find the nearest zero-crossing time to a given timestamp, for precise, click-free edit points"),
        []() {
            InputSchema schema;
            schema.args["time"] = Arg(DataType::Float, u"Time in seconds to search near");
            return schema;
        }(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/set-clip-color"),
        TranslatableString("mcp", "Set clip color"),
        TranslatableString("mcp", "Set a clip's color tag for visual organization"),
        []() {
            InputSchema schema;
            schema.args["key"] = Arg(DataType::String, u"The clip's key, in \"trackId:itemId\" format");
            schema.args["color_index"] = Arg(DataType::Float, u"0 (no custom color, inherit from track) to 9");
            return schema;
        }(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/set-track-color"),
        TranslatableString("mcp", "Set track color"),
        TranslatableString("mcp", "Set the color tag for the currently selected track(s) - call select-tracks first"),
        []() {
            InputSchema schema;
            schema.args["color_index"] = Arg(DataType::Float, u"0 (no custom color, inherit default) to 9");
            return schema;
        }(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/edit-cut"),
        TranslatableString("mcp", "Cut"),
        TranslatableString("mcp", "Cut the current selection to the clipboard"),
        InputSchema(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/edit-copy"),
        TranslatableString("mcp", "Copy"),
        TranslatableString("mcp", "Copy the current selection to the clipboard"),
        InputSchema(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/edit-paste"),
        TranslatableString("mcp", "Paste"),
        TranslatableString("mcp", "Paste the clipboard contents at the current selection start"),
        InputSchema(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/edit-delete"),
        TranslatableString("mcp", "Delete"),
        TranslatableString("mcp", "Delete the current selection, closing the gap (does not copy to clipboard)"),
        InputSchema(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/edit-split"),
        TranslatableString("mcp", "Split"),
        TranslatableString("mcp", "Split the selected track(s) at the current selection boundaries"),
        InputSchema(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/edit-trim"),
        TranslatableString("mcp", "Trim"),
        TranslatableString("mcp", "Delete everything outside the current selection"),
        InputSchema(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/edit-silence"),
        TranslatableString("mcp", "Silence"),
        TranslatableString("mcp", "Replace the current selection with silence"),
        InputSchema(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/edit-duplicate"),
        TranslatableString("mcp", "Duplicate selection"),
        TranslatableString("mcp", "Duplicate the current selection into a new track"),
        InputSchema(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/edit-undo"),
        TranslatableString("mcp", "Undo"),
        TranslatableString("mcp", "Undo the last action"),
        InputSchema(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/edit-redo"),
        TranslatableString("mcp", "Redo"),
        TranslatableString("mcp", "Redo the last undone action"),
        InputSchema(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/edit-split-new"),
        TranslatableString("mcp", "Split into new track"),
        TranslatableString("mcp", "Split the selected audio into a new track at the selection boundaries"),
        InputSchema(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/edit-split-cut"),
        TranslatableString("mcp", "Split cut"),
        TranslatableString("mcp", "Cut the selected audio to clipboard without closing the gap"),
        InputSchema(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/edit-split-delete"),
        TranslatableString("mcp", "Split delete"),
        TranslatableString("mcp", "Delete the selected audio without closing the gap"),
        InputSchema(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/edit-disjoin"),
        TranslatableString("mcp", "Disjoin"),
        TranslatableString("mcp", "Split the selected audio at detected silences into separate clips"),
        InputSchema(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/edit-join"),
        TranslatableString("mcp", "Join"),
        TranslatableString("mcp", "Join the selected clips into one clip"),
        InputSchema(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/select-zero-crossing"),
        TranslatableString("mcp", "Select to zero crossing"),
        TranslatableString("mcp", "Adjust the current selection boundaries to the nearest zero crossings"),
        InputSchema(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/project-open"),
        TranslatableString("mcp", "Open project"),
        TranslatableString("mcp", "Open an existing Audacity project file"),
        []() {
            InputSchema schema;
            schema.args["path"] = Arg(DataType::String, u"Absolute path to the .aup4 project file");
            return schema;
        }(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/project-import"),
        TranslatableString("mcp", "Import audio"),
        TranslatableString("mcp", "Import an audio file into the current project as a new track"),
        []() {
            InputSchema schema;
            schema.args["path"] = Arg(DataType::String, u"Absolute path to the audio file (wav, mp3, ogg, flac, etc.)");
            return schema;
        }(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/project-close"),
        TranslatableString("mcp", "Close project"),
        TranslatableString("mcp", "Close the current project"),
        InputSchema(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/project-save-as"),
        TranslatableString("mcp", "Save project as"),
        TranslatableString("mcp", "Save the current project to a new file path"),
        []() {
            InputSchema schema;
            schema.args["path"] = Arg(DataType::String, u"Absolute path for the new project file");
            return schema;
        }(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/project-export"),
        TranslatableString("mcp", "Export project audio"),
        TranslatableString("mcp", "Export the full project's audio to a stereo WAV file at the given path"),
        []() {
            InputSchema schema;
            schema.args["path"] = Arg(DataType::String, u"Filesystem path to write the WAV file to");
            schema.args["overwrite"] = Arg(DataType::Boolean, u"Set true to replace an existing file - refused by default");
            return schema;
        }(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/transport-record"),
        TranslatableString("mcp", "Record"),
        TranslatableString("mcp", "Start recording on a new track"),
        InputSchema(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/cursor-set"),
        TranslatableString("mcp", "Set cursor position"),
        TranslatableString("mcp", "Move the playback cursor / edit point to a specific time"),
        []() {
            InputSchema schema;
            schema.args["time"] = Arg(DataType::Float, u"Position in seconds");
            return schema;
        }(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/project-new"),
        TranslatableString("mcp", "New project"),
        TranslatableString("mcp", "Create a new project in the current window - only allowed when no project is currently open"),
        InputSchema(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/project-get-info"),
        TranslatableString("mcp", "Get project info"),
        TranslatableString("mcp", "Get project-wide info: path, unsaved-changes state, duration, and a "
                                    "summary of every track (id, title, type, rate, mute/solo, clip count)"),
        InputSchema(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/track-get-info"),
        TranslatableString("mcp", "Get track info"),
        TranslatableString("mcp", "Get detailed info about one track by id, including its full clip list "
                                    "(or label list, for a label track)"),
        []() {
            InputSchema schema;
            schema.args["track_id"] = Arg(DataType::Integer, u"Track id, from project-get-info's track list");
            return schema;
        }(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/select-clip"),
        TranslatableString("mcp", "Select clip"),
        TranslatableString("mcp", "Select a specific clip by key (selects its track, the clip itself, "
                                    "and the time range spanning it)"),
        []() {
            InputSchema schema;
            schema.args["key"] = Arg(DataType::String, u"Clip key \"trackId:itemId\", from track-get-info's clip list");
            return schema;
        }(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/transport-get-play-position"),
        TranslatableString("mcp", "Get play position"),
        TranslatableString("mcp", "Get the current playhead position, whether playback is active, "
                                    "and the current time selection (start/end)"),
        InputSchema(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/list-effects"),
        TranslatableString("mcp", "List effects"),
        TranslatableString("mcp", "List available effects/plugins (builtin, VST3, Nyquist, etc.). Each "
                                    "result's \"title\" is the effect_id to pass to apply-effect (uses "
                                    "title-fallback resolution); its \"id\" is the real internal PluginID, "
                                    "REQUIRED for add-realtime-effect instead (that path has no title "
                                    "fallback - confirmed live, passing a title there fails with \"cannot "
                                    "load the effect\"). Filter by category (substring, e.g. \"reverb\"), "
                                    "family (exact, e.g. \"VST3\"), or search (title substring). Capped at "
                                    "100 results by default - use limit to change, totalMatched shows the real count."),
        []() {
            InputSchema schema;
            schema.args["category"] = Arg(DataType::String, u"Category substring filter, e.g. \"reverb\", \"eq\", \"compression\"");
            schema.args["family"] = Arg(DataType::String, u"Exact family filter: Builtin, VST3, Nyquist, LV2, AudioUnit, Extension");
            schema.args["search"] = Arg(DataType::String, u"Title substring filter");
            schema.args["limit"] = Arg(DataType::Integer, u"Max results to return, default 100");
            return schema;
        }(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/add-realtime-effect"),
        TranslatableString("mcp", "Add realtime effect"),
        TranslatableString("mcp", "Add a non-destructive realtime effect to a track's (or the Master "
                                    "bus's) effect chain - stays adjustable/removable afterward, unlike "
                                    "apply-effect which permanently renders the effect into the audio"),
        []() {
            InputSchema schema;
            schema.args["track_id"] = Arg(DataType::Integer, u"Track id from project-get-info, or -2 for the Master bus");
            schema.args["effect_id"] = Arg(DataType::String, u"The real PluginID, from list-effects' \"id\" field - NOT \"title\", "
                                                              u"which only works for apply-effect");
            return schema;
        }(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/list-realtime-effects"),
        TranslatableString("mcp", "List realtime effects"),
        TranslatableString("mcp", "List the realtime effect chain on a track or the Master bus, with "
                                    "each effect's index (for remove/set-active), name, and active state"),
        []() {
            InputSchema schema;
            schema.args["track_id"] = Arg(DataType::Integer, u"Track id from project-get-info, or -2 for the Master bus");
            return schema;
        }(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/remove-realtime-effect"),
        TranslatableString("mcp", "Remove realtime effect"),
        TranslatableString("mcp", "Remove one effect from a track's (or the Master bus's) realtime effect chain by index"),
        []() {
            InputSchema schema;
            schema.args["track_id"] = Arg(DataType::Integer, u"Track id from project-get-info, or -2 for the Master bus");
            schema.args["index"] = Arg(DataType::Integer, u"Position in the chain, from list-realtime-effects");
            return schema;
        }(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/set-realtime-effect-active"),
        TranslatableString("mcp", "Bypass/enable realtime effect"),
        TranslatableString("mcp", "Enable or bypass one effect in a track's (or the Master bus's) realtime effect chain, without removing it"),
        []() {
            InputSchema schema;
            schema.args["track_id"] = Arg(DataType::Integer, u"Track id from project-get-info, or -2 for the Master bus");
            schema.args["index"] = Arg(DataType::Integer, u"Position in the chain, from list-realtime-effects");
            schema.args["active"] = Arg(DataType::Boolean, u"true to enable, false to bypass");
            return schema;
        }(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/list-effect-parameters"),
        TranslatableString("mcp", "List effect parameters"),
        TranslatableString("mcp", "List a realtime effect's real, plugin-reported parameters - name, "
                                    "units, min/max/default/current value, and a human-formatted current "
                                    "value string. Works uniformly across Builtin/VST3/LV2/AudioUnit "
                                    "plugins via Audacity's own parameter-extraction layer - no need to "
                                    "guess a plugin's parameter names or ranges."),
        []() {
            InputSchema schema;
            schema.args["track_id"] = Arg(DataType::Integer, u"Track id from project-get-info, or -2 for the Master bus");
            schema.args["index"] = Arg(DataType::Integer, u"Position in the chain, from list-realtime-effects");
            return schema;
        }(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/set-effect-parameter"),
        TranslatableString("mcp", "Set effect parameter"),
        TranslatableString("mcp", "Set one real-time parameter on a realtime effect to an exact value - "
                                    "e.g. the actual wet/dry mix or decay time of a reverb, not just "
                                    "add/remove/bypass. Value must be within [minValue, maxValue] from "
                                    "list-effect-parameters. CONFIRMED LIVE: for VST3 plugins this range is "
                                    "typically normalized 0-1, NOT real display units, even though "
                                    "list-effect-parameters' currentValueString shows a real unit (e.g. "
                                    "\"21.26 s\") - after setting, re-read currentValueString to see the "
                                    "actual resulting value, don't assume the input scale matches it."),
        []() {
            InputSchema schema;
            schema.args["track_id"] = Arg(DataType::Integer, u"Track id from project-get-info, or -2 for the Master bus");
            schema.args["index"] = Arg(DataType::Integer, u"Position in the chain, from list-realtime-effects");
            schema.args["parameter_id"] = Arg(DataType::String, u"Parameter id, from list-effect-parameters' \"id\" field");
            schema.args["value"] = Arg(DataType::Float, u"New value within [minValue, maxValue] (VST3: typically "
                                                        u"normalized 0-1, not display units - see description)");
            return schema;
        }(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/list-effect-presets"),
        TranslatableString("mcp", "List effect presets"),
        TranslatableString("mcp", "List a realtime effect's real factory presets (e.g. a reverb's named "
                                    "room/hall presets), if the plugin format exposes any"),
        []() {
            InputSchema schema;
            schema.args["track_id"] = Arg(DataType::Integer, u"Track id from project-get-info, or -2 for the Master bus");
            schema.args["index"] = Arg(DataType::Integer, u"Position in the chain, from list-realtime-effects");
            return schema;
        }(),
        Decoration()
    },
    CommandInfo{
        Command("command://mcp/apply-effect-preset"),
        TranslatableString("mcp", "Apply effect preset"),
        TranslatableString("mcp", "Apply one of a realtime effect's factory presets by id"),
        []() {
            InputSchema schema;
            schema.args["track_id"] = Arg(DataType::Integer, u"Track id from project-get-info, or -2 for the Master bus");
            schema.args["index"] = Arg(DataType::Integer, u"Position in the chain, from list-realtime-effects");
            schema.args["preset_id"] = Arg(DataType::String, u"Preset id, from list-effect-presets");
            return schema;
        }(),
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
