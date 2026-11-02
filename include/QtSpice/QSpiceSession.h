#pragma once

#include <QObject>
#include <QString>
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

Q_SIGNALS:
    void stateChanged(QtSpice::Session::State state);
    void errorOccurred(const QString &message);
    void channelAdded(int type, int id);
    void channelRemoved(int type, int id);

private:
    struct Worker;
    std::unique_ptr<Worker> worker_;
    State state_ = State::Disconnected;
    void setState(State state);
};

} // namespace QtSpice
