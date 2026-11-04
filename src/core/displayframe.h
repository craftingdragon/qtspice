#pragma once
#include <QtSpice/QSpiceDisplay.h>
#include <spice-client.h>

namespace QtSpice {
// Monitor coordinates describe the visible area inside the backing surface.
// Reject transient configurations outside that surface instead of padding with
// uninitialized pixels or exposing the previous desktop around the edges.
inline QImage copyMonitor(const SpiceDisplayPrimary &primary, const GArray *monitors) {
    QImage image = copyPrimary(primary.format, primary.width, primary.height, primary.stride, primary.data);
    if (image.isNull() || !monitors || !monitors->len) return image;
    for (guint i = 0; i < monitors->len; ++i) {
        const auto &monitor = g_array_index(monitors, SpiceDisplayMonitorConfig, i);
        if (monitor.id != 0) continue;
        if (monitor.surface_id != 0 || !monitor.width || !monitor.height ||
            quint64(monitor.x) + monitor.width > quint64(primary.width) ||
            quint64(monitor.y) + monitor.height > quint64(primary.height)) return {};
        return image.copy(int(monitor.x), int(monitor.y), int(monitor.width), int(monitor.height));
    }
    return {}; // Monitor zero is disabled; do not show another monitor's pixels.
}
}
