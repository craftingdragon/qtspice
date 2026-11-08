// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include "keyboardlocks.h"
namespace QtSpice {
// Wayland lock state belongs to a surface, and becomes authoritative only
// after a modifiers event following enter or a keymap replacement.
class FocusedLocks {
public:
    void enter(void *surface) { surface_ = surface; invalidate(); }
    void leave() { surface_ = nullptr; invalidate(); }
    void invalidate() { state_ = {}; }
    void update(LockSnapshot state) { state_ = surface_ ? state : LockSnapshot{}; }
    LockSnapshot forSurface(void *surface) const {
        return surface && surface == surface_ ? state_ : LockSnapshot{};
    }
private:
    void *surface_ = nullptr;
    LockSnapshot state_;
};
}
