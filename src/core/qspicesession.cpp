#include "qspicesession.h"
#include "keyboardcommands.h"
#include "clipboardcapability.h"
#include "displayframe.h"
#include <QtSpice/QSpiceDisplay.h>

#include <QMetaObject>
#include <QFileInfo>
#include <QTimer>
#include <QLoggingCategory>
#include <utility>
#include <cmath>
#include <algorithm>
#include <spice/vd_agent.h>

namespace { constexpr qsizetype maxClipboardBytes = 4 * 1024 * 1024; }

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

Session::Worker::Worker(Session *o) : diagnostics("display", o), audio(o), owner(o) {}

Session::Worker::~Worker() {
    disconnect();
}

void Session::Worker::post(std::function<void()> task) {
    // spice-client-glib dispatches channel callbacks through Qt's GLib event dispatcher on Linux.
    // Keep SPICE calls and teardown on that same thread.
    task();
}

void Session::Worker::onChannelNew(SpiceSession *, SpiceChannel *channel, gpointer data) {
    auto *self = static_cast<Worker *>(data);
    g_signal_connect(channel, "channel-event", G_CALLBACK(onChannelEvent), self);
    int type = 0, id = 0;
    g_object_get(channel, "channel-type", &type, "channel-id", &id, nullptr);
    qCDebug(connectionLog) << self->owner << "channel created" << spice_channel_type_to_string(type) << "id" << id;
    if (SPICE_IS_WEBDAV_CHANNEL(channel)) {
        self->webdav.insert(channel);
        g_signal_connect(channel, "notify::port-opened", G_CALLBACK(+[](GObject *, GParamSpec *, gpointer data) {
            static_cast<Worker *>(data)->owner->updateSharingState();
        }), self);
        if (self->owner->sharedDirectory_ && self->owner->sharingError_.isEmpty()) spice_channel_connect(channel);
        self->owner->updateSharingState();
    }
    if (type == SPICE_CHANNEL_PLAYBACK) self->audio.attach(SPICE_PLAYBACK_CHANNEL(channel));
    if (type == SPICE_CHANNEL_MAIN) {
        self->main = SPICE_MAIN_CHANNEL(channel);
        g_signal_connect(channel, "main-mouse-update", G_CALLBACK(onMouseUpdate), self);
        g_signal_connect(channel, "main-agent-update", G_CALLBACK(onAgentUpdate), self);
        g_signal_connect(channel, "notify::agent-connected", G_CALLBACK(+[](GObject *object, GParamSpec *, gpointer data) {
            Worker::onAgentUpdate(SPICE_MAIN_CHANNEL(object), data);
        }), self);
        g_signal_connect(channel, "main-clipboard-selection-grab", G_CALLBACK(onClipboardGrab), self);
        g_signal_connect(channel, "main-clipboard-selection", G_CALLBACK(onClipboardData), self);
        g_signal_connect(channel, "main-clipboard-selection-release", G_CALLBACK(onClipboardRelease), self);
        g_signal_connect(channel, "main-clipboard-selection-request", G_CALLBACK(onClipboardRequest), self);
    } else if (type == SPICE_CHANNEL_DISPLAY && id == 0) {
        self->display = SPICE_DISPLAY_CHANNEL(channel);
        self->displayId = id;
        g_signal_connect(channel, "notify::monitors", G_CALLBACK(+[](GObject *c, GParamSpec *, gpointer d) {
            Worker::onDisplayChanged(SPICE_CHANNEL(c), d);
        }), self);
        g_signal_connect(channel, "display-primary-create", G_CALLBACK(+[](SpiceChannel *c, gint, gint, gint, gint, gint, gpointer, gpointer d) {
            Worker::onDisplayChanged(c, d);
        }), self);
        g_signal_connect(channel, "display-primary-destroy", G_CALLBACK(onDisplayChanged), self);
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
                QMetaObject::invokeMethod(w->owner, [owner = w->owner] { owner->setInputReady(true); }, Qt::QueuedConnection);
            }
        }), self);
    } else if (type == SPICE_CHANNEL_CURSOR && id == 0) {
        self->cursor = SPICE_CURSOR_CHANNEL(channel);
        g_signal_connect(channel, "cursor-set", G_CALLBACK(onCursorSet), self);
        g_signal_connect(channel, "cursor-hide", G_CALLBACK(onCursorHide), self);
    }
    if ((type == SPICE_CHANNEL_DISPLAY && id == 0) || type == SPICE_CHANNEL_INPUTS ||
        (type == SPICE_CHANNEL_CURSOR && id == 0)) {
        spice_channel_connect(channel);
    }
    QMetaObject::invokeMethod(self->owner, [owner = self->owner, type, id] { Q_EMIT owner->channelAdded(type, id); }, Qt::QueuedConnection);
}

