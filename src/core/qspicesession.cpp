#include "qspicesession.h"
#include "spicelogging_p.h"
#include "keyboardcommands.h"
#include "clipboardcapability.h"
#include "displayframe.h"
#include <QtSpice/QSpiceDisplay.h>

#include <QMetaObject>
#include <QFileInfo>
#include <QTimer>
#include <QUuid>
#include <QLoggingCategory>
#include <utility>
#include <cmath>
#include <algorithm>
#include <spice/vd_agent.h>


Q_LOGGING_CATEGORY(inputLog, "qtspice.input", QtWarningMsg)
Q_LOGGING_CATEGORY(displayLog, "qtspice.display", QtWarningMsg)
Q_LOGGING_CATEGORY(connectionLog, "qtspice.connection", QtWarningMsg)

namespace {
const char *channelEventName(SpiceChannelEvent event) {
    switch (event) {
    case SPICE_CHANNEL_OPENED: return "opened";
    case SPICE_CHANNEL_SWITCHING: return "switching";
    case SPICE_CHANNEL_CLOSED: return "closed";
    case SPICE_CHANNEL_ERROR_CONNECT: return "error-connect";
    case SPICE_CHANNEL_ERROR_TLS: return "error-tls";
    case SPICE_CHANNEL_ERROR_LINK: return "error-link";
    case SPICE_CHANNEL_ERROR_AUTH: return "error-auth";
    case SPICE_CHANNEL_ERROR_IO: return "error-io";
    default: return "unknown";
    }
}
}

