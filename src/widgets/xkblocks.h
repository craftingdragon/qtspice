// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include "keyboardlocks.h"
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-names.h>
namespace QtSpice {
inline LockSnapshot xkbLocks(xkb_state *state) {
    LockSnapshot result;
    if (!state) return result;
    for (auto [name, bit] : {std::pair{XKB_LED_NAME_SCROLL, 1u},
                             std::pair{XKB_LED_NAME_NUM, 2u},
                             std::pair{XKB_LED_NAME_CAPS, 4u}}) {
        const int active = xkb_state_led_name_is_active(state, name);
        if (active < 0) continue;
        result.known |= bit;
        if (active) result.locks |= bit;
    }
    return result;
}
}
