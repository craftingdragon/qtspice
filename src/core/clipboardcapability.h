// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

namespace QtSpice {
inline bool clipboardByDemandReady(bool agentConnected, bool byDemandCapability) {
    return agentConnected && byDemandCapability;
}
}
