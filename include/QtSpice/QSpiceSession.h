#pragma once

#include <QObject>
#include <QString>
#include <QImage>
#include <memory>

namespace QtSpice {

class Session final : public QObject {
    Q_OBJECT
public:
    enum class State { Disconnected, Connecting, Connected };
    Q_ENUM(State)

    explicit Session(QObject *parent = nullptr);
    ~Session() override;
    State state() const noexcept;
    void connectToHost(const QString &host, quint16 port);
    void disconnectFromHost();
    void sendKey(quint32 scancode, bool pressed);
    void sendPointer(int x, int y, int buttons);
    void sendButton(int button, bool pressed, int buttons);

Q_SIGNALS:
    void stateChanged(QtSpice::Session::State state);
    void errorOccurred(const QString &message);
    void channelAdded(int type, int id);
    void channelRemoved(int type, int id);
    void frameReady(const QImage &frame);
    void cursorChanged(const QImage &cursor, int hotX, int hotY);

private:
    struct Worker;
    std::unique_ptr<Worker> worker_;
    State state_ = State::Disconnected;
    void setState(State state);
};

} // namespace QtSpice
