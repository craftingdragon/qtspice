#pragma once

#include <QObject>
#include <QString>
#include <QImage>
#include <QtSpice/QSpiceConnection.h>
#include <memory>

namespace QtSpice {

class Session final : public QObject {
    Q_OBJECT
public:
    enum class State { Disconnected, Connecting, Connected };
    Q_ENUM(State)
    enum class MouseMode { Client, Server };
    Q_ENUM(MouseMode)

    explicit Session(QObject *parent = nullptr);
    ~Session() override;
    State state() const noexcept;
    MouseMode mouseMode() const noexcept;
    bool inputReady() const noexcept;
    void connectToHost(const QString &host, quint16 port);
    void connectToConfig(const ConnectionConfig &config);
    void disconnectFromHost();
    void sendKey(quint32 scancode, bool pressed);
    void sendPointer(int x, int y, int buttons);
    void sendRelativePointer(int dx, int dy, int buttons);
    void sendButton(int button, bool pressed, int buttons);
    void releaseAllInputs();

Q_SIGNALS:
    void stateChanged(QtSpice::Session::State state);
    void errorOccurred(const QString &message);
    void channelAdded(int type, int id);
    void channelRemoved(int type, int id);
    void frameReady(const QImage &frame);
    void cursorChanged(const QImage &cursor, int hotX, int hotY);
    void mouseModeChanged(QtSpice::Session::MouseMode mode);
    void inputReadyChanged(bool ready);

private:
    struct Worker;
    std::unique_ptr<Worker> worker_;
    State state_ = State::Disconnected;
    MouseMode mouseMode_ = MouseMode::Client;
    bool inputReady_ = false;
    void setState(State state);
    void setMouseMode(MouseMode mode);
    void setInputReady(bool ready);
};

} // namespace QtSpice
