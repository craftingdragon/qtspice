#include <QtSpice/QSpiceConnection.h>
#include <QHash>
#include <QStringList>

namespace QtSpice {
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
    config.password = fields.value(QStringLiteral("password"));
    config.certificateSubject = fields.value(QStringLiteral("host-subject"));
    QString ca = fields.value(QStringLiteral("ca"));
    ca.replace(QStringLiteral("\\n"), QStringLiteral("\n"));
    config.caPem = ca.toUtf8();
    if (config.proxy.isEmpty() == false && config.tlsPort == 0)
        return fail(QStringLiteral("Proxy connection requires tls-port"));
    return config;
}
}
