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

using namespace muse;
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

TEST_F(AudacityCommandsRegisterTests, CommandListHasSixteenEntries)
{
    // new-project is deliberately not listed - see AudacityCommandsController::init()
    EXPECT_EQ(m_register.commandList().size(), size_t(16));
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
