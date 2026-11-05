#include "keyboardcapture.h"
#include <QGuiApplication>
#include <QWindow>
#include <QtGui/qguiapplication_platform.h>
#include <QtGui/qpa/qplatformnativeinterface.h>
#include <cstring>
namespace QtSpice {
KeyboardCapture::KeyboardCapture(std::function<void(bool)> changed) : changed_(std::move(changed)) {
    auto *app = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (!app || !app->display()) return;
    registry_ = wl_display_get_registry(app->display());
    static const wl_registry_listener listener{global, removed};
    wl_registry_add_listener(registry_, &listener, this);
}
KeyboardCapture::~KeyboardCapture() {
    release();
    if (manager_) zwp_keyboard_shortcuts_inhibit_manager_v1_destroy(manager_);
    if (registry_) wl_registry_destroy(registry_);
}
void KeyboardCapture::global(void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t) {
    auto *self = static_cast<KeyboardCapture *>(data);
    if (self->manager_ || std::strcmp(interface, zwp_keyboard_shortcuts_inhibit_manager_v1_interface.name)) return;
    self->manager_ = static_cast<zwp_keyboard_shortcuts_inhibit_manager_v1 *>(wl_registry_bind(registry, name, &zwp_keyboard_shortcuts_inhibit_manager_v1_interface, 1));
    self->name_ = name;
    if (self->availabilityChanged) self->availabilityChanged();
}
void KeyboardCapture::removed(void *data, wl_registry *, uint32_t name) {
    auto *self = static_cast<KeyboardCapture *>(data);
    if (name != self->name_) return;
    self->release();
    zwp_keyboard_shortcuts_inhibit_manager_v1_destroy(self->manager_);
    self->manager_ = nullptr;
    self->name_ = 0;
    if (self->availabilityChanged) self->availabilityChanged();
}
bool KeyboardCapture::request(QWindow *window) {
    if (inhibitor_) return true;
    auto *app = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    auto *platform = QGuiApplication::platformNativeInterface();
    if (!window || !manager_ || !app || !app->seat() || !platform) return false;
    auto *surface = static_cast<wl_surface *>(platform->nativeResourceForWindow("surface", window));
    if (!surface) return false;
    inhibitor_ = zwp_keyboard_shortcuts_inhibit_manager_v1_inhibit_shortcuts(manager_, surface, app->seat());
    static const zwp_keyboard_shortcuts_inhibitor_v1_listener listener{
        [](void *d, zwp_keyboard_shortcuts_inhibitor_v1 *) { static_cast<KeyboardCapture *>(d)->changed_(true); },
        [](void *d, zwp_keyboard_shortcuts_inhibitor_v1 *) { static_cast<KeyboardCapture *>(d)->changed_(false); }};
    zwp_keyboard_shortcuts_inhibitor_v1_add_listener(inhibitor_, &listener, this);
    return true;
}
void KeyboardCapture::release() {
    if (inhibitor_) zwp_keyboard_shortcuts_inhibitor_v1_destroy(inhibitor_);
    inhibitor_ = nullptr;
}
}