void Session::Worker::onChannelDestroy(SpiceSession *, SpiceChannel *channel, gpointer data) {
    auto *self = static_cast<Worker *>(data);
    int type = 0, id = 0;
    g_object_get(channel, "channel-type", &type, "channel-id", &id, nullptr);
    qCDebug(connectionLog) << self->owner << "channel destroyed" << spice_channel_type_to_string(type) << "id" << id;
    self->audio.detach(channel);
    self->webdav.remove(channel);
    self->webdavOpened.remove(channel);
    self->owner->updateSharingState();
    if (channel == SPICE_CHANNEL(self->display)) self->display = nullptr;
    if (channel == SPICE_CHANNEL(self->inputs)) {
        self->inputs = nullptr;
        self->inputsReady = false;
        self->keyboardReadiness = {};
        self->pressedKeys.clear();
        self->pressedButtons.clear();
        QMetaObject::invokeMethod(self->owner, [owner = self->owner] { owner->setInputReady(false); }, Qt::QueuedConnection);
    }
    if (channel == SPICE_CHANNEL(self->cursor)) self->cursor = nullptr;
    if (channel == SPICE_CHANNEL(self->main)) {
        self->main = nullptr;
        self->mainOpened = false;
        self->sentDisplaySize = {};
        self->clipboardReady = false;
        self->hostOwnsClipboard = false;
        self->guestClipboardRequestPending = false;
        QMetaObject::invokeMethod(self->owner, [owner = self->owner] { owner->setClipboardReady(false); }, Qt::QueuedConnection);
    }
    g_signal_handlers_disconnect_by_data(channel, self);
    QMetaObject::invokeMethod(self->owner, [owner = self->owner, type, id] { Q_EMIT owner->channelRemoved(type, id); }, Qt::QueuedConnection);
}

void Session::Worker::onDisplayChanged(SpiceChannel *channel, gpointer data) {
    auto *self = static_cast<Worker *>(data);
    QElapsedTimer copyTime;
    if (self->diagnostics.enabled()) copyTime.start();
    SpiceDisplayPrimary primary{};
    QImage image;
    if (spice_display_channel_get_primary(channel, 0, &primary)) {
        GArray *monitors = nullptr;
        g_object_get(channel, "monitors", &monitors, nullptr);
        image = copyMonitor(primary, monitors);
        if (monitors) g_array_unref(monitors);
        qCDebug(displayLog) << "surface" << QSize(primary.width, primary.height) << "visible monitor" << image.size();
    }
    self->diagnostics.add(QStringLiteral("copies"));
    if (copyTime.isValid()) self->diagnostics.add(QStringLiteral("copy-us"), quint64(copyTime.nsecsElapsed() / 1000));
    self->diagnostics.add(QStringLiteral("frame-bytes"), quint64(image.sizeInBytes()));
    ++self->pendingFrames;
    self->diagnostics.gauge(QStringLiteral("pending-frames"), self->pendingFrames);
    QMetaObject::invokeMethod(&self->diagnostics, [self, image] {
        --self->pendingFrames;
        self->diagnostics.gauge(QStringLiteral("pending-frames"), self->pendingFrames);
        self->diagnostics.add(QStringLiteral("delivered"));
        Q_EMIT self->owner->frameReady(image);
    }, Qt::QueuedConnection);
}

void Session::Worker::onInvalidate(SpiceChannel *channel, gint, gint, gint, gint, gpointer data) {
    static_cast<Worker *>(data)->diagnostics.add(QStringLiteral("invalidations"));
    onDisplayChanged(channel, data);
}

