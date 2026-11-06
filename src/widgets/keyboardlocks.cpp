#include "keyboardlocks.h"
#include <QGuiApplication>
#include <QWindow>
#if defined(QTSPICE_LOCKS_WAYLAND) || defined(QTSPICE_LOCKS_X11)
#include <QtGui/qguiapplication_platform.h>
#endif
#if defined(QTSPICE_LOCKS_WAYLAND) && !QT_CONFIG(wayland)
#undef QTSPICE_LOCKS_WAYLAND
#endif
#if defined(QTSPICE_LOCKS_X11) && !QT_CONFIG(xcb)
#undef QTSPICE_LOCKS_X11
#endif
#ifdef QTSPICE_LOCKS_WAYLAND
#include <QtGui/qpa/qplatformnativeinterface.h>
#include <wayland-client.h>
#include "xkblocks.h"
#include "focusedlocks.h"
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#endif
#ifdef QTSPICE_LOCKS_X11
#include <X11/XKBlib.h>
#endif
namespace QtSpice {
struct KeyboardLocks::Native {
    KeyboardLocks *owner;
#ifdef QTSPICE_LOCKS_WAYLAND
    wl_keyboard *keyboard = nullptr;
    FocusedLocks focused;
    xkb_context *context = nullptr;
    xkb_keymap *keymap = nullptr;
    xkb_state *state = nullptr;
    void clearKeymap() {
        focused.invalidate();
        if (state) xkb_state_unref(state);
        if (keymap) xkb_keymap_unref(keymap);
        state = nullptr;
        keymap = nullptr;
    }
    void attach() {
        auto *app = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
        if (keyboard || !app || !app->seat() || !app->keyboard()) return;
        context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        if (!context) return;
        keyboard = wl_seat_get_keyboard(app->seat());
        static const wl_keyboard_listener listener{
            [](void *d, wl_keyboard *, uint32_t format, int32_t fd, uint32_t size) {
                auto *n = static_cast<Native *>(d);
                n->clearKeymap();
                struct stat metadata{};
                if (format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 && size > 0 && size <= 16*1024*1024 &&
                    fstat(fd, &metadata) == 0 && metadata.st_size >= size) {
                    void *map = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
                    if (map != MAP_FAILED) {
                        if (static_cast<char *>(map)[size-1] == '\0')
                            n->keymap = xkb_keymap_new_from_string(n->context, static_cast<char *>(map), XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
                        munmap(map, size);
                    }
                    if (n->keymap) n->state = xkb_state_new(n->keymap);
                }
                close(fd);
                n->notify();
            },
            [](void *d, wl_keyboard *, uint32_t, wl_surface *surface, wl_array *) {
                auto *n = static_cast<Native *>(d);
                n->focused.enter(surface);
                n->notify();
            },
            [](void *d, wl_keyboard *, uint32_t, wl_surface *) {
                auto *n = static_cast<Native *>(d);
                n->focused.leave();
                n->notify();
            },
            [](void *, wl_keyboard *, uint32_t, uint32_t, uint32_t, uint32_t) {},
            [](void *d, wl_keyboard *, uint32_t, uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group) {
                auto *n = static_cast<Native *>(d);
                if (n->state) {
                    xkb_state_update_mask(n->state, depressed, latched, locked, 0, 0, group);
                    n->focused.update(xkbLocks(n->state));
                }
                n->notify();
            },
            [](void *, wl_keyboard *, int32_t, int32_t) {}
        };
        wl_keyboard_add_listener(keyboard, &listener, this);
        notify();
    }
#endif
    void notify() { if (owner->changed) owner->changed(); }
    ~Native() {
#ifdef QTSPICE_LOCKS_WAYLAND
        if (keyboard) {
            if (wl_keyboard_get_version(keyboard) >= WL_KEYBOARD_RELEASE_SINCE_VERSION) wl_keyboard_release(keyboard);
            else wl_keyboard_destroy(keyboard);
        }
        clearKeymap();
        if (context) xkb_context_unref(context);
#endif
    }
};
KeyboardLocks::KeyboardLocks(QObject *parent) : QObject(parent), native_(std::make_unique<Native>()) {
    native_->owner = this;
#ifdef QTSPICE_LOCKS_WAYLAND
    native_->attach();
#endif
}
KeyboardLocks::~KeyboardLocks() = default;
bool KeyboardLocks::available() const {
#ifdef QTSPICE_LOCKS_WAYLAND
    if (native_->keyboard) return true;
#endif
#ifdef QTSPICE_LOCKS_X11
    if (auto *app = qGuiApp->nativeInterface<QNativeInterface::QX11Application>(); app && app->display()) {
        int opcode, event, error, major = XkbMajorVersion, minor = XkbMinorVersion;
        return XkbQueryExtension(app->display(), &opcode, &event, &error, &major, &minor);
    }
#endif
    return false;
}
LockSnapshot KeyboardLocks::snapshot(QWindow *window) {
    if (!window) return {};
#ifdef QTSPICE_LOCKS_WAYLAND
    native_->attach();
    if (native_->keyboard) {
        auto *platform = QGuiApplication::platformNativeInterface();
        auto *surface = platform ? static_cast<wl_surface *>(platform->nativeResourceForWindow("surface", window)) : nullptr;
        return native_->focused.forSurface(surface);
    }
#endif
#ifdef QTSPICE_LOCKS_X11
    if (auto *app = qGuiApp->nativeInterface<QNativeInterface::QX11Application>(); app && app->display()) {
        LockSnapshot result;
        for (auto [name, bit] : {std::pair{"Scroll Lock", 1u}, std::pair{"Num Lock", 2u}, std::pair{"Caps Lock", 4u}}) {
            const Atom atom = XInternAtom(app->display(), name, True);
            Bool active = False;
            if (atom && XkbGetNamedIndicator(app->display(), atom, nullptr, &active, nullptr, nullptr)) {
                result.known |= bit;
                if (active) result.locks |= bit;
            }
        }
        return result;
    }
#endif
    return {};
}
}
