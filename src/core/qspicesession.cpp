#include "qspicesession.h"

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
    QMetaObject::invokeMethod(self->owner, [owner = self->owner, type, id] { Q_EMIT owner->channelAdded(type, id); }, Qt::QueuedConnection);
}

void Session::Worker::onChannelDestroy(SpiceSession *, SpiceChannel *channel, gpointer data) {
    auto *self = static_cast<Worker *>(data);
    int type = 0, id = 0;
    g_object_get(channel, "channel-type", &type, "channel-id", &id, nullptr);
    g_signal_handlers_disconnect_by_data(channel, self);
    QMetaObject::invokeMethod(self->owner, [owner = self->owner, type, id] { Q_EMIT owner->channelRemoved(type, id); }, Qt::QueuedConnection);
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

} // namespace QtSpice