void Session::Worker::onCursorSet(SpiceCursorChannel *, gint width, gint height, gint hotX, gint hotY, gpointer rgba, gpointer data) {
    auto *self = static_cast<Worker *>(data);
    QImage image;
    if (rgba && width > 0 && height > 0 && width <= 256 && height <= 256)
        image = QImage(static_cast<const uchar *>(rgba), width, height, QImage::Format_ARGB32).copy();
    QMetaObject::invokeMethod(self->owner, [owner = self->owner, image, hotX, hotY] { Q_EMIT owner->cursorChanged(image, hotX, hotY); }, Qt::QueuedConnection);
}

void Session::Worker::onCursorHide(SpiceCursorChannel *, gpointer data) {
    auto *self = static_cast<Worker *>(data);
    QMetaObject::invokeMethod(self->owner, [owner = self->owner] { Q_EMIT owner->cursorChanged({}, 0, 0); }, Qt::QueuedConnection);
}

void Session::Worker::onMouseUpdate(SpiceMainChannel *channel, gpointer data) {
    auto *self = static_cast<Worker *>(data);
    int mode = SPICE_MOUSE_MODE_CLIENT;
    g_object_get(channel, "mouse-mode", &mode, nullptr);
    qCDebug(inputLog) << "mouse mode" << mode;
    if (mode != SPICE_MOUSE_MODE_CLIENT && mode != SPICE_MOUSE_MODE_SERVER) return;
    const auto value = mode == SPICE_MOUSE_MODE_SERVER ? MouseMode::Server : MouseMode::Client;
    self->hasPointer = false;
    QMetaObject::invokeMethod(self->owner, [owner = self->owner, value] { owner->setMouseMode(value); }, Qt::QueuedConnection);
}

void Session::Worker::onAgentUpdate(SpiceMainChannel *channel, gpointer data) {
    auto *self = static_cast<Worker *>(data);
    gboolean connected = FALSE;
    g_object_get(channel, "agent-connected", &connected, nullptr);
    // agent-connected is announced before capabilities; clipboard grab requires
    // BY_DEMAND and would otherwise produce a GSpice critical warning.
    const bool ready = clipboardByDemandReady(connected,
        spice_main_channel_agent_test_capability(channel, VD_AGENT_CAP_CLIPBOARD_BY_DEMAND));
    if (connected) self->sendDisplaySize();
    else self->sentDisplaySize = {};
    if (self->clipboardReady == ready) return;
    self->clipboardReady = ready;
    self->hostOwnsClipboard = false;
    self->guestClipboardRequestPending = false;
    if (ready && !self->hostClipboard.isEmpty()) {
        guint32 type = VD_AGENT_CLIPBOARD_UTF8_TEXT;
        spice_main_channel_clipboard_selection_grab(channel, VD_AGENT_CLIPBOARD_SELECTION_CLIPBOARD, &type, 1);
        self->hostOwnsClipboard = true;
    }
    QMetaObject::invokeMethod(self->owner, [owner = self->owner, ready] { owner->setClipboardReady(ready); }, Qt::QueuedConnection);
}

gboolean Session::Worker::onClipboardGrab(SpiceMainChannel *channel, guint selection, gpointer types, guint count, gpointer data) {
    auto *self = static_cast<Worker *>(data);
    if (!self->clipboardReady || !spice_main_channel_agent_test_capability(channel, VD_AGENT_CAP_CLIPBOARD_BY_DEMAND) ||
        selection != VD_AGENT_CLIPBOARD_SELECTION_CLIPBOARD || !types) return FALSE;
    const auto *offered = static_cast<const guint32 *>(types);
    for (guint i = 0; i < count; ++i) {
        if (offered[i] == VD_AGENT_CLIPBOARD_UTF8_TEXT) {
            self->hostOwnsClipboard = false;
            self->guestClipboardRequestPending = true;
            QMetaObject::invokeMethod(self->owner, [self, channel] {
                if (self->main == channel && self->clipboardReady && self->guestClipboardRequestPending &&
                    !self->hostOwnsClipboard &&
                    spice_main_channel_agent_test_capability(channel, VD_AGENT_CAP_CLIPBOARD_BY_DEMAND))
                    spice_main_channel_clipboard_selection_request(channel, VD_AGENT_CLIPBOARD_SELECTION_CLIPBOARD,
                                                                   VD_AGENT_CLIPBOARD_UTF8_TEXT);
            }, Qt::QueuedConnection);
            return TRUE;
        }
    }
    return FALSE;
}

