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
#ifndef AU_MCP_TESTS_EFFECTEXECUTIONSCENARIOMOCK_H
#define AU_MCP_TESTS_EFFECTEXECUTIONSCENARIOMOCK_H

#include <gmock/gmock.h>

#include "effects/effects_base/ieffectexecutionscenario.h"

namespace au::mcp {
class EffectExecutionScenarioMock : public au::effects::IEffectExecutionScenario
{
public:
    MOCK_METHOD(muse::Ret, performEffect, (const au::effects::EffectId&), (override));
    MOCK_METHOD(muse::Ret, performEffect, (const au::effects::EffectId&, const std::string&), (override));
    MOCK_METHOD(bool, lastProcessorIsAvailable, (), (const, override));
    MOCK_METHOD(muse::async::Notification, lastProcessorIsNowAvailable, (), (const, override));
    MOCK_METHOD(muse::async::Channel<au::effects::EffectId>, lastProcessorIdChanged, (), (const, override));
    MOCK_METHOD(muse::Ret, repeatLastProcessor, (), (override));
    MOCK_METHOD(muse::Ret, previewEffect, (const au::effects::EffectInstanceId&, au::effects::EffectSettings&), (override));
    MOCK_METHOD(void, stopPreview, (), (override));
};
}

#endif // AU_MCP_TESTS_EFFECTEXECUTIONSCENARIOMOCK_H
