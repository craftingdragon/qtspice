// SPDX-License-Identifier: LGPL-3.0-or-later
#include "waylandpointerlock.h"

#include <QGuiApplication>
#include <QWindow>
#include <QtGui/qguiapplication_platform.h>
#include <QtGui/qpa/qplatformnativeinterface.h>
#include <cstring>

namespace QtSpice {

WaylandPointerLock::WaylandPointerLock(std::function<void(double, double)> motion,
                                       std::function<void(bool)> lockChanged)
    : motion_(std::move(motion)), lockChanged_(std::move(lockChanged)) {
    auto *native = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (!native || !native->display()) return;
    registry_ = wl_display_get_registry(native->display());
    if (!registry_) return;
    static const wl_registry_listener listener = {&WaylandPointerLock::global, &WaylandPointerLock::globalRemove};
    wl_registry_add_listener(registry_, &listener, this);
}

WaylandPointerLock::~WaylandPointerLock() {
    release();
    if (constraints_) zwp_pointer_constraints_v1_destroy(constraints_);
    if (relativeManager_) zwp_relative_pointer_manager_v1_destroy(relativeManager_);
    if (registry_) wl_registry_destroy(registry_);
}

void WaylandPointerLock::global(void *data, wl_registry *registry, uint32_t name,
                                const char *interface, uint32_t) {
    auto *self = static_cast<WaylandPointerLock *>(data);
    if (!self->relativeManager_ && std::strcmp(interface, zwp_relative_pointer_manager_v1_interface.name) == 0) {
        self->relativeManager_ = static_cast<zwp_relative_pointer_manager_v1 *>(
            wl_registry_bind(registry, name, &zwp_relative_pointer_manager_v1_interface, 1));
        self->relativeName_ = name;
    } else if (!self->constraints_ && std::strcmp(interface, zwp_pointer_constraints_v1_interface.name) == 0) {
        self->constraints_ = static_cast<zwp_pointer_constraints_v1 *>(
            wl_registry_bind(registry, name, &zwp_pointer_constraints_v1_interface, 1));
        self->constraintsName_ = name;
    }
}

void WaylandPointerLock::globalRemove(void *data, wl_registry *, uint32_t name) {
    auto *self = static_cast<WaylandPointerLock *>(data);
    if (name == self->relativeName_ || name == self->constraintsName_) self->release();
    if (name == self->relativeName_) {
        zwp_relative_pointer_manager_v1_destroy(self->relativeManager_);
        self->relativeManager_ = nullptr;
        self->relativeName_ = 0;
    }
    if (name == self->constraintsName_) {
        zwp_pointer_constraints_v1_destroy(self->constraints_);
        self->constraints_ = nullptr;
        self->constraintsName_ = 0;
    }
}

bool WaylandPointerLock::request(QWindow *window) {
    if (lockedPointer_) return true;
    if (!window || !relativeManager_ || !constraints_) return false;
    auto *app = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    auto *platform = QGuiApplication::platformNativeInterface();
    if (!app || !platform || !app->pointer()) return false;
    auto *surface = static_cast<wl_surface *>(platform->nativeResourceForWindow("surface", window));
    if (!surface) return false;

    relativePointer_ = zwp_relative_pointer_manager_v1_get_relative_pointer(relativeManager_, app->pointer());
    if (!relativePointer_) return false;
    static const zwp_relative_pointer_v1_listener relativeListener = {&WaylandPointerLock::relativeMotion};
    zwp_relative_pointer_v1_add_listener(relativePointer_, &relativeListener, this);

    lockedPointer_ = zwp_pointer_constraints_v1_lock_pointer(
        constraints_, surface, app->pointer(), nullptr, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_ONESHOT);
    if (!lockedPointer_) {
        zwp_relative_pointer_v1_destroy(relativePointer_);
        relativePointer_ = nullptr;
        return false;
    }
    static const zwp_locked_pointer_v1_listener lockedListener = {
        &WaylandPointerLock::locked, &WaylandPointerLock::unlocked};
    zwp_locked_pointer_v1_add_listener(lockedPointer_, &lockedListener, this);
    return true;
}

void WaylandPointerLock::release() {
    if (lockedPointer_) {
        zwp_locked_pointer_v1_destroy(lockedPointer_);
        lockedPointer_ = nullptr;
    }
    if (relativePointer_) {
        zwp_relative_pointer_v1_destroy(relativePointer_);
        relativePointer_ = nullptr;
    }
    if (locked_) {
        locked_ = false;
        lockChanged_(false);
    }
}

void WaylandPointerLock::relativeMotion(void *data, zwp_relative_pointer_v1 *, uint32_t, uint32_t,
                                         wl_fixed_t dx, wl_fixed_t dy, wl_fixed_t, wl_fixed_t) {
    auto *self = static_cast<WaylandPointerLock *>(data);
    if (self->locked_) self->motion_(wl_fixed_to_double(dx), wl_fixed_to_double(dy));
}

void WaylandPointerLock::locked(void *data, zwp_locked_pointer_v1 *) {
    auto *self = static_cast<WaylandPointerLock *>(data);
    self->locked_ = true;
    self->lockChanged_(true);
}

void WaylandPointerLock::unlocked(void *data, zwp_locked_pointer_v1 *) {
    auto *self = static_cast<WaylandPointerLock *>(data);
    self->release();
}

}
