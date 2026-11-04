#pragma once
#include <QNetworkProxyFactory>
#include <QUrl>

// Configure before creating the first WebEngine profile. Qt's application-proxy
// path bypasses simple hostnames; Chromium's explicit configuration avoids that.
inline void configureBrowserProxy(const QUrl &bridge) {
    QByteArray flags = qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
    flags += " --proxy-server=" + bridge.toEncoded() + " --proxy-bypass-list=<-loopback>";
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", flags);
    QNetworkProxyFactory::setUseSystemConfiguration(true);
}