void Session::Worker::onClipboardData(SpiceMainChannel *, guint selection, guint type, gpointer bytes, guint size, gpointer data) {
    auto *self = static_cast<Worker *>(data);
    if (!self->clipboardReady || !self->guestClipboardRequestPending || self->hostOwnsClipboard ||
        selection != VD_AGENT_CLIPBOARD_SELECTION_CLIPBOARD || type != VD_AGENT_CLIPBOARD_UTF8_TEXT ||
        size > maxClipboardBytes || (size && !bytes)) return;
    self->guestClipboardRequestPending = false;
    const QString text = QString::fromUtf8(static_cast<const char *>(bytes), size);
    QMetaObject::invokeMethod(self->owner, [owner = self->owner, text] { Q_EMIT owner->guestClipboardText(text); }, Qt::QueuedConnection);
}

void Session::Worker::onClipboardRelease(SpiceMainChannel *, guint selection, gpointer data) {
    auto *self = static_cast<Worker *>(data);
    if (selection == VD_AGENT_CLIPBOARD_SELECTION_CLIPBOARD) self->guestClipboardRequestPending = false;
}

gboolean Session::Worker::onClipboardRequest(SpiceMainChannel *channel, guint selection, guint type, gpointer data) {
    auto *self = static_cast<Worker *>(data);
    if (!self->clipboardReady || !self->hostOwnsClipboard ||
        !spice_main_channel_agent_test_capability(channel, VD_AGENT_CAP_CLIPBOARD_BY_DEMAND) ||
        selection != VD_AGENT_CLIPBOARD_SELECTION_CLIPBOARD ||
        type != VD_AGENT_CLIPBOARD_UTF8_TEXT) return FALSE;
    spice_main_channel_clipboard_selection_notify(channel, selection, type,
        reinterpret_cast<const guchar *>(self->hostClipboard.constData()), size_t(self->hostClipboard.size()));
    return TRUE;
}

void Session::Worker::onChannelEvent(SpiceChannel *channel, SpiceChannelEvent event, gpointer data) {
    auto *self = static_cast<Worker *>(data);
    int type = 0, id = 0;
    g_object_get(channel, "channel-type", &type, "channel-id", &id, nullptr);
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
        QMetaObject::invokeMethod(self->owner, [owner = self->owner, ready] { owner->setInputReady(ready); }, Qt::QueuedConnection);
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
    if (type != SPICE_CHANNEL_MAIN) return;
    if (event == SPICE_CHANNEL_OPENED) {
        self->mainOpened = true;
        onMouseUpdate(SPICE_MAIN_CHANNEL(channel), self);
        onAgentUpdate(SPICE_MAIN_CHANNEL(channel), self);
        QMetaObject::invokeMethod(self->owner, [owner = self->owner] { owner->setState(State::Connected); }, Qt::QueuedConnection);
    } else if (event == SPICE_CHANNEL_CLOSED || event >= SPICE_CHANNEL_ERROR_CONNECT) {
        self->mainOpened = false;
        self->sentDisplaySize = {};
        self->inputsReady = false;
        self->keyboardReadiness = {};
        const GError *error = spice_channel_get_error(channel);
        QString message = error ? QString::fromUtf8(error->message) : QStringLiteral("SPICE connection closed");
        QMetaObject::invokeMethod(self->owner, [owner = self->owner, message] {
            const bool unexpected = owner->state() != State::Disconnected;
            owner->setState(State::Disconnected);
            if (unexpected) Q_EMIT owner->errorOccurred(message);
        }, Qt::QueuedConnection);
    }
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
        QMetaObject::invokeMethod(owner, [owner = owner] {
            owner->setState(State::Disconnected);
            Q_EMIT owner->errorOccurred(QStringLiteral("Could not start SPICE connection"));
        }, Qt::QueuedConnection);
    }
}