namespace QtSpice {
namespace {
int nativeCompression(Session::ImageCompression mode) {
    switch (mode) {
    case Session::ImageCompression::Automatic: return SPICE_IMAGE_COMPRESSION_AUTO_GLZ;
    case Session::ImageCompression::Uncompressed: return SPICE_IMAGE_COMPRESSION_OFF;
    default: return SPICE_IMAGE_COMPRESSION_INVALID;
    }
}
}
void Session::setImageCompression(ImageCompression preference) {
    if (preference != ImageCompression::ServerDefault && preference != ImageCompression::Automatic && preference != ImageCompression::Uncompressed) return;
    if (imageCompression_ == preference) return;
    imageCompression_ = preference;
    if (worker_->session) g_object_set(worker_->session, "preferred-compression", nativeCompression(preference), nullptr);
    if (preference != ImageCompression::ServerDefault) {
        for (auto it = worker_->compressionChannels.cbegin(); it != worker_->compressionChannels.cend(); ++it) if (it.value()) {
            worker_->compressionSender(it.key(), nativeCompression(preference));
            worker_->compressionOverrides.insert(it.key());
        }
    }
    qCDebug(displayLog) << "image compression preference" << int(preference);
    Q_EMIT imageCompressionChanged(preference);
    worker_->compressionStatusChanged();
}
QString Session::imageCompressionStatus() const {
    if (imageCompression_ == ImageCompression::ServerDefault) return worker_->compressionOverrides.isEmpty()
        ? QStringLiteral("Server default") : QStringLiteral("Reconnect to restore server defaults");
    if (worker_->compressionChannels.isEmpty()) return QStringLiteral("Applies on connection");
    int supported = 0;
    for (bool value : worker_->compressionChannels) supported += value;
    if (!supported) return QStringLiteral("Image compression preference unsupported; using server settings");
    if (supported != worker_->compressionChannels.size()) return QStringLiteral("Preference requested; unsupported display channels keep server settings");
    return QStringLiteral("Image compression preference requested");
}
void Session::Worker::compressionOpened(SpiceChannel *channel, bool supported) {
    compressionChannels[channel] = supported;
    // Native display initialization sends the session property on channel startup.
    compressionOverrides.remove(channel);
    if (supported && owner->imageCompression() != ImageCompression::ServerDefault) compressionOverrides.insert(channel);
    compressionStatusChanged();
}


bool Session::shareCd(const QString &path, QString *error) {
    if (state() != State::Connected) { if (error) *error = QStringLiteral("Console disconnected"); return false; }
    return worker_->usb.shareCd(path, error);
}
void Session::disconnectSharedCd() { worker_->usb.stopCd(); }
Session::SharedCdState Session::sharedCdState() const { return worker_->usb.cdState; }
QString Session::sharedCdSource() const { return worker_->usb.cdSource; }
QString Session::sharedCdError() const { return worker_->usb.cdError; }
QList<UsbDeviceInfo> Session::usbDevices() const { return worker_->usb.devices(); }
int Session::usbFreeChannels() const { return worker_->usb.freeChannels; }
QString Session::usbError() const { return worker_->usb.error; }
void Session::connectUsbDevice(quint64 id) { if (state_ == State::Connected) worker_->usb.request(id, true); }
void Session::disconnectUsbDevice(quint64 id) { worker_->usb.request(id, false); }
void Session::cancelUsbConnection() { worker_->usb.cancel(); }

Session::Worker::Worker(Session *o) : diagnostics("display", o), audio(o), microphone(o), usb(o), transfers(o), owner(o) {
    SpiceLogging::configure();
    diagnostics.updateGauges = [this] { diagnostics.gauge(QStringLiteral("pending-flushes"), displayDamage.size()); };
    clipboard.grab = [this](QList<quint32> types) { if (main) spice_main_channel_clipboard_selection_grab(main, 0, types.data(), types.size()); };
    clipboard.release = [this] { if (main) spice_main_channel_clipboard_selection_release(main, 0); };
    clipboard.request = [this](unsigned type) { if (main) spice_main_channel_clipboard_selection_request(main, 0, type); };
    clipboard.notify = [this](unsigned type, QByteArray bytes) { if (main) spice_main_channel_clipboard_selection_notify(main, 0, type, reinterpret_cast<const guchar *>(bytes.constData()), bytes.size()); };
    clipboard.received = [this](ClipboardContent content) {
        Q_EMIT owner->guestClipboardContent(content);
        if (content.text && content.image.isNull()) Q_EMIT owner->guestClipboardText(*content.text);
    };
}

Session::Worker::~Worker() {
    disconnect();
}

void Session::Worker::post(std::function<void()> task) {
    // spice-client-glib dispatches channel callbacks through Qt's GLib event dispatcher on Linux.
    // Keep SPICE calls and teardown on that same thread.
    task();
}

void Session::setMonitorStatus(const QString &status) {
    if (diagnostics_.monitorStatus != status) {
        diagnostics_.monitorStatus = status;
        Q_EMIT diagnosticsChanged();
    }
    Q_EMIT monitorConfigurationStatusChanged(status);
}

void Session::Worker::onChannelNew(SpiceSession *, SpiceChannel *channel, gpointer data) {
    auto *self = static_cast<Worker *>(data);
    g_signal_connect(channel, "channel-event", G_CALLBACK(onChannelEvent), self);
    int type = 0, id = 0;
    g_object_get(channel, "channel-type", &type, "channel-id", &id, nullptr);
    auto &records = self->owner->diagnostics_.channels;
    const char *channelName = spice_channel_type_to_string(type);
    records.append({type, id, channelName ? QString::fromUtf8(channelName) : QStringLiteral("Unknown"), {}, ChannelDiagnostics::State::Created});
    std::sort(records.begin(), records.end(), [](const auto &a, const auto &b) { return std::pair(a.type,a.id) < std::pair(b.type,b.id); });
    Q_EMIT self->owner->diagnosticsChanged();
    qCDebug(connectionLog) << self->owner << "channel created" << spice_channel_type_to_string(type) << "id" << id;
    if (SPICE_IS_WEBDAV_CHANNEL(channel)) {
        self->webdav.insert(channel);
        g_signal_connect(channel, "notify::port-opened", G_CALLBACK(+[](GObject *, GParamSpec *, gpointer data) {
            static_cast<Worker *>(data)->owner->updateSharingState();
        }), self);
        if (self->owner->sharedDirectory_ && self->owner->sharingError_.isEmpty()) spice_channel_connect(channel);
        self->owner->updateSharingState();
    }
    if (type == SPICE_CHANNEL_RECORD) self->microphone.attach(SPICE_RECORD_CHANNEL(channel));
    if (type == SPICE_CHANNEL_PLAYBACK) self->audio.attach(SPICE_PLAYBACK_CHANNEL(channel));
    if (type == SPICE_CHANNEL_MAIN) {
        self->main = SPICE_MAIN_CHANNEL(channel);
        self->transfers.attach(self->main);
        g_object_set(channel, "max-clipboard", int(clipboardImageLimit), nullptr);
        g_signal_connect(channel, "main-mouse-update", G_CALLBACK(onMouseUpdate), self);
        g_signal_connect(channel, "main-agent-update", G_CALLBACK(onAgentUpdate), self);
        g_signal_connect(channel, "notify::agent-connected", G_CALLBACK(+[](GObject *object, GParamSpec *, gpointer data) {
            Worker::onAgentUpdate(SPICE_MAIN_CHANNEL(object), data);
        }), self);
        g_signal_connect(channel, "main-clipboard-selection-grab", G_CALLBACK(onClipboardGrab), self);
        g_signal_connect(channel, "main-clipboard-selection", G_CALLBACK(onClipboardData), self);
        g_signal_connect(channel, "main-clipboard-selection-release", G_CALLBACK(onClipboardRelease), self);
        g_signal_connect(channel, "main-clipboard-selection-request", G_CALLBACK(onClipboardRequest), self);
    } else if (type == SPICE_CHANNEL_DISPLAY) {
        self->display = SPICE_DISPLAY_CHANNEL(channel);
        self->displays.insert(id, SPICE_DISPLAY_CHANNEL(channel));
        g_signal_connect(channel, "notify::monitors-max", G_CALLBACK(+[](GObject *, GParamSpec *, gpointer d) { static_cast<Worker *>(d)->refreshMonitors(); }), self);
        self->refreshMonitors();
        g_signal_connect(channel, "notify::monitors", G_CALLBACK(+[](GObject *c, GParamSpec *, gpointer d) {
            Worker::onDisplayChanged(SPICE_CHANNEL(c), d);
        }), self);
        g_signal_connect(channel, "display-primary-create", G_CALLBACK(+[](SpiceChannel *c, gint, gint, gint, gint, gint, gpointer, gpointer d) {
            Worker::onDisplayChanged(c, d);
        }), self);
        g_signal_connect(channel, "display-primary-destroy", G_CALLBACK(+[](SpiceChannel *c, gpointer d) {
            int id = 0; g_object_get(c, "channel-id", &id, nullptr);
            static_cast<Worker *>(d)->clearDisplay(id);
        }), self);
        g_signal_connect(channel, "display-invalidate", G_CALLBACK(onInvalidate), self);
    } else if (type == SPICE_CHANNEL_INPUTS) {
        self->inputs = SPICE_INPUTS_CHANNEL(channel);
        self->keyboardReadiness = {};
        g_signal_connect(channel, "inputs-modifiers", G_CALLBACK(+[](SpiceChannel *source, gpointer data) {
            auto *w = static_cast<Worker *>(data);
            if (source != SPICE_CHANNEL(w->inputs)) return;
            w->keyboardReadiness.initialized = true;
            if (!w->inputsReady && w->keyboardReadiness.ready()) {
                w->inputsReady = true;
                QMetaObject::invokeMethod(w->owner, [owner = w->owner, generation = w->generation] { if (generation != owner->worker_->generation) return; owner->setInputReady(true); }, Qt::QueuedConnection);
            }
        }), self);
    } else if (type == SPICE_CHANNEL_CURSOR) {
        self->cursor = SPICE_CURSOR_CHANNEL(channel);
        g_signal_connect(channel, "cursor-set", G_CALLBACK(onCursorSet), self);
        g_signal_connect(channel, "cursor-hide", G_CALLBACK(onCursorHide), self);
    }
    if ((type == SPICE_CHANNEL_DISPLAY) || type == SPICE_CHANNEL_INPUTS ||
        (type == SPICE_CHANNEL_CURSOR)) {
        spice_channel_connect(channel);
    }
    QMetaObject::invokeMethod(self->owner, [owner = self->owner, type, id, generation = self->generation] { if (generation != owner->worker_->generation) return; if (type == SPICE_CHANNEL_USBREDIR) owner->worker_->usb.refreshChannels(); Q_EMIT owner->channelAdded(type, id); }, Qt::QueuedConnection);
}

void Session::Worker::onChannelDestroy(SpiceSession *, SpiceChannel *channel, gpointer data) {
    auto *self = static_cast<Worker *>(data);
    int type = 0, id = 0;
    g_object_get(channel, "channel-type", &type, "channel-id", &id, nullptr);
    auto &records = self->owner->diagnostics_.channels;
    records.removeIf([=](const auto &c) { return c.type == type && c.id == id; });
    Q_EMIT self->owner->diagnosticsChanged();
    qCDebug(connectionLog) << self->owner << "channel destroyed" << spice_channel_type_to_string(type) << "id" << id;
    self->compressionChannels.remove(channel);
    self->compressionOverrides.remove(channel);
    self->compressionStatusChanged();
    self->audio.detach(channel);
    self->microphone.detach(channel);
    self->webdav.remove(channel);
    self->webdavOpened.remove(channel);
    self->owner->updateSharingState();
    if (type == SPICE_CHANNEL_DISPLAY) { self->clearDisplay(id); self->displays.remove(id); self->refreshMonitors(); }
    if (channel == SPICE_CHANNEL(self->display)) self->display = nullptr;
    if (channel == SPICE_CHANNEL(self->inputs)) {
        self->inputs = nullptr;
        self->inputsReady = false;
        self->keyboardReadiness = {};
        self->pressedKeys.clear();
        self->pressedButtons.clear();
        QMetaObject::invokeMethod(self->owner, [owner = self->owner, generation = self->generation] { if (generation != owner->worker_->generation) return; owner->setInputReady(false); }, Qt::QueuedConnection);
    }
    if (channel == SPICE_CHANNEL(self->cursor)) self->cursor = nullptr;
    if (channel == SPICE_CHANNEL(self->main)) {
        self->transfers.detach();
        self->main = nullptr;
        self->microphone.detach();
        auto &d = self->owner->diagnostics_;
        if (d.agentConnected || d.clipboardByDemand || d.monitorConfig || d.monitorPosition || d.fileTransferDisabled) {
            d.agentConnected = d.clipboardByDemand = d.monitorConfig = d.monitorPosition = d.fileTransferDisabled = false;
            Q_EMIT self->owner->diagnosticsChanged();
        }
        self->mainOpened = false;
        self->sentDisplaySize = {};
        self->clipboardReady = false;
        self->clipboard.reset(false);
        QMetaObject::invokeMethod(self->owner, [owner = self->owner, generation = self->generation] { if (generation != owner->worker_->generation) return; owner->setClipboardReady(false); }, Qt::QueuedConnection);
    }
    g_signal_handlers_disconnect_by_data(channel, self);
    QMetaObject::invokeMethod(self->owner, [owner = self->owner, type, id, generation = self->generation] { if (generation != owner->worker_->generation) return; if (type == SPICE_CHANNEL_USBREDIR) owner->worker_->usb.refreshChannels(); Q_EMIT owner->channelRemoved(type, id); }, Qt::QueuedConnection);
}

void Session::Worker::refreshMonitors() {
    QList<MonitorInfo> result;
    for (auto it = displays.cbegin(); it != displays.cend(); ++it) {
        guint maximum = 1; GArray *configs = nullptr;
        g_object_get(it.value(), "monitors-max", &maximum, "monitors", &configs, nullptr);
        const int count = it.key() == 0 ? int(std::min(maximum, 256u)) : 1;
        for (int i = 0; i < count; ++i) {
            MonitorInfo info{{it.key(), i}, {}, false};
            if (configs) for (guint j = 0; j < configs->len; ++j) {
                const auto &c = g_array_index(configs, SpiceDisplayMonitorConfig, j);
                if (c.id == guint(i) && c.surface_id == 0 && c.width && c.height && c.width <= 16384 && c.height <= 16384 && c.x <= 16384 && c.y <= 16384) {
                    info.geometry = QRect(c.x, c.y, c.width, c.height); info.enabled = true;
                }
            }
            if ((!configs || !configs->len) && !spice_channel_test_capability(SPICE_CHANNEL(it.value()), SPICE_DISPLAY_CAP_MONITORS_CONFIG)) {
                SpiceDisplayPrimary primary{};
                if (spice_display_channel_get_primary(SPICE_CHANNEL(it.value()), 0, &primary)) {
                    info.geometry = QRect(0, 0, primary.width, primary.height); info.enabled = !info.geometry.isEmpty();
                }
            }
            result.append(info);
        }
        if (configs) g_array_unref(configs);
    }
    if (owner->monitors_ == result) return;
    owner->monitors_ = result;
    bool firstTopology = awaitingTopology && std::any_of(result.cbegin(), result.cend(), [](const MonitorInfo &m) { return m.enabled; });
    if (firstTopology) {
        awaitingTopology = false;
        if (layoutPending) for (const auto &m : result) if (!desiredMonitors.contains(m.id)) desiredMonitors[m.id] = m;
    }
    observeMonitorLayout(result);
    for (auto it = owner->frames_.begin(); it != owner->frames_.end();) {
        bool enabled = false;
        for (const auto &m : result) if (m.id == it.key() && m.enabled) enabled = true;
        if (!enabled) { auto id = it.key(); it = owner->frames_.erase(it); Q_EMIT owner->monitorFrameUpdated(id, {}); Q_EMIT owner->monitorFrameReady(id, {}); if (id == MonitorId{}) Q_EMIT owner->frameReady({}); }
        else ++it;
    }
    Q_EMIT owner->monitorsChanged();
    if (firstTopology && layoutPending) sendDisplaySize();
}

void Session::Worker::observeMonitorLayout(const QList<MonitorInfo> &observed) {
    bool matches = true;
    for (const auto &wanted : std::as_const(desiredMonitors)) {
        bool found = false;
        for (const auto &actual : observed) if (actual.id == wanted.id) found = actual.enabled == wanted.enabled && (!wanted.enabled || actual.geometry == wanted.geometry);
        if (!found) matches = false;
    }
    if (layoutPending && matches) { layoutPending = false; ++layoutSerial; owner->setMonitorStatus({}); }
    if (!layoutPending) {
        // A new guest topology after timeout supersedes the expired request.
        ++layoutSerial;
        owner->setMonitorStatus({});
        desiredMonitors.clear();
        for (const auto &m : observed) desiredMonitors[m.id] = m;
    }
}

void Session::Worker::onDisplayChanged(SpiceChannel *channel, gpointer data) {
    auto *self = static_cast<Worker *>(data);
    int channelId = 0; g_object_get(channel, "channel-id", &channelId, nullptr);
    if (self->displays.value(channelId) != SPICE_DISPLAY_CHANNEL(channel)) return;
    self->refreshMonitors();
    self->scheduleDisplay(channel);
}
void Session::Worker::clearDisplay(int id) {
    ++surfaceRevisions[id];
    displayTickets.remove(id); displayDamage.remove(id); fullRefresh.remove(id);
    const auto keys = owner->frames_.keys();
    for (auto key : keys) if (key.channelId == id) {
        owner->frames_.remove(key);
        Q_EMIT owner->monitorFrameUpdated(key, {});
        Q_EMIT owner->monitorFrameReady(key, {});
        if (key == MonitorId{}) Q_EMIT owner->frameReady({});
    }
}
void Session::Worker::scheduleDisplay(SpiceChannel *channel, QRect damage) {
    int id = 0; g_object_get(channel, "channel-id", &id, nullptr);
    const bool pending = displayDamage.contains(id);
    if (damage.isEmpty()) fullRefresh.insert(id);
    else displayDamage[id] += damage;
    if (displayDamage[id].rectCount() > 64) displayDamage[id] = displayDamage[id].boundingRect();
    if (pending) return;
    const auto ticket = ++nextDisplayTicket;
    displayTickets[id] = ticket;
    const auto revision = displayRevision;
    const auto connection = generation;
    QMetaObject::invokeMethod(owner, [this, id, revision, connection, ticket] {
        if (revision != displayRevision || connection != generation || displayTickets.value(id) != ticket) return;
        displayTickets.remove(id);
        auto damage = displayDamage.take(id);
        const bool full = fullRefresh.remove(id);
        auto *channel = displays.value(id);
        if (!channel) return;
        diagnostics.add(QStringLiteral("flushes"));
        // renderMonitors consumes the union rather than its bounding rectangle.
        flushingDamage = full ? QRegion{} : damage;
        renderMonitors(SPICE_CHANNEL(channel), full ? QRect{} : damage.boundingRect());
    }, Qt::QueuedConnection);
}
void Session::Worker::renderMonitors(SpiceChannel *channel, QRect invalidated) {
    auto *self = this;
    const QRegion currentDamage = flushingDamage;
    int channelId = 0; g_object_get(channel, "channel-id", &channelId, nullptr);
    const auto surfaceRevision = surfaceRevisions.value(channelId);
    SpiceDisplayPrimary primary{};
    if (!primaryReader(channel, &primary)) {
        for (auto it = owner->frames_.begin(); it != owner->frames_.end();) {
            if (it.key().channelId != channelId) { ++it; continue; }
            auto id = it.key(); it = owner->frames_.erase(it);
            Q_EMIT owner->monitorFrameUpdated(id, {}); Q_EMIT owner->monitorFrameReady(id, {}); if (id == MonitorId{}) Q_EMIT owner->frameReady({});
        }
        return;
    }
    const auto monitors = owner->monitors_;
    const auto connection = generation;
    const auto revision = displayRevision;
    for (const auto &m : monitors) {
        if (m.id.channelId != channelId || !m.enabled) continue;
        if (!invalidated.isEmpty() && !m.geometry.intersects(invalidated)) continue;
        QElapsedTimer copyTime; if (self->diagnostics.enabled()) copyTime.start();
        auto &cached = self->owner->frames_[m.id];
        const QImage::Format format = primary.format == 80 ? QImage::Format_RGB16 : primary.format == 32 ? QImage::Format_RGB32 : primary.format == 96 ? QImage::Format_ARGB32 : QImage::Format_Invalid;
        QRegion damage = invalidated.isEmpty() ? QRegion(QRect(QPoint(), m.geometry.size())) : currentDamage.translated(-m.geometry.topLeft());
        damage = boundedDamage(damage, QRect(QPoint(), m.geometry.size()));
        if (!invalidated.isEmpty() && damage.isEmpty()) continue;
        quint64 copied = 0;
        if (cached.size() != m.geometry.size() || cached.format() != format || invalidated.isEmpty()) {
            cached = copyMonitorRect(primary, m.geometry);
            copied = cached.sizeInBytes();
            damage = cached.rect();
        } else {
            // Validate the whole surface and monitor before constructing a borrowed view.
            const int pixelBytes = primary.format == 80 ? 2 : 4;
            if (!primary.data || primary.stride < qint64(primary.width)*pixelBytes || primary.stride <= 0 || primary.width > 16384 || primary.height > 16384 || qint64(primary.stride)*primary.height > 512*1024*1024 || !QRect(0,0,primary.width,primary.height).contains(m.geometry)) {
                cached = {}; damage = {};
            } else {
                QImage source(primary.data + qint64(m.geometry.y())*primary.stride + m.geometry.x()*pixelBytes, m.geometry.width(), m.geometry.height(), primary.stride, format);
                copied = applyFrameDamage(cached, source, damage);
            }
        }
        const QImage image = cached;
        self->diagnostics.add(QStringLiteral("copied-bytes"), copied);
        if (copyTime.isValid()) self->diagnostics.add(QStringLiteral("copy-us"), copyTime.nsecsElapsed()/1000);

        self->diagnostics.add(QStringLiteral("copies"));
        self->diagnostics.add(QStringLiteral("frame-bytes"), image.sizeInBytes());
        Q_EMIT self->owner->monitorFrameUpdated(m.id, damage);
        if (connection != generation || revision != displayRevision || surfaceRevisions.value(channelId) != surfaceRevision || self->displays.value(channelId) != SPICE_DISPLAY_CHANNEL(channel)) return;
        self->diagnostics.add(QStringLiteral("delivered"));
        Q_EMIT self->owner->monitorFrameReady(m.id, image);
        if (connection != generation || revision != displayRevision || surfaceRevisions.value(channelId) != surfaceRevision) return;
        if (m.id == MonitorId{}) Q_EMIT self->owner->frameReady(image);
        if (connection != generation || revision != displayRevision || surfaceRevisions.value(channelId) != surfaceRevision) return;
    }
}

void Session::Worker::onInvalidate(SpiceChannel *channel, gint x, gint y, gint width, gint height, gpointer data) {
    static_cast<Worker *>(data)->diagnostics.add(QStringLiteral("invalidations"));
    if (width > 0 && height > 0) static_cast<Worker *>(data)->scheduleDisplay(channel, QRect(x,y,width,height));
}

void Session::Worker::onCursorSet(SpiceCursorChannel *channel, gint width, gint height, gint hotX, gint hotY, gpointer rgba, gpointer data) {
    auto *self = static_cast<Worker *>(data);
    QImage image;
    if (rgba && width > 0 && height > 0 && width <= 256 && height <= 256)
        image = QImage(static_cast<const uchar *>(rgba), width, height, QImage::Format_ARGB32).copy();
    int id = 0; g_object_get(channel, "channel-id", &id, nullptr);
    self->owner->cursors_[id] = {image, QPoint(hotX, hotY)};
    Q_EMIT self->owner->monitorCursorChanged(id, image, hotX, hotY);
    QMetaObject::invokeMethod(self->owner, [owner = self->owner, image, hotX, hotY, generation = self->generation] { if (generation != owner->worker_->generation) return; Q_EMIT owner->cursorChanged(image, hotX, hotY); }, Qt::QueuedConnection);
}

void Session::Worker::onCursorHide(SpiceCursorChannel *channel, gpointer data) {
    auto *self = static_cast<Worker *>(data);
    int id = 0; g_object_get(channel, "channel-id", &id, nullptr);
    self->owner->cursors_.remove(id);
    Q_EMIT self->owner->monitorCursorChanged(id, {}, 0, 0);
    QMetaObject::invokeMethod(self->owner, [owner = self->owner, generation = self->generation] { if (generation != owner->worker_->generation) return; Q_EMIT owner->cursorChanged({}, 0, 0); }, Qt::QueuedConnection);
}

void Session::Worker::onMouseUpdate(SpiceMainChannel *channel, gpointer data) {
    auto *self = static_cast<Worker *>(data);
    int mode = SPICE_MOUSE_MODE_CLIENT;
    g_object_get(channel, "mouse-mode", &mode, nullptr);
    qCDebug(inputLog) << "mouse mode" << mode;
    if (mode != SPICE_MOUSE_MODE_CLIENT && mode != SPICE_MOUSE_MODE_SERVER) return;
    const auto value = mode == SPICE_MOUSE_MODE_SERVER ? MouseMode::Server : MouseMode::Client;
    self->hasPointer = false;
    QMetaObject::invokeMethod(self->owner, [owner = self->owner, value, generation = self->generation] { if (generation != owner->worker_->generation) return; owner->setMouseMode(value); }, Qt::QueuedConnection);
}

void Session::Worker::onAgentUpdate(SpiceMainChannel *channel, gpointer data) {
    auto *self = static_cast<Worker *>(data);
    gboolean connected = FALSE;
    g_object_get(channel, "agent-connected", &connected, nullptr);
    gint caps = 0; g_object_get(channel, "agent-caps-0", &caps, nullptr);
    auto next = self->owner->diagnostics_;
    next.agentConnected = connected;
    auto cap = [=](int value) { return connected && spice_main_channel_agent_test_capability(channel, value); };
    next.clipboardByDemand = cap(VD_AGENT_CAP_CLIPBOARD_BY_DEMAND);
    next.monitorConfig = cap(VD_AGENT_CAP_MONITORS_CONFIG);
    next.monitorPosition = cap(VD_AGENT_CAP_MONITORS_CONFIG_POSITION);
    next.fileTransferDisabled = cap(VD_AGENT_CAP_FILE_XFER_DISABLED);
    if (next != self->owner->diagnostics_) { self->owner->diagnostics_ = next; Q_EMIT self->owner->diagnosticsChanged(); }
    const bool transferReady = self->mainOpened && connected && !(quint32(caps) & (1u << VD_AGENT_CAP_FILE_XFER_DISABLED));
    self->transfers.availability(transferReady, transferReady ? QString() : !connected ? QStringLiteral("Guest agent unavailable") : QStringLiteral("Guest file transfer unavailable or disabled"));
    // agent-connected is announced before capabilities; clipboard grab requires
    // BY_DEMAND and would otherwise produce a GSpice critical warning.
    const bool ready = clipboardByDemandReady(connected,
        spice_main_channel_agent_test_capability(channel, VD_AGENT_CAP_CLIPBOARD_BY_DEMAND));
    self->owner->monitorConfigurationAvailable_ = connected;
    Q_EMIT self->owner->monitorConfigurationAvailableChanged(connected);
    if (connected) self->sendDisplaySize();
    else self->sentDisplaySize = {};
    if (self->clipboardReady == ready) return;
    self->clipboardReady = ready;
    self->clipboard.reset(ready);
    QMetaObject::invokeMethod(self->owner, [owner = self->owner, ready, generation = self->generation] { if (generation != owner->worker_->generation) return; owner->setClipboardReady(ready); }, Qt::QueuedConnection);
}

gboolean Session::Worker::onClipboardGrab(SpiceMainChannel *channel, guint selection, gpointer types, guint count, gpointer data) {
    auto *self = static_cast<Worker *>(data);
    if (channel != self->main || !self->clipboardReady || selection != 0 || !types || count > 256) return FALSE;
    QList<quint32> offered;
    const auto *values = static_cast<const guint32 *>(types);
    for (guint i = 0; i < count; ++i) offered << values[i];
    return self->clipboard.offer(offered);
}
void Session::Worker::onClipboardData(SpiceMainChannel *channel, guint selection, guint type, gpointer bytes, guint size, gpointer data) {
    auto *self = static_cast<Worker *>(data);
    if (channel != self->main || selection != 0 || (size && !bytes)) return;
    if (size > clipboardImageLimit) { self->clipboard.data(VD_AGENT_CLIPBOARD_NONE, {}); return; }
    self->clipboard.data(type, QByteArray(static_cast<const char *>(bytes), size));
}
void Session::Worker::onClipboardRelease(SpiceMainChannel *channel, guint selection, gpointer data) {
    auto *self = static_cast<Worker *>(data);
    if (channel == self->main && selection == 0) self->clipboard.guestRelease();
}
gboolean Session::Worker::onClipboardRequest(SpiceMainChannel *channel, guint selection, guint type, gpointer data) {
    auto *self = static_cast<Worker *>(data);
    return channel == self->main && selection == 0 && self->clipboard.hostRequest(type);
}

void Session::Worker::onChannelEvent(SpiceChannel *channel, SpiceChannelEvent event, gpointer data) {
    auto *self = static_cast<Worker *>(data);
    if (SPICE_IS_USBREDIR_CHANNEL(channel)) {
        // The manager processes channel events after our handler. Refresh only
        // after all native handlers have updated its channel inventory.
        QMetaObject::invokeMethod(self->owner, [owner = self->owner, generation = self->generation] {
            if (generation == owner->worker_->generation) owner->worker_->usb.refreshChannels();
        }, Qt::QueuedConnection);
    }
    int type = 0, id = 0;
    g_object_get(channel, "channel-type", &type, "channel-id", &id, nullptr);
    if (type == SPICE_CHANNEL_DISPLAY) {
        if (event == SPICE_CHANNEL_OPENED) self->compressionOpened(channel, spice_channel_test_capability(channel, SPICE_DISPLAY_CAP_PREF_COMPRESSION));
        else { self->compressionChannels.remove(channel); self->compressionOverrides.remove(channel); self->compressionStatusChanged(); }
    }
    for (auto &record : self->owner->diagnostics_.channels) if (record.type == type && record.id == id) {
        auto next = record;
        next.event = QString::fromUtf8(channelEventName(event));
        using S = ChannelDiagnostics::State;
        if (event == SPICE_CHANNEL_OPENED) next.state = S::Open;
        else if (event == SPICE_CHANNEL_SWITCHING) next.state = S::Switching;
        else if (event == SPICE_CHANNEL_CLOSED) next.state = S::Closed;
        else if (event >= SPICE_CHANNEL_ERROR_CONNECT) next.state = S::Error;
        if (next != record) { record = next; Q_EMIT self->owner->diagnosticsChanged(); }
        break;
    }
    const GError *channelError = spice_channel_get_error(channel);
    qCDebug(connectionLog) << self->owner << "channel event" << spice_channel_type_to_string(type)
        << "id" << id << channelEventName(event) << "code" << int(event)
        << "error-domain" << (channelError ? g_quark_to_string(channelError->domain) : "none")
        << "error-code" << (channelError ? channelError->code : 0)
        << "message" << (channelError ? channelError->message : "none");
    if (channel == SPICE_CHANNEL(self->inputs)) {
        if (event == SPICE_CHANNEL_OPENED) {
            self->keyboardReadiness.opened = true;
            self->inputsReady = self->keyboardReadiness.ready();
        }
        else if (event == SPICE_CHANNEL_CLOSED || event >= SPICE_CHANNEL_ERROR_CONNECT) {
            self->inputsReady = false;
            self->keyboardReadiness = {};
            self->pressedKeys.clear();
            self->pressedButtons.clear();
        }
        const bool ready = self->inputsReady;
        qCDebug(inputLog) << "inputs channel event" << event << "ready" << ready;
        QMetaObject::invokeMethod(self->owner, [owner = self->owner, ready, generation = self->generation] { if (generation != owner->worker_->generation) return; owner->setInputReady(ready); }, Qt::QueuedConnection);
    }
    if (self->webdav.contains(channel)) {
        if (event == SPICE_CHANNEL_OPENED) self->webdavOpened.insert(channel);
        else self->webdavOpened.remove(channel);
        if (event >= SPICE_CHANNEL_ERROR_CONNECT) {
            const GError *error = spice_channel_get_error(channel);
            self->owner->sharingError_ = error ? QString::fromUtf8(error->message) : QStringLiteral("WebDAV channel connection failed");
        }
        self->owner->updateSharingState();
    }
    if (SPICE_IS_RECORD_CHANNEL(channel) && (event == SPICE_CHANNEL_CLOSED || event >= SPICE_CHANNEL_ERROR_CONNECT)) self->microphone.detach(channel);
    if (type != SPICE_CHANNEL_MAIN) return;
    if (event == SPICE_CHANNEL_OPENED) {
        self->mainOpened = true;
        onMouseUpdate(SPICE_MAIN_CHANNEL(channel), self);
        onAgentUpdate(SPICE_MAIN_CHANNEL(channel), self);
        QMetaObject::invokeMethod(self->owner, [owner = self->owner, generation = self->generation] { if (generation != owner->worker_->generation) return; owner->setState(State::Connected); }, Qt::QueuedConnection);
    } else if (event == SPICE_CHANNEL_CLOSED || event >= SPICE_CHANNEL_ERROR_CONNECT) {
        self->microphone.detach();
        auto &d = self->owner->diagnostics_;
        if (d.agentConnected || d.clipboardByDemand || d.monitorConfig || d.monitorPosition || d.fileTransferDisabled) {
            d.agentConnected = d.clipboardByDemand = d.monitorConfig = d.monitorPosition = d.fileTransferDisabled = false;
            Q_EMIT self->owner->diagnosticsChanged();
        }
        self->mainOpened = false;
        self->transfers.availability(false, QStringLiteral("Main channel disconnected"));
        self->sentDisplaySize = {};
        self->inputsReady = false;
        self->keyboardReadiness = {};
        const GError *error = spice_channel_get_error(channel);
        QString message = error ? QString::fromUtf8(error->message) : QStringLiteral("SPICE connection closed");
        QMetaObject::invokeMethod(self->owner, [owner = self->owner, message, generation = self->generation] { if (generation != owner->worker_->generation) return;
            const bool unexpected = owner->state() != State::Disconnected;
            owner->setState(State::Disconnected);
            if (generation != owner->worker_->generation) return;
            owner->worker_->disconnect();
            if (unexpected) Q_EMIT owner->errorOccurred(message);
        }, Qt::QueuedConnection);
    }
}

void Session::updateGuestInfo(const QString &name, const QString &uuid) {
    if (guestName_ == name && guestUuid_ == uuid) return;
    guestName_ = name; guestUuid_ = uuid;
    Q_EMIT guestInfoChanged();
}

void Session::Worker::refreshGuestInfo() {
    if (!session) return;
    gchar *name = nullptr;
    guint8 *uuid = nullptr; // Borrowed session property: copy before returning.
    g_object_get(session, "name", &name, "uuid", &uuid, nullptr);
    const auto id = uuid ? QUuid::fromRfc4122(QByteArray(reinterpret_cast<const char *>(uuid), 16)) : QUuid();
    owner->updateGuestInfo(QString::fromUtf8(name), id.isNull() ? QString() : id.toString(QUuid::WithoutBraces));
    g_free(name);
}

void Session::Worker::connectToConfig(const ConnectionConfig &config) {
    disconnect();
    // Do not log connection configuration: Proxmox proxy URLs can contain tickets.
    qCDebug(connectionLog) << owner << "connection requested" << "plain-port" << bool(config.port)
        << "tls-port" << bool(config.tlsPort) << "network-proxy" << !config.networkProxy.isEmpty()
        << "spice-proxy" << !config.proxy.isEmpty();
    QUrl proxyUrl;
    if (!config.networkProxy.isEmpty() || !config.proxy.isEmpty()) {
        proxy = std::make_unique<TunnelProxy>();
        const QUrl outer = config.networkProxy.isEmpty() ? QUrl() : *parseProxyUrl(config.networkProxy);
        const QUrl tunnel = config.proxy.isEmpty() ? QUrl() : *parseProxyUrl(config.proxy);
        if (!proxy->start(outer, tunnel)) {
            owner->setState(State::Disconnected);
            Q_EMIT owner->errorOccurred(QStringLiteral("Could not start local proxy bridge"));
            return;
        }
        proxyUrl = proxy->url();
    }
    session = spice_session_new();
    auto infoChanged = +[](GObject *, GParamSpec *, gpointer data) { static_cast<Worker *>(data)->refreshGuestInfo(); };
    g_signal_connect(session, "notify::name", G_CALLBACK(infoChanged), this);
    g_signal_connect(session, "notify::uuid", G_CALLBACK(infoChanged), this);
    g_object_set(session, "preferred-compression", nativeCompression(owner->imageCompression()), nullptr);
    usb.attach(session);
    owner->sharedDirectory_ = config.sharedDirectory;
    owner->sharingError_.clear();
    applySharing();
    const QByteArray name = config.host.toUtf8();
    const QByteArray port = config.port ? QByteArray::number(config.port) : QByteArray();
    const QByteArray tlsPort = config.tlsPort ? QByteArray::number(config.tlsPort) : QByteArray();
    const QByteArray proxyAddress = proxyUrl.toEncoded();
    const QByteArray password = config.password.toUtf8();
    const QByteArray subject = config.certificateSubject.toUtf8();
    g_object_set(session, "host", name.constData(), "port", port.isEmpty() ? nullptr : port.constData(),
                 "tls-port", tlsPort.isEmpty() ? nullptr : tlsPort.constData(),
                 "proxy", proxyAddress.isEmpty() ? nullptr : proxyAddress.constData(),
                 "password", password.isEmpty() ? nullptr : password.constData(),
                 "cert-subject", subject.isEmpty() ? nullptr : subject.constData(), nullptr);
    if (!config.caPem.isEmpty()) {
        GByteArray *ca = g_byte_array_sized_new(guint(config.caPem.size()));
        g_byte_array_append(ca, reinterpret_cast<const guint8 *>(config.caPem.constData()), guint(config.caPem.size()));
        g_object_set(session, "ca", ca, nullptr);
        g_byte_array_unref(ca);
    }
    if (!config.certificateSubject.isEmpty())
        g_object_set(session, "verify", SPICE_SESSION_VERIFY_SUBJECT, nullptr);
    if (config.tlsPort && !config.port) {
        const char *secure[] = {"all", nullptr};
        g_object_set(session, "secure-channels", secure, nullptr);
    }
    g_signal_connect(session, "channel-new", G_CALLBACK(onChannelNew), this);
    g_signal_connect(session, "channel-destroy", G_CALLBACK(onChannelDestroy), this);
    if (!spice_session_connect(session)) {
        qCDebug(connectionLog) << owner << "connection start rejected";
        QMetaObject::invokeMethod(owner, [owner = owner, generation = this->generation] { if (generation != owner->worker_->generation) return;
            owner->setState(State::Disconnected);
            if (generation != owner->worker_->generation) return;
            owner->worker_->disconnect();
            Q_EMIT owner->errorOccurred(QStringLiteral("Could not start SPICE connection"));
        }, Qt::QueuedConnection);
    }
}

void Session::Worker::disconnect() {
    compressionChannels.clear(); compressionOverrides.clear();
    compressionStatusChanged();
    ++generation;
    if (session) qCDebug(connectionLog) << owner << "local teardown requested";
    transfers.detach();
    usb.detach();
    audio.detach();
    microphone.detach();
    ++sharingGeneration;
    webdav.clear();
    webdavOpened.clear();
    owner->sharedDirectory_.reset();
    owner->sharingError_.clear();
    owner->updateSharingState();
    if (session) {
        releaseInputs();
        GList *channels = spice_session_get_channels(session);
        for (GList *item = channels; item; item = item->next)
            g_signal_handlers_disconnect_by_data(item->data, this);
        g_list_free(channels);
        g_signal_handlers_disconnect_by_data(session, this);
        spice_session_disconnect(session);
        g_object_unref(session);
        session = nullptr;
    }
    if (owner->diagnostics_ != SessionDiagnostics{}) { owner->diagnostics_ = {}; Q_EMIT owner->diagnosticsChanged(); }
    owner->updateGuestInfo({}, {});
    proxy.reset();
    ++displayRevision; displayDamage.clear(); displayTickets.clear(); fullRefresh.clear();
    displays.clear(); desiredMonitors.clear(); awaitingTopology = true; layoutPending = false; ++layoutSerial;
    owner->cursors_.clear();
    owner->frames_.clear(); owner->monitors_.clear();
    owner->monitorConfigurationAvailable_ = false;
    Q_EMIT owner->monitorConfigurationAvailableChanged(false);
    Q_EMIT owner->monitorsChanged();
    display = nullptr;
    inputs = nullptr;
    inputsReady = false;
    keyboardReadiness = {};
    cursor = nullptr;
    main = nullptr;
    mainOpened = false;
    sentDisplaySize = {};
    clipboardReady = false;
    clipboard.reset(false);
    hasPointer = false;
    pressedKeys.clear();
    pressedButtons.clear();
    buttonState = 0;
    owner->setInputReady(false);
    owner->setClipboardReady(false);
    owner->setMouseMode(MouseMode::Client);
}

void Session::Worker::sendDisplaySize() {
    if (!main || !mainOpened || awaitingTopology) return;
    gboolean connected = FALSE; g_object_get(main, "agent-connected", &connected, nullptr);
    if (!connected || desiredMonitors.isEmpty()) return;
    layoutPending = true;
    observeMonitorLayout(owner->monitors_);
    if (!layoutPending) return;
    g_object_set(main, "disable-display-position", FALSE, "disable-display-align", TRUE, nullptr);
    for (const auto &m : std::as_const(desiredMonitors)) {
        int id = m.id.channelId == 0 ? m.id.monitorId : m.id.channelId;
        qCDebug(displayLog) << "requested monitor" << m.id.channelId << m.id.monitorId << "wire-id" << id << "enabled" << m.enabled << "geometry" << m.geometry;
        spice_main_channel_update_display_enabled(main, id, m.enabled, FALSE);
        if (m.enabled) spice_main_channel_update_display(main, id, m.geometry.x(), m.geometry.y(), m.geometry.width(), m.geometry.height(), FALSE);
    }
    spice_main_channel_update_display_enabled(main, -1, FALSE, FALSE);
    for (const auto &m : std::as_const(desiredMonitors)) if (m.enabled)
        spice_main_channel_update_display_enabled(main, m.id.channelId == 0 ? m.id.monitorId : m.id.channelId, TRUE, FALSE);
    const bool sent = spice_main_channel_send_monitor_config(main);
    qCDebug(displayLog) << "monitor config sent" << sent << "agent monitor support" << spice_main_channel_agent_test_capability(main, VD_AGENT_CAP_MONITORS_CONFIG) << "position support" << spice_main_channel_agent_test_capability(main, VD_AGENT_CAP_MONITORS_CONFIG_POSITION);
    if (!sent) { owner->setMonitorStatus(QStringLiteral("Could not send guest monitor configuration")); return; }
    layoutPending = true; const auto serial = ++layoutSerial;
    owner->setMonitorStatus(QStringLiteral("Waiting for guest monitor configuration…"));
    QTimer::singleShot(5000, owner, [this, serial] {
        if (!layoutPending || serial != layoutSerial) return;
        observeMonitorLayout(owner->monitors_);
        if (!layoutPending) return;
        QString detail;
        for (const auto &wanted : std::as_const(desiredMonitors)) {
            auto actual = std::find_if(owner->monitors_.cbegin(), owner->monitors_.cend(), [&wanted](const MonitorInfo &m) { return m.id == wanted.id; });
            if (actual != owner->monitors_.cend() && actual->enabled == wanted.enabled && (!wanted.enabled || actual->geometry == wanted.geometry)) continue;
            auto describe = [](const MonitorInfo &m) {
                return m.enabled ? QStringLiteral("%1×%2 at %3,%4").arg(m.geometry.width()).arg(m.geometry.height()).arg(m.geometry.x()).arg(m.geometry.y()) : QStringLiteral("disabled");
            };
            detail = QStringLiteral(" · %1:%2 requested %3; guest %4").arg(wanted.id.channelId).arg(wanted.id.monitorId).arg(describe(wanted), actual == owner->monitors_.cend() ? QStringLiteral("unavailable") : describe(*actual));
            break;
        }
        layoutPending = false;
        owner->setMonitorStatus(QStringLiteral("Guest monitor configuration unconfirmed") + detail);
    });
}

void Session::Worker::releaseInputs() {
    if (inputsReady && inputs) {
        for (quint32 key : std::as_const(pressedKeys)) spice_inputs_channel_key_release(inputs, key);
        int remaining = buttonState;
        for (int button : std::as_const(pressedButtons)) {
            spice_inputs_channel_button_release(inputs, button, remaining);
            remaining &= ~(1 << (button - 1));
        }
    }
    pressedKeys.clear();
    pressedButtons.clear();
    buttonState = 0;
    hasPointer = false;
}


std::optional<SharedDirectory> validateSharedDirectory(const SharedDirectory &directory, QString *error) {
    if (error) error->clear();
    const QFileInfo info(directory.path);
    if (directory.path.isEmpty() || !info.isDir() || !info.isReadable() ||
        (directory.access != SharedDirectoryAccess::ReadOnly && directory.access != SharedDirectoryAccess::ReadWrite) ||
        (directory.access == SharedDirectoryAccess::ReadWrite && !info.isWritable())) {
        if (error) *error = QStringLiteral("Select an existing readable directory and a valid access mode; read/write requires write permission.");
        return std::nullopt;
    }
    return SharedDirectory{info.canonicalFilePath(), directory.access};
}

void Session::Worker::applySharing() {
    if (!session) return;
    const auto share = owner->sharedDirectory_;
    const QByteArray path = share ? share->path.toUtf8() : QByteArray();
    g_object_set(session, "share-dir-ro", !share || share->access == SharedDirectoryAccess::ReadOnly,
                 "shared-dir", share ? path.constData() : nullptr, nullptr);
    if (share) {
        GObject *server = nullptr;
        g_object_get(session, "webdav-server", &server, nullptr);
        if (!server) owner->sharingError_ = QStringLiteral("This libspice build has no WebDAV server support.");
        if (server) g_object_unref(server);
    }
    owner->updateSharingState();
}

bool Session::setSharedDirectory(const SharedDirectory &directory, QString *error) {
    const auto validated = validateSharedDirectory(directory, error);
    if (!validated) return false;
    disableSharedDirectory();
    sharedDirectory_ = validated;
    sharingError_.clear();
    worker_->applySharing();
    const int generation = worker_->sharingGeneration;
    // Allow disconnected channel coroutines to finish resetting before reconnecting.
    auto *retry = new QTimer(this);
    retry->setInterval(100);
    connect(retry, &QTimer::timeout, this, [this, generation, retry] {
        if (generation != worker_->sharingGeneration || !sharedDirectory_ || !sharingError_.isEmpty() ||
            worker_->webdav.isEmpty() || worker_->webdavOpened.size() == worker_->webdav.size()) {
            retry->stop(); retry->deleteLater(); return;
        }
        const auto channels = worker_->webdav;
        for (auto *channel : channels) spice_channel_connect(channel);
        updateSharingState();
    });
    retry->start();
    updateSharingState();
    return true;
}
void Session::disableSharedDirectory() {
    ++worker_->sharingGeneration;
    sharedDirectory_.reset();
    sharingError_.clear();
    worker_->applySharing();
    const auto channels = worker_->webdav;
    for (auto *channel : channels) spice_channel_disconnect(channel, SPICE_CHANNEL_CLOSED);
    worker_->webdavOpened.clear();
    updateSharingState();
}
std::optional<SharedDirectory> Session::sharedDirectory() const { return sharedDirectory_; }
Session::SharingState Session::sharingState() const noexcept { return sharingState_; }
QString Session::sharingError() const { return sharingError_; }
void Session::updateSharingState() {
    SharingState state = SharingState::Disabled;
    if (sharedDirectory_) {
        state = SharingState::WaitingForChannel;
        if (!sharingError_.isEmpty()) state = SharingState::Error;
        else if (!worker_->webdav.isEmpty()) {
            state = SharingState::Connecting;
            if (!worker_->webdavOpened.isEmpty()) state = SharingState::WaitingForGuest;
            for (auto *channel : worker_->webdavOpened) {
                gboolean opened = FALSE;
                g_object_get(channel, "port-opened", &opened, nullptr);
                if (opened) state = SharingState::Active;
            }
        }
    }
    sharingState_ = state;
    Q_EMIT sharingChanged();
}

Session::Session(QObject *parent) : QObject(parent), worker_(std::make_unique<Worker>(this)) {}
Session::~Session() = default;
Session::State Session::state() const noexcept { return state_; }
Session::MouseMode Session::mouseMode() const noexcept { return mouseMode_; }
bool Session::inputReady() const noexcept { return inputReady_; }
void Session::setClipboardMode(ClipboardMode mode) {
    if (mode < ClipboardMode::Bidirectional || mode > ClipboardMode::Disabled || mode == clipboardMode_) return;
    clipboardMode_ = mode;
    worker_->clipboard.setMode(mode);
    Q_EMIT clipboardModeChanged(mode);
}

bool Session::clipboardReady() const noexcept { return clipboardReady_; }

void Session::setState(State state) {
    if (state_ == state) return;
    qCDebug(connectionLog) << this << "state" << state_ << "->" << state;
    state_ = state;
    Q_EMIT stateChanged(state);
}
void Session::setMouseMode(MouseMode mode) {
    if (mouseMode_ == mode) return;
    mouseMode_ = mode;
    Q_EMIT mouseModeChanged(mode);
}
void Session::setInputReady(bool ready) {
    if (inputReady_ == ready) return;
    inputReady_ = ready;
    Q_EMIT inputReadyChanged(ready);
}
void Session::setClipboardReady(bool ready) {
    if (clipboardReady_ == ready) return;
    clipboardReady_ = ready;
    Q_EMIT clipboardReadyChanged(ready);
}

void Session::connectToHost(const QString &host, quint16 port) {
    ConnectionConfig config;
    config.host = host;
    config.port = port;
    connectToConfig(config);
}

void Session::connectToConfig(const ConnectionConfig &config) {
    if (config.host.trimmed().isEmpty() || (!config.port && !config.tlsPort)) {
        Q_EMIT errorOccurred(QStringLiteral("Host and nonzero port are required"));
        return;
    }
    ConnectionConfig normalized = config;
    if (!normalized.proxy.isEmpty() && !normalized.proxy.contains(QStringLiteral("://")))
        normalized.proxy.prepend(QStringLiteral("http://"));
    QString error;
    if ((!config.networkProxy.isEmpty() && !parseProxyUrl(config.networkProxy, &error)) ||
        (!normalized.proxy.isEmpty() && !parseProxyUrl(normalized.proxy, &error))) {
        Q_EMIT errorOccurred(error);
        return;
    }
    if (config.sharedDirectory) {
        normalized.sharedDirectory = validateSharedDirectory(*config.sharedDirectory, &error);
        if (!normalized.sharedDirectory) { Q_EMIT errorOccurred(error); return; }
    }
    setState(State::Connecting);
    worker_->post([worker = worker_.get(), normalized] { worker->connectToConfig(normalized); });
}

void Session::disconnectFromHost() {
    const auto generation = worker_->generation;
    setState(State::Disconnected);
    if (generation != worker_->generation) return;
    worker_->post([worker = worker_.get()] { worker->disconnect(); });
}

void Session::sendCtrlAltDelete() {
    worker_->post([w = worker_.get()] {
        if (w->inputsReady) qCDebug(inputLog) << "send Ctrl+Alt+Delete";
        dispatchCtrlAltDelete(w->inputsReady && w->inputs, w->pressedKeys,
            [w](quint32 code, bool pressed) {
                if (pressed) spice_inputs_channel_key_press(w->inputs, code);
                else spice_inputs_channel_key_release(w->inputs, code);
            });
    });
}
void Session::setKeyLocks(KeyboardLocks locks, KeyboardLocks knownLocks) {
    worker_->post([w = worker_.get(), locks, knownLocks] {
        if (!w->inputsReady || !w->inputs || !(knownLocks.toInt() & 7)) return;
        gint guest = 0;
        g_object_get(w->inputs, "key-modifiers", &guest, nullptr);
        const unsigned desired = mergedKeyLocks(guest, locks.toInt(), knownLocks.toInt());
        qCDebug(inputLog) << "synchronize key locks" << desired << "known" << knownLocks.toInt();
        spice_inputs_channel_set_key_locks(w->inputs, desired);
    });
}

void Session::sendKey(quint32 scancode, bool pressed) {
    if (!scancode) return;
    worker_->post([w = worker_.get(), scancode, pressed] {
        if (!w->inputsReady || !w->inputs) return;
        if (pressed && !w->pressedKeys.contains(scancode)) {
            w->pressedKeys.insert(scancode);
            spice_inputs_channel_key_press(w->inputs, scancode);
        } else if (!pressed && w->pressedKeys.remove(scancode)) {
            spice_inputs_channel_key_release(w->inputs, scancode);
        }
    });
}

void Session::sendPointer(int x, int y, int buttons) {
    worker_->post([w = worker_.get(), x, y, buttons] {
        if (!w->inputsReady || !w->inputs) return;
        int mode = SPICE_MOUSE_MODE_CLIENT;
        if (w->main) g_object_get(w->main, "mouse-mode", &mode, nullptr);
        qCDebug(inputLog) << "absolute pointer" << x << y << "mode" << mode << "buttons" << buttons;
        if (mode == SPICE_MOUSE_MODE_CLIENT) {
            spice_inputs_channel_position(w->inputs, x, y, w->displayId, buttons);
        } else if (mode == SPICE_MOUSE_MODE_SERVER && w->hasPointer) {
            spice_inputs_channel_motion(w->inputs, x - w->lastX, y - w->lastY, buttons);
        }
        w->lastX = x;
        w->lastY = y;
        w->hasPointer = true;
    });
}

void Session::sendRelativePointer(int dx, int dy, int buttons) {
    worker_->post([w = worker_.get(), dx, dy, buttons] {
        if (w->inputsReady && w->inputs && (dx || dy)) {
            qCDebug(inputLog) << "relative pointer" << dx << dy << "buttons" << buttons;
            spice_inputs_channel_motion(w->inputs, dx, dy, buttons);
        }
    });
}

void Session::sendButton(int button, bool pressed, int buttons) {
    if (button < 1 || button > 7) return;
    worker_->post([w = worker_.get(), button, pressed, buttons] {
        if (!w->inputsReady || !w->inputs) return;
        qCDebug(inputLog) << "button" << button << (pressed ? "press" : "release") << "prior state" << buttons;
        w->buttonState = pressed ? buttons | (1 << (button - 1)) : buttons & ~(1 << (button - 1));
        if (pressed) {
            if (!w->pressedButtons.contains(button)) {
                w->pressedButtons.insert(button);
                spice_inputs_channel_button_press(w->inputs, button, buttons);
            }
        } else if (w->pressedButtons.remove(button)) {
            spice_inputs_channel_button_release(w->inputs, button, buttons);
        }
    });
}

void Session::releaseAllInputs() { worker_->post([w = worker_.get()] { w->releaseInputs(); }); }

void Session::setHostClipboardContent(const ClipboardContent &content) {
    worker_->clipboard.setHost(content);
}
void Session::setHostClipboardText(const QString &text) {
    setHostClipboardContent({text, {}});
}

void Session::setDesiredDisplaySize(QSize size) { setDesiredMonitorSize({}, size); }
void Session::setDesiredMonitorSize(MonitorId id, QSize size) {
    if (size.isEmpty()) return;
    if (size.width() < 320 || size.height() < 200 || size.width() > 8192 || size.height() > 8192 || qint64(size.width()) * size.height() > 32 * 1024 * 1024) return;
    auto desired = worker_->desiredMonitors;
    if (desired.isEmpty()) for (const auto &m : monitors_) desired[m.id] = m;
    desired[id] = {id, QRect(QPoint(), size), true};
    QList<MonitorInfo> layout = desired.values();
    int x = 0; for (auto &m : layout) if (m.enabled) { m.geometry.moveTopLeft({x, 0}); x += m.geometry.width(); }
    requestMonitorLayout(layout);
}
void Session::setMonitorEnabled(MonitorId id, bool enabled) {
    if (!enabled && id == MonitorId{}) return;
    auto desired = worker_->desiredMonitors;
    if (desired.isEmpty()) for (const auto &m : monitors_) desired[m.id] = m;
    auto m = desired.value(id, {id, QRect(0, 0, 1024, 768), false});
    if (m.geometry.isEmpty()) m.geometry = QRect(0, 0, 1024, 768);
    m.enabled = enabled; desired[id] = m;
    auto layout = desired.values(); int x = 0;
    for (auto &head : layout) if (head.enabled) { head.geometry.moveTopLeft({x, 0}); x += head.geometry.width(); }
    requestMonitorLayout(layout);
}
void Session::requestMonitorLayout(const QList<MonitorInfo> &layout) {
    QMap<MonitorId, MonitorInfo> validated; QSet<int> ids;
    for (const auto &m : layout) {
        if (m.id.channelId < 0 || m.id.monitorId < 0) return;
        if (!monitors_.isEmpty()) {
            bool advertised = false; for (const auto &slot : monitors_) if (slot.id == m.id) advertised = true;
            if (!advertised) { setMonitorStatus(QStringLiteral("Monitor not advertised by guest")); return; }
        }
        int wire = m.id.channelId == 0 ? m.id.monitorId : m.id.channelId;
        if (wire < 0 || wire > 255 || ids.contains(wire) || (m.id.channelId != 0 && m.id.monitorId != 0)) return;
        ids.insert(wire);
        if (m.enabled && (m.geometry.isEmpty() || m.geometry.x() < 0 || m.geometry.y() < 0 || m.geometry.right() >= 16384 || m.geometry.bottom() >= 16384)) return;
        validated[m.id] = m;
    }
    if (diagnostics_.requestedMonitors != layout) { diagnostics_.requestedMonitors = layout; Q_EMIT diagnosticsChanged(); }
    if (validated != worker_->desiredMonitors) Q_EMIT monitorLayoutRequested(layout);
    const bool unchangedPending = validated == worker_->desiredMonitors && worker_->layoutPending;
    worker_->desiredMonitors = validated;
    worker_->layoutPending = true;
    worker_->observeMonitorLayout(monitors_);
    if (!worker_->layoutPending || unchangedPending) return;
    worker_->sendDisplaySize();
}
void Session::sendMonitorPointer(MonitorId id, int x, int y, int buttons) {
    bool active = false; for (const auto &m : monitors_) if (m.id == id && m.enabled) active = true;
    if (!active) return;
    worker_->displayId = id.channelId == 0 ? id.monitorId : id.channelId;
    sendPointer(x, y, buttons);
    worker_->displayId = 0;
}

} // namespace QtSpice

