#pragma once
#include <QByteArray>
#include <QString>
#include <QUrl>
#include <optional>

namespace QtSpice {
enum class SharedDirectoryAccess { ReadOnly, ReadWrite };
struct SharedDirectory {
    QString path;
    SharedDirectoryAccess access;
};
// Resolves the directory and checks host permissions.
std::optional<SharedDirectory> validateSharedDirectory(const SharedDirectory &, QString *error = nullptr);

struct ConnectionConfig {
    QString host;
    quint16 port = 0;
    quint16 tlsPort = 0;
    QString proxy;
    QString password;
    QByteArray caPem;
    QString certificateSubject;
    // Optional outer HTTP CONNECT or SOCKS5 proxy, including for the .vv proxy.
    QString networkProxy;
    std::optional<SharedDirectory> sharedDirectory;
};

// Requires an explicit http:// or socks5:// URL. Errors never include credentials.
std::optional<QUrl> parseProxyUrl(const QString &value, QString *error = nullptr);

// Parses [virt-viewer] SPICE files. The error contains no connection secrets.
std::optional<ConnectionConfig> parseVv(const QByteArray &contents, QString *error = nullptr);
}
