// SPDX-License-Identifier: LGPL-3.0-or-later
#include "certificatestore.h"
#include <QCryptographicHash>

CertificateStore::CertificateStore(const QString &settingsFile) {
    if (settingsFile.isEmpty())
        settings_ = std::make_unique<QSettings>(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("QtSpice"), QStringLiteral("ProxmoxViewer"));
    else
        settings_ = std::make_unique<QSettings>(settingsFile, QSettings::IniFormat);
}

QString CertificateStore::fingerprint(const QByteArray &leafDer) {
    return QString::fromLatin1(QCryptographicHash::hash(leafDer, QCryptographicHash::Sha256).toHex());
}

QString CertificateStore::originKey(const QUrl &url) {
    if (url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0 || url.host().isEmpty()) return {};
    QUrl origin;
    origin.setScheme(QStringLiteral("https"));
    origin.setHost(url.host().toLower());
    origin.setPort(url.port(443));
    return QString::fromLatin1(QCryptographicHash::hash(origin.toEncoded(), QCryptographicHash::Sha256).toHex());
}

bool CertificateStore::isTrusted(const QUrl &url, const QByteArray &leafDer, int warningType) const {
    const QString key = originKey(url);
    if (key.isEmpty() || leafDer.isEmpty()) return false;
    const QString prefix = QStringLiteral("certificates/%1/").arg(key);
    return settings_->value(prefix + QStringLiteral("fingerprint")).toString() == fingerprint(leafDer) &&
           settings_->value(prefix + QStringLiteral("warnings")).toStringList().contains(QString::number(warningType));
}

void CertificateStore::remember(const QUrl &url, const QByteArray &leafDer, int warningType) {
    const QString key = originKey(url);
    if (key.isEmpty() || leafDer.isEmpty()) return;
    const QString prefix = QStringLiteral("certificates/%1/").arg(key);
    const QString digest = fingerprint(leafDer);
    QStringList warnings;
    if (settings_->value(prefix + QStringLiteral("fingerprint")).toString() == digest)
        warnings = settings_->value(prefix + QStringLiteral("warnings")).toStringList();
    if (!warnings.contains(QString::number(warningType))) warnings.append(QString::number(warningType));
    settings_->setValue(prefix + QStringLiteral("fingerprint"), digest);
    settings_->setValue(prefix + QStringLiteral("warnings"), warnings);
    settings_->sync();
}