namespace QtSpice {
bool Session::audioEnabled() const { return worker_->audio.enabled; }
bool Session::audioMuted() const { return worker_->audio.muted; }
double Session::audioVolume() const { return worker_->audio.volume; }
Session::AudioState Session::audioState() const { return worker_->audio.state; }
QString Session::audioError() const { return worker_->audio.error; }
void Session::setAudioEnabled(bool v) { worker_->audio.setEnabled(v); }
void Session::setAudioMuted(bool v) { worker_->audio.muted = v; Q_EMIT audioChanged(); }
void Session::setAudioVolume(double v) { worker_->audio.volume = std::isfinite(v) ? std::clamp(v, 0.0, 1.0) : 1.0; Q_EMIT audioChanged(); }
}

namespace QtSpice {
bool Session::fileTransferReady() const { return state_ == State::Connected && worker_->transfers.ready; }
QString Session::fileTransferReadinessReason() const { return state_ == State::Connected ? worker_->transfers.reason : QStringLiteral("Console disconnected"); }
QList<FileTransferInfo> Session::fileTransfers() const { return worker_->transfers.rows; }
bool Session::sendFiles(const QStringList &paths, QString *error) { if (!fileTransferReady()) { if (error) *error = fileTransferReadinessReason(); return false; } return worker_->transfers.send(paths, error); }
void Session::cancelFileTransfer(quint64 id) { worker_->transfers.cancel(id); }
void Session::cancelAllFileTransfers() { worker_->transfers.cancelAll(); }
void Session::clearFinishedFileTransfers() { worker_->transfers.clearFinished(); }
}

namespace QtSpice {
bool Session::microphoneEnabled() const { return worker_->microphone.enabled; }
void Session::setMicrophoneEnabled(bool v) { worker_->microphone.setEnabled(v && state() == State::Connected); }
QByteArray Session::microphoneDevice() const { return worker_->microphone.deviceId; }
void Session::setMicrophoneDevice(const QByteArray &id) { worker_->microphone.select(id); }
QList<AudioInputDeviceInfo> Session::audioInputDevices() const { QList<AudioInputDeviceInfo> r; for (auto d : QMediaDevices::audioInputs()) r.append({d.id(), d.description()}); return r; }
Session::MicrophoneState Session::microphoneState() const { return worker_->microphone.state; }
QString Session::microphoneError() const { return worker_->microphone.error; }
}
