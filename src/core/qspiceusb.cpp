// SPDX-License-Identifier: LGPL-3.0-or-later
#include "qspiceusb.h"
#include <QSet>
#include <QFileInfo>
#include <QFile>
#ifdef Q_OS_LINUX
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/cdrom.h>
#include <sys/stat.h>
#endif
namespace QtSpice {
QString UsbController::validateCdSource(const QString &path) {
    QFileInfo info(path);
    if (!info.exists()) return QStringLiteral("Source does not exist");
    if (info.isFile()) {
        QFile file(info.canonicalFilePath());
        if (!file.open(QIODevice::ReadOnly) || file.size() <= 0) return QStringLiteral("Image must be a readable, nonempty file");
        return {};
    }
#ifdef Q_OS_LINUX
    const auto name = QFile::encodeName(info.canonicalFilePath());
    // Never open FIFOs or arbitrary devices: optical drives are block devices.
    struct stat st {};
    if (::stat(name.constData(), &st) || !S_ISBLK(st.st_mode)) return QStringLiteral("Source is not an optical drive or image");
    int fd = ::open(name.constData(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) return QStringLiteral("Cannot open optical drive for reading");
    int result = ioctl(fd, CDROM_GET_CAPABILITY, 0); ::close(fd);
    if (result >= 0) return {};
#endif
    return QStringLiteral("Source is not a supported optical drive");
}
bool UsbController::shareCd(const QString &path, QString *out) {
    QString reason;
    if (busy || cdCreating || cdId) reason = QStringLiteral("A USB operation or CD share is already active");
    else if (!createCd) reason = QStringLiteral("CD sharing unavailable in this backend");
    else if (freeChannels <= 0) reason = QStringLiteral("No free USB redirection channel");
    else reason = validateCdSource(path);
    if (!reason.isEmpty()) { if (out) *out = reason; return false; }
    cdSource = QFileInfo(path).canonicalFilePath(); cdError.clear(); cdCancelled = false;
    cdCreating = true; cdState = Session::SharedCdState::Creating; cdChanged();
    reason = createCd(cdSource);
    if (!reason.isEmpty()) {
        cdCreating = false; cdState = Session::SharedCdState::Error; cdError = reason;
        cdChanged(); if (out) *out = reason; return false;
    }
    return true;
}
void UsbController::cdDeviceAdded(quint64 id) {
    if (!cdCreating || !rows.contains(id)) return;
    cdId = id; cdCreating = false;
    rows[id].description = QStringLiteral("Shared CD/DVD (%1)").arg(QFileInfo(cdSource).fileName());
    request(id, true);
    if (cdCancelled) cancel();
}
void UsbController::stopCd() {
    if (cdCreating) { cdCancelled = true; return; }
    if (cdId && busy == cdId && connecting) { cancel(); return; }
    if (cdId) request(cdId, false);
}
void UsbController::syncCd() {
    if (!cdId) return;
    const auto it = rows.constFind(cdId);
    if (it == rows.cend()) {
        cdId = 0; cdState = cdError.isEmpty() ? Session::SharedCdState::Idle : Session::SharedCdState::Error;
        cdSource.clear(); cdChanged(); return;
    }
    using S = UsbDeviceInfo::State;
    cdState = it->state == S::Connected ? Session::SharedCdState::Connected : it->state == S::Disconnecting ? Session::SharedCdState::Disconnecting : Session::SharedCdState::Connecting;
    if (!it->reason.isEmpty()) cdError = it->reason;
    cdChanged();
}
void UsbController::update(QList<UsbDeviceInfo> devices, int free) {
    rows.clear();
    for (auto row : devices) rows.insert(row.id, row);
    if (busy && !rows.contains(busy)) {
        cancel(); ++serial; busy = 0;
        if (pending) { g_object_unref(pending); pending = nullptr; }
    }
    freeChannels = free;
    syncCd();
    changed();
}
void UsbController::request(quint64 id, bool connect) {
    if (busy || (cdCreating && id != cdId) || !rows.contains(id) || !operation) return;
    auto &row = rows[id];
    if (connect ? ((!row.canRedirect && id != cdId) || row.state != UsbDeviceInfo::State::Available) : row.state != UsbDeviceInfo::State::Connected) return;
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
        if (id == self->cdId && !message.isEmpty()) self->cdError = message;
        self->refresh(); self->syncCd(); self->changed();
    });
    syncCd(); changed();
}
void UsbController::cancel() { if (busy && connecting && cancellation) cancellation(); }
void UsbController::detach() {
    lifetime->controller = nullptr;
    lifetime = std::make_shared<Lifetime>(Lifetime{this});
    ++serial;
    if (pending) { g_cancellable_cancel(pending); g_object_unref(pending); pending = nullptr; }
    if (manager) g_signal_handlers_disconnect_by_data(manager, this);
    manager = nullptr;
    cdId = 0; cdCreating = false; cdCancelled = false; createCd = {};
    cdState = Session::SharedCdState::Idle; cdSource.clear(); cdError.clear(); cdChanged();
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
#ifdef QTSPICE_HAVE_SHARED_CD
    createCd = [this](const QString &path) {
        GError *err = nullptr; auto bytes = QFile::encodeName(path);
        bool ok = spice_usb_device_manager_create_shared_cd_device(manager, bytes.data(), &err);
        QString result = err ? QString::fromUtf8(err->message) : ok ? QString() : QStringLiteral("Cannot create shared CD device");
        if (err) g_error_free(err);
        return result;
    };
#endif
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
        if (id == cdId) row.description = QStringLiteral("Shared CD/DVD (%1)").arg(QFileInfo(cdSource).fileName());
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
#ifdef QTSPICE_HAVE_SHARED_CD
    if (cdCreating) {
        for (auto it = native.cbegin(); it != native.cend(); ++it) {
            if (!spice_usb_device_manager_is_device_shared_cd(manager, it.value())) continue;
            cdDeviceAdded(it.key());
            break;
        }
    }
#endif
    syncCd();
    if (devices) g_ptr_array_unref(devices);
    if (filtered) g_ptr_array_unref(filtered);
    g_object_get(manager, "free-channels", &freeChannels, nullptr);
    changed();
}
}
