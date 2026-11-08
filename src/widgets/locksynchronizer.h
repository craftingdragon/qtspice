// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include "keyboardlocks.h"
namespace QtSpice {
class LockSynchronizer {
public:
    void reset() { sent_ = {}; }
    template<class Send> void apply(LockSnapshot state, bool allowed, Send send) {
        state.known &= 7;
        if (!allowed || !state.known) { reset(); return; }
        state.locks &= state.known;
        if (state == sent_) return;
        sent_ = state;
        send(state);
    }
private:
    LockSnapshot sent_;
};
}