void Session::Worker::disconnect() {
    if (session) qCDebug(connectionLog) << owner << "local teardown requested";
    audio.detach();
    ++sharingGeneration;
    webdav.clear();
    webdavOpened.clear();
    owner->sharedDirectory_.reset();
    owner->sharingError_.clear();
    owner->updateSharingState();
    if (!session) { proxy.reset(); return; }
    releaseInputs();
    GList *channels = spice_session_get_channels(session);
    for (GList *item = channels; item; item = item->next)
        g_signal_handlers_disconnect_by_data(item->data, this);
    g_list_free(channels);
    g_signal_handlers_disconnect_by_data(session, this);
    spice_session_disconnect(session);
    g_object_unref(session);
    session = nullptr;
    proxy.reset();
    display = nullptr;
    inputs = nullptr;
    inputsReady = false;
    keyboardReadiness = {};
    cursor = nullptr;
    main = nullptr;
    mainOpened = false;
    sentDisplaySize = {};
    clipboardReady = false;
    hostOwnsClipboard = false;
    guestClipboardRequestPending = false;
    hostClipboard.clear();
    hasPointer = false;
    pressedKeys.clear();
    pressedButtons.clear();
    buttonState = 0;
    owner->setInputReady(false);
    owner->setClipboardReady(false);
    owner->setMouseMode(MouseMode::Client);
}

void Session::Worker::sendDisplaySize() {
    if (!main || !mainOpened || desiredDisplaySize.isEmpty() || sentDisplaySize == desiredDisplaySize) return;
    gboolean agentConnected = FALSE;
    g_object_get(main, "agent-connected", &agentConnected, nullptr);
    if (!agentConnected) {
        qCDebug(displayLog) << "guest agent not connected; retaining resize request" << desiredDisplaySize;
        return;
    }
    // Older agents can accept monitor configuration without advertising this
    // capability; spice-gtk sends it whenever the agent is connected.
    spice_main_channel_update_display_enabled(main, 0, TRUE, FALSE);
    spice_main_channel_update_display(main, 0, 0, 0, desiredDisplaySize.width(), desiredDisplaySize.height(), FALSE);
    const bool sent = spice_main_channel_send_monitor_config(main);
    qCDebug(displayLog) << "monitor configuration" << desiredDisplaySize << "sent" << sent;
    if (sent) sentDisplaySize = desiredDisplaySize;
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
    worker_->post([worker = worker_.get()] { worker->disconnect(); });
    setState(State::Disconnected);
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

void Session::setHostClipboardText(const QString &text) {
    const QByteArray utf8 = text.toUtf8();
    worker_->post([w = worker_.get(), utf8] {
        const bool valid = utf8.size() <= maxClipboardBytes;
        if (valid && w->hostClipboard == utf8 && w->hostOwnsClipboard) return;
        w->hostClipboard = valid ? utf8 : QByteArray();
        w->guestClipboardRequestPending = false;
        if (!w->clipboardReady || !w->main ||
            !spice_main_channel_agent_test_capability(w->main, VD_AGENT_CAP_CLIPBOARD_BY_DEMAND)) return;
        if (!valid || utf8.isEmpty()) {
            if (w->hostOwnsClipboard)
                spice_main_channel_clipboard_selection_release(w->main, VD_AGENT_CLIPBOARD_SELECTION_CLIPBOARD);
            w->hostOwnsClipboard = false;
        } else {
            guint32 type = VD_AGENT_CLIPBOARD_UTF8_TEXT;
            spice_main_channel_clipboard_selection_grab(w->main, VD_AGENT_CLIPBOARD_SELECTION_CLIPBOARD, &type, 1);
            w->hostOwnsClipboard = true;
        }
    });
}

void Session::setDesiredDisplaySize(QSize size) {
    if (size.width() < 320 || size.height() < 200 || size.width() > 8192 || size.height() > 8192 ||
        qint64(size.width()) * size.height() > 32 * 1024 * 1024) size = {};
    worker_->post([w = worker_.get(), size] {
        if (w->desiredDisplaySize == size) return;
        w->desiredDisplaySize = size;
        if (!size.isEmpty()) w->sendDisplaySize();
    });
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
