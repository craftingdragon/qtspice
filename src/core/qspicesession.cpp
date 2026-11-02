#include "qspicesession.h"
#include <QtSpice/QSpiceDisplay.h>

#include <QMetaObject>
#include <utility>

namespace QtSpice {

Session::Worker::Worker(Session *o)
    : owner(o), context(g_main_context_new()), loop(g_main_loop_new(context, FALSE)) {
    thread = std::thread([this] {
        g_main_context_push_thread_default(context);
        g_main_loop_run(loop);
        disconnect();
        g_main_context_pop_thread_default(context);
    });
}

Session::Worker::~Worker() {
    post([this] { g_main_loop_quit(loop); });
    thread.join();
    g_main_loop_unref(loop);
    g_main_context_unref(context);
}

void Session::Worker::post(std::function<void()> task) {
    auto *heap = new std::function<void()>(std::move(task));
    GSource *source = g_idle_source_new();
    g_source_set_callback(source, [](gpointer data) -> gboolean {
        (*static_cast<std::function<void()> *>(data))();
        return G_SOURCE_REMOVE;
    }, heap, [](gpointer data) { delete static_cast<std::function<void()> *>(data); });
    g_source_attach(source, context);
    g_source_unref(source);
}

void Session::Worker::onChannelNew(SpiceSession *, SpiceChannel *channel, gpointer data) {
    auto *self = static_cast<Worker *>(data);
    g_signal_connect(channel, "channel-event", G_CALLBACK(onChannelEvent), self);
    int type = 0, id = 0;
    g_object_get(channel, "channel-type", &type, "channel-id", &id, nullptr);
    if (type == SPICE_CHANNEL_MAIN) {
        self->main = SPICE_MAIN_CHANNEL(channel);
    } else if (type == SPICE_CHANNEL_DISPLAY && id == 0) {
        self->display = SPICE_DISPLAY_CHANNEL(channel);
        self->displayId = id;
        g_signal_connect(channel, "display-primary-create", G_CALLBACK(+[](SpiceChannel *c, gint, gint, gint, gint, gint, gpointer, gpointer d) {
            Worker::onDisplayChanged(c, d);
        }), self);
        g_signal_connect(channel, "display-primary-destroy", G_CALLBACK(onDisplayChanged), self);
        g_signal_connect(channel, "display-invalidate", G_CALLBACK(onInvalidate), self);
    } else if (type == SPICE_CHANNEL_INPUTS) {
        self->inputs = SPICE_INPUTS_CHANNEL(channel);
    } else if (type == SPICE_CHANNEL_CURSOR && id == 0) {
        self->cursor = SPICE_CURSOR_CHANNEL(channel);
        g_signal_connect(channel, "cursor-set", G_CALLBACK(onCursorSet), self);
        g_signal_connect(channel, "cursor-hide", G_CALLBACK(onCursorHide), self);
    }
    QMetaObject::invokeMethod(self->owner, [owner = self->owner, type, id] { Q_EMIT owner->channelAdded(type, id); }, Qt::QueuedConnection);
}

void Session::Worker::onChannelDestroy(SpiceSession *, SpiceChannel *channel, gpointer data) {
    auto *self = static_cast<Worker *>(data);
    int type = 0, id = 0;
    g_object_get(channel, "channel-type", &type, "channel-id", &id, nullptr);
    if (channel == SPICE_CHANNEL(self->display)) self->display = nullptr;
    if (channel == SPICE_CHANNEL(self->inputs)) self->inputs = nullptr;
    if (channel == SPICE_CHANNEL(self->cursor)) self->cursor = nullptr;
    if (channel == SPICE_CHANNEL(self->main)) self->main = nullptr;
    g_signal_handlers_disconnect_by_data(channel, self);
    QMetaObject::invokeMethod(self->owner, [owner = self->owner, type, id] { Q_EMIT owner->channelRemoved(type, id); }, Qt::QueuedConnection);
}

void Session::Worker::onDisplayChanged(SpiceChannel *channel, gpointer data) {
    auto *self = static_cast<Worker *>(data);
    SpiceDisplayPrimary primary{};
    QImage image;
    if (spice_display_channel_get_primary(channel, 0, &primary))
        image = copyPrimary(primary.format, primary.width, primary.height, primary.stride, primary.data);
    QMetaObject::invokeMethod(self->owner, [owner = self->owner, image] { Q_EMIT owner->frameReady(image); }, Qt::QueuedConnection);
}

void Session::Worker::onInvalidate(SpiceChannel *channel, gint, gint, gint, gint, gpointer data) {
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

void Session::Worker::onChannelEvent(SpiceChannel *channel, SpiceChannelEvent event, gpointer data) {
    auto *self = static_cast<Worker *>(data);
    int type = 0;
    g_object_get(channel, "channel-type", &type, nullptr);
    if (type != SPICE_CHANNEL_MAIN) return;
    if (event == SPICE_CHANNEL_OPENED) {
        QMetaObject::invokeMethod(self->owner, [owner = self->owner] { owner->setState(State::Connected); }, Qt::QueuedConnection);
    } else if (event >= SPICE_CHANNEL_ERROR_CONNECT) {
        const GError *error = spice_channel_get_error(channel);
        QString message = error ? QString::fromUtf8(error->message) : QStringLiteral("SPICE connection failed");
        QMetaObject::invokeMethod(self->owner, [owner = self->owner, message] {
            Q_EMIT owner->errorOccurred(message);
            owner->setState(State::Disconnected);
        }, Qt::QueuedConnection);
    }
}

void Session::Worker::connectToHost(const QString &host, quint16 port) {
    disconnect();
    session = spice_session_new();
    QByteArray name = host.toUtf8();
    QByteArray service = QByteArray::number(port);
    g_object_set(session, "host", name.constData(), "port", service.constData(), nullptr);
    g_signal_connect(session, "channel-new", G_CALLBACK(onChannelNew), this);
    g_signal_connect(session, "channel-destroy", G_CALLBACK(onChannelDestroy), this);
    if (!spice_session_connect(session)) {
        QMetaObject::invokeMethod(owner, [owner = owner] {
            Q_EMIT owner->errorOccurred(QStringLiteral("Could not start SPICE connection"));
            owner->setState(State::Disconnected);
        }, Qt::QueuedConnection);
    }
}

void Session::Worker::disconnect() {
    if (!session) return;
    spice_session_disconnect(session);
    g_signal_handlers_disconnect_by_data(session, this);
    g_object_unref(session);
    session = nullptr;
    display = nullptr;
    inputs = nullptr;
    cursor = nullptr;
    main = nullptr;
    hasPointer = false;
}

Session::Session(QObject *parent) : QObject(parent), worker_(std::make_unique<Worker>(this)) {}
Session::~Session() = default;
Session::State Session::state() const noexcept { return state_; }

void Session::setState(State state) {
    if (state_ == state) return;
    state_ = state;
    Q_EMIT stateChanged(state);
}

void Session::connectToHost(const QString &host, quint16 port) {
    if (host.trimmed().isEmpty() || port == 0) {
        Q_EMIT errorOccurred(QStringLiteral("Host and nonzero port are required"));
        return;
    }
    setState(State::Connecting);
    worker_->post([worker = worker_.get(), host, port] { worker->connectToHost(host, port); });
}

void Session::disconnectFromHost() {
    worker_->post([worker = worker_.get()] { worker->disconnect(); });
    setState(State::Disconnected);
}

void Session::sendKey(quint32 scancode, bool pressed) {
    if (!scancode) return;
    worker_->post([w = worker_.get(), scancode, pressed] {
        if (!w->inputs) return;
        if (pressed) spice_inputs_channel_key_press(w->inputs, scancode);
        else spice_inputs_channel_key_release(w->inputs, scancode);
    });
}

void Session::sendPointer(int x, int y, int buttons) {
    worker_->post([w = worker_.get(), x, y, buttons] {
        if (!w->inputs) return;
        int mode = SPICE_MOUSE_MODE_CLIENT;
        if (w->main) g_object_get(w->main, "mouse-mode", &mode, nullptr);
        if (mode == SPICE_MOUSE_MODE_SERVER) {
            if (w->hasPointer) spice_inputs_channel_motion(w->inputs, x - w->lastX, y - w->lastY, buttons);
        } else {
            spice_inputs_channel_position(w->inputs, x, y, w->displayId, buttons);
        }
        w->lastX = x;
        w->lastY = y;
        w->hasPointer = true;
    });
}

void Session::sendButton(int button, bool pressed, int buttons) {
    worker_->post([w = worker_.get(), button, pressed, buttons] {
        if (!w->inputs) return;
        if (pressed) spice_inputs_channel_button_press(w->inputs, button, buttons);
        else spice_inputs_channel_button_release(w->inputs, button, buttons);
    });
}

} // namespace QtSpice
