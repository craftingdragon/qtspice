#include "qspiceusb.h"
#include <QSet>
namespace QtSpice {
void UsbController::update(QList<UsbDeviceInfo> devices, int free) {
    rows.clear();
    for (auto row : devices) rows.insert(row.id, row);
    if (busy && !rows.contains(busy)) {
        cancel(); ++serial; busy = 0;
        if (pending) { g_object_unref(pending); pending = nullptr; }
    }
    freeChannels = free;
    changed();
}
void UsbController::request(quint64 id, bool connect) {
    if (busy || !rows.contains(id) || !operation) return;
    auto &row = rows[id];
    if (connect ? (!row.canRedirect || row.state != UsbDeviceInfo::State::Available) : row.state != UsbDeviceInfo::State::Connected) return;
    busy = id; connecting = connect; const auto attempt = ++serial;
    row.reason.clear(); row.state = connect ? UsbDeviceInfo::State::Connecting : UsbDeviceInfo::State::Disconnecting;
    auto token = lifetime;
    operation(id, connect, [token, attempt, id, connect](QString message) {
        auto *self = token->controller;
        if (!self || self->serial != attempt) return;
        self->busy = 0;
        if (self->pending) { g_object_unref(self->pending); self->pending = nullptr; }
        if (self->rows.contains(id)) {
            auto &row = self->rows[id];
            row.state = message.isEmpty() ? (connect ? UsbDeviceInfo::State::Connected : UsbDeviceInfo::State::Available)
                                         : (connect ? UsbDeviceInfo::State::Available : UsbDeviceInfo::State::Connected);
            row.reason = message;
        }
        self->refresh(); self->changed();
    });
    changed();
}
void UsbController::cancel() { if (busy && connecting && cancellation) cancellation(); }
void UsbController::detach() {
    lifetime->controller = nullptr;
    lifetime = std::make_shared<Lifetime>(Lifetime{this});
    ++serial;
    if (pending) { g_cancellable_cancel(pending); g_object_unref(pending); pending = nullptr; }
    if (manager) g_signal_handlers_disconnect_by_data(manager, this);
    manager = nullptr;
    for (auto device : native) g_boxed_free(SPICE_TYPE_USB_DEVICE, device);
    native.clear(); rows.clear(); busy = 0; freeChannels = 0;
    operation = {}; cancellation = {}; error.clear(); changed();
}
void UsbController::deviceSignal(SpiceUsbDeviceManager *, SpiceUsbDevice *, gpointer data) { static_cast<UsbController *>(data)->refresh(); }
void UsbController::deviceError(SpiceUsbDeviceManager *, SpiceUsbDevice *device, GError *err, gpointer data) {
    auto *self = static_cast<UsbController *>(data);
    self->refresh();
    for (auto it = self->native.begin(); it != self->native.end(); ++it)
        if (it.value() == device) self->rows[it.key()].reason = QString::fromUtf8(err->message);
    self->changed();
}
void UsbController::attach(SpiceSession *session) {
    detach();
    GError *err = nullptr;
    manager = spice_usb_device_manager_get(session, &err);
    if (!manager) { error = err ? QString::fromUtf8(err->message) : QStringLiteral("USB redirection unavailable"); if (err) g_error_free(err); changed(); return; }
    g_object_set(manager, "auto-connect", FALSE, "redirect-on-connect", nullptr, nullptr);
    g_signal_connect(manager, "device-added", G_CALLBACK(deviceSignal), this);
    g_signal_connect(manager, "device-removed", G_CALLBACK(deviceSignal), this);
    g_signal_connect(manager, "device-error", G_CALLBACK(deviceError), this);
    g_signal_connect(manager, "notify::free-channels", G_CALLBACK(+[](GObject *, GParamSpec *, gpointer data) { static_cast<UsbController *>(data)->refresh(); }), this);
    cancellation = [this] { if (pending) g_cancellable_cancel(pending); };
    operation = [this](quint64 id, bool connect, Completion done) {
        struct Request { Completion done; bool connect; SpiceUsbDevice *device; };
        auto *request = new Request{std::move(done), connect, static_cast<SpiceUsbDevice *>(g_boxed_copy(SPICE_TYPE_USB_DEVICE, native.value(id)))};
        pending = g_cancellable_new();
        auto callback = +[](GObject *source, GAsyncResult *result, gpointer data) {
            std::unique_ptr<Request> request(static_cast<Request *>(data));
            GError *err = nullptr;
            const bool ok = request->connect
                ? spice_usb_device_manager_connect_device_finish(SPICE_USB_DEVICE_MANAGER(source), result, &err)
                : spice_usb_device_manager_disconnect_device_finish(SPICE_USB_DEVICE_MANAGER(source), result, &err);
            const QString message = err ? QString::fromUtf8(err->message) : ok ? QString() : QStringLiteral("USB operation failed");
            if (err) g_error_free(err);
            g_boxed_free(SPICE_TYPE_USB_DEVICE, request->device);
            request->done(message);
        };
        if (connect) spice_usb_device_manager_connect_device_async(manager, native[id], pending, callback, request);
        else spice_usb_device_manager_disconnect_device_async(manager, native[id], pending, callback, request);
    };
    refresh();
}
void UsbController::refresh() {
    if (!manager) return;
    auto *devices = spice_usb_device_manager_get_devices(manager);
    auto *filtered = spice_usb_device_manager_get_devices_with_filter(manager, "0x03,-1,-1,-1,0|-1,-1,-1,-1,1");
    QSet<SpiceUsbDevice *> nonInput, present;
    if (filtered) for (guint i = 0; i < filtered->len; ++i) nonInput.insert(static_cast<SpiceUsbDevice *>(filtered->pdata[i]));
    if (devices) for (guint i = 0; i < devices->len; ++i) {
        auto *device = static_cast<SpiceUsbDevice *>(devices->pdata[i]); present.insert(device);
        quint64 id = native.key(device, 0);
        if (!id) { id = ++nextId; native[id] = static_cast<SpiceUsbDevice *>(g_boxed_copy(SPICE_TYPE_USB_DEVICE, device)); }
        auto &row = rows[id]; row.id = id; row.inputDevice = !nonInput.contains(device);
        char *description = spice_usb_device_get_description(device, nullptr);
        row.description = QString::fromUtf8(description ? description : "USB device"); g_free(description);
        if (busy != id) row.state = spice_usb_device_manager_is_device_connected(manager, device) ? UsbDeviceInfo::State::Connected : UsbDeviceInfo::State::Available;
        GError *err = nullptr;
        const bool wasRedirectable = row.canRedirect;
        row.canRedirect = spice_usb_device_manager_can_redirect_device(manager, device, &err);
        if (row.canRedirect && !wasRedirectable) row.reason.clear();
        if (!row.canRedirect && row.reason.isEmpty() && err) row.reason = QString::fromUtf8(err->message);
        if (err) g_error_free(err);
    }
    for (auto it = native.begin(); it != native.end();) {
        if (present.contains(it.value())) { ++it; continue; }
        if (busy == it.key()) { cancel(); ++serial; busy = 0; if (pending) { g_object_unref(pending); pending = nullptr; } }
        rows.remove(it.key()); g_boxed_free(SPICE_TYPE_USB_DEVICE, it.value()); it = native.erase(it);
    }
    if (devices) g_ptr_array_unref(devices);
    if (filtered) g_ptr_array_unref(filtered);
    g_object_get(manager, "free-channels", &freeChannels, nullptr);
    changed();
}
}
