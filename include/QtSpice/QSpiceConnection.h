#pragma once
#include <QByteArray>
#include <QString>
#include <optional>

namespace QtSpice {
struct ConnectionConfig {
    QString host;
    quint16 port = 0;
    quint16 tlsPort = 0;
    QString proxy;
    QString password;
    QByteArray caPem;
    QString certificateSubject;
};

// Parses [virt-viewer] SPICE files. The error contains no connection secrets.
std::optional<ConnectionConfig> parseVv(const QByteArray &contents, QString *error = nullptr);
}
