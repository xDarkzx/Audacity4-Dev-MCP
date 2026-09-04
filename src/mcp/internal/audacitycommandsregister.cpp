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
