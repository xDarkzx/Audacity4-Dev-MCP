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
