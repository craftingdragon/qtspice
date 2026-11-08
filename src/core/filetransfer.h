// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <QtSpice/QSpiceSession.h>
#include <spice-client.h>
#include <functional>
namespace QtSpice {
class FileTransferController {
public:
    explicit FileTransferController(Session *s) : owner(s) {}
    ~FileTransferController();
    using Progress = std::function<void(quint64, quint64)>;
    using Completion = std::function<void(QString)>;
    std::function<void(QString, Progress, Completion)> operation;
    std::function<void()> cancellation;
    QList<FileTransferInfo> rows;
    bool ready = false;
    QString reason = QStringLiteral("Console disconnected");
    void attach(SpiceMainChannel *);
    void availability(bool, QString);
    void detach();
    bool send(const QStringList &, QString *);
    void cancel(quint64);
    void cancelAll();
    void clearFinished();
private:
    Session *owner;
    struct Lifetime { FileTransferController *self; };
    std::shared_ptr<Lifetime> life = std::make_shared<Lifetime>(Lifetime{this});
    quint64 nextId = 0, active = 0, serial = 0;
    SpiceMainChannel *channel = nullptr;
    GCancellable *pending = nullptr;
    void start();
    void changed();
};
}
