// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QtSpice/QSpiceConnection.h>
#include <QHash>
#include <QStringList>

namespace QtSpice {
std::optional<QUrl> parseProxyUrl(const QString &value, QString *error) {
    if (error) error->clear();
    QUrl url(value, QUrl::StrictMode);
    if (!url.isValid() || url.host().isEmpty() ||
        (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("socks5")) ||
        url.port() == 0 || url.hasQuery() || url.hasFragment() ||
        (!url.path().isEmpty() && url.path() != QStringLiteral("/")) ||
        value.contains(QChar::Null) || url.userName().contains(QChar::Null) || url.password().contains(QChar::Null)) {
        if (error) *error = QStringLiteral("Invalid proxy URL; use http://host:port or socks5://host:port");
        return std::nullopt;
    }
    url.setPort(url.port(url.scheme() == QStringLiteral("http") ? 8080 : 1080));
    url.setPath({});
    return url;
}

std::optional<ConnectionConfig> parseVv(const QByteArray &contents, QString *error) {
    auto fail = [error](const QString &reason) -> std::optional<ConnectionConfig> {
        if (error) *error = reason;
        return std::nullopt;
    };
    if (error) error->clear();
    if (contents.isEmpty() || contents.size() > 1024 * 1024 || contents.contains('\0'))
        return fail(QStringLiteral("Invalid or oversized connection file"));
    const QString text = QString::fromUtf8(contents);
    const QStringList lines = text.split(QLatin1Char('\n'));
    QHash<QString, QString> fields;
    bool inSection = false;
    bool foundSection = false;
    for (QString line : lines) {
        if (line.endsWith(QLatin1Char('\r'))) line.chop(1);
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char(';'))) continue;
        if (line.startsWith(QLatin1Char('['))) {
            if (!line.endsWith(QLatin1Char(']'))) return fail(QStringLiteral("Malformed section header"));
            inSection = line.mid(1, line.size() - 2).trimmed().compare(QStringLiteral("virt-viewer"), Qt::CaseInsensitive) == 0;
            foundSection |= inSection;
            continue;
        }
        if (!inSection) continue;
        const qsizetype equal = line.indexOf(QLatin1Char('='));
        if (equal <= 0) return fail(QStringLiteral("Malformed connection field"));
        const QString key = line.left(equal).trimmed().toLower();
        if (fields.contains(key)) return fail(QStringLiteral("Duplicate connection field: %1").arg(key));
        fields.insert(key, line.mid(equal + 1).trimmed());
    }
    if (!foundSection) return fail(QStringLiteral("Missing [virt-viewer] section"));
    if (fields.value(QStringLiteral("type")).compare(QStringLiteral("spice"), Qt::CaseInsensitive) != 0)
        return fail(QStringLiteral("Only SPICE connection files are supported"));
    ConnectionConfig config;
    config.host = fields.value(QStringLiteral("host"));
    if (config.host.isEmpty()) return fail(QStringLiteral("Missing host"));
    auto parsePort = [&fields](const QString &key, quint16 &port) -> bool {
        const QString value = fields.value(key);
        if (value.isEmpty()) return true;
        bool ok = false;
        const uint n = value.toUInt(&ok);
        if (!ok || n == 0 || n > 65535) return false;
        port = quint16(n);
        return true;
    };
    if (!parsePort(QStringLiteral("port"), config.port) || !parsePort(QStringLiteral("tls-port"), config.tlsPort))
        return fail(QStringLiteral("Invalid port"));
    if (!config.port && !config.tlsPort) return fail(QStringLiteral("Missing port or tls-port"));
    config.proxy = fields.value(QStringLiteral("proxy"));
    // libspice also accepts legacy proxy=host:port entries in .vv files.
    if (!config.proxy.isEmpty() && !config.proxy.contains(QStringLiteral("://")))
        config.proxy.prepend(QStringLiteral("http://"));
    config.networkProxy = fields.value(QStringLiteral("network-proxy"));
    if ((!config.proxy.isEmpty() && !parseProxyUrl(config.proxy, error)) ||
        (!config.networkProxy.isEmpty() && !parseProxyUrl(config.networkProxy, error))) return std::nullopt;
    config.password = fields.value(QStringLiteral("password"));
    config.certificateSubject = fields.value(QStringLiteral("host-subject"));
    QString ca = fields.value(QStringLiteral("ca"));
    ca.replace(QStringLiteral("\\n"), QStringLiteral("\n"));
    config.caPem = ca.toUtf8();
    return config;
}
}
