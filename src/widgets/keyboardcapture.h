// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <functional>
#include <wayland-client.h>
#include "keyboard-shortcuts-client-protocol.h"
class QWindow;
namespace QtSpice {
class KeyboardCapture final {
public:
    explicit KeyboardCapture(std::function<void(bool)> changed);
    ~KeyboardCapture();
    bool available() const { return manager_; }
    bool request(QWindow *window);
    void release();
    std::function<void()> availabilityChanged;
private:
    wl_registry *registry_ = nullptr;
    zwp_keyboard_shortcuts_inhibit_manager_v1 *manager_ = nullptr;
    zwp_keyboard_shortcuts_inhibitor_v1 *inhibitor_ = nullptr;
    uint32_t name_ = 0;
    std::function<void(bool)> changed_;
    static void global(void *, wl_registry *, uint32_t, const char *, uint32_t);
    static void removed(void *, wl_registry *, uint32_t);
};
}
