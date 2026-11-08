#pragma once
#include <QLoggingCategory>
#include <glib.h>
#include <cerrno>
#include <cstring>

// Private native logging adapter. Only the exact transient TLS backpressure
// warning is routed to Qt debug logging; other native diagnostics are preserved.
namespace SpiceLogging {
inline bool isBackpressureWarning(const gchar *domain, GLogLevelFlags level, const gchar *message) {
    if (!domain || std::strcmp(domain, "GSpice") != 0 || !message ||
        level != G_LOG_LEVEL_WARNING) return false;
    return std::strcmp(message, g_strerror(EAGAIN)) == 0 ||
           std::strcmp(message, g_strerror(EWOULDBLOCK)) == 0;
}
inline const QLoggingCategory &transportCategory() {
    static const QLoggingCategory category("qtspice.transport", QtWarningMsg);
    return category;
}
inline void handle(const gchar *domain, GLogLevelFlags level, const gchar *message, gpointer) {
    if (isBackpressureWarning(domain, level, message)) {
        qCDebug(transportCategory).noquote() << "SPICE TLS write waiting for socket buffer space:" << message;
        return;
    }
    g_log_default_handler(domain, level, message, nullptr);
}
inline void configure() {
    static const guint handler = g_log_set_handler("GSpice", G_LOG_LEVEL_WARNING, handle, nullptr);
    Q_UNUSED(handler);
}
}
