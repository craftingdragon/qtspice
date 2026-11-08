// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <QByteArray>
#include <QProcess>

// Keep explicit Chromium logging choices supplied by the caller. Other flags,
// including proxy configuration, are preserved verbatim.
inline QByteArray browserLoggingFlags(QByteArray flags) {
    const auto arguments = QProcess::splitCommand(QString::fromUtf8(flags));
    for (const auto &argument : arguments) {
        if (argument == QStringLiteral("--enable-logging") || argument.startsWith(QStringLiteral("--enable-logging=")) ||
            argument == QStringLiteral("--disable-logging") || argument.startsWith(QStringLiteral("--log-level=")) ||
            argument == QStringLiteral("--log-level") || argument.startsWith(QStringLiteral("--v=")) ||
            argument.startsWith(QStringLiteral("--vmodule="))) return flags;
    }
    // Chromium may still send ERROR messages to stderr with logging disabled;
    // retain fatal diagnostics, but suppress routine TLS handshake failures.
    if (!flags.isEmpty()) flags += ' ';
    flags += "--disable-logging --log-level=3";
    return flags;
}
inline void configureBrowserLogging() {
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", browserLoggingFlags(qgetenv("QTWEBENGINE_CHROMIUM_FLAGS")));
}
