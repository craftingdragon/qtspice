// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <QByteArray>
#include <QSettings>
#include <QUrl>
#include <memory>

// An acknowledgment applies to one origin, one exact leaf certificate, and one warning type.
class CertificateStore {
public:
    explicit CertificateStore(const QString &settingsFile = {});
    bool isTrusted(const QUrl &url, const QByteArray &leafDer, int warningType) const;
    void remember(const QUrl &url, const QByteArray &leafDer, int warningType);
    static QString fingerprint(const QByteArray &leafDer);
private:
    static QString originKey(const QUrl &url);
    std::unique_ptr<QSettings> settings_;
};
