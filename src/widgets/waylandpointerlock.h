#pragma once

#include <functional>
#include <wayland-client.h>
#include "pointer-constraints-client-protocol.h"
#include "relative-pointer-client-protocol.h"

class QWindow;

namespace QtSpice {

// Uses Qt's Wayland connection and pointer; all methods and callbacks run on the GUI thread.
class WaylandPointerLock final {
public:
    WaylandPointerLock(std::function<void(double, double)> motion,
                       std::function<void(bool)> lockChanged);
    ~WaylandPointerLock();
    WaylandPointerLock(const WaylandPointerLock &) = delete;
    WaylandPointerLock &operator=(const WaylandPointerLock &) = delete;

    bool request(QWindow *window);
    void release();
    bool locked() const { return locked_; }
    bool available() const { return relativeManager_ && constraints_; }

private:
    struct wl_registry *registry_ = nullptr;
    struct zwp_relative_pointer_manager_v1 *relativeManager_ = nullptr;
    struct zwp_pointer_constraints_v1 *constraints_ = nullptr;
    struct zwp_relative_pointer_v1 *relativePointer_ = nullptr;
    struct zwp_locked_pointer_v1 *lockedPointer_ = nullptr;
    bool locked_ = false;
    std::function<void(double, double)> motion_;
    std::function<void(bool)> lockChanged_;

    static void global(void *data, wl_registry *, uint32_t name, const char *interface, uint32_t version);
    static void globalRemove(void *data, wl_registry *, uint32_t name);
    static void relativeMotion(void *data, zwp_relative_pointer_v1 *, uint32_t hi, uint32_t lo,
                               wl_fixed_t dx, wl_fixed_t dy, wl_fixed_t dxUnaccel, wl_fixed_t dyUnaccel);
    static void locked(void *data, zwp_locked_pointer_v1 *);
    static void unlocked(void *data, zwp_locked_pointer_v1 *);
    uint32_t relativeName_ = 0;
    uint32_t constraintsName_ = 0;
};

}
