#pragma once

namespace QtSpice {
inline bool clipboardByDemandReady(bool agentConnected, bool byDemandCapability) {
    return agentConnected && byDemandCapability;
}
}
