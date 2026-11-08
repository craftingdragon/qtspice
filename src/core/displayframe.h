// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <QtSpice/QSpiceDisplay.h>
#include <spice-client.h>
#include "framedamage.h"

namespace QtSpice {
inline QImage copyMonitorRect(const SpiceDisplayPrimary &p, QRect r) {
    if (r.isEmpty() || r.x() < 0 || r.y() < 0 || qint64(r.x()) + r.width() > p.width || qint64(r.y()) + r.height() > p.height) return {};
    int bytes = p.format == 80 ? 2 : 4;
    if (!p.data || p.stride <= 0 || p.width <= 0 || p.height <= 0 || p.width > 16384 || p.height > 16384 || p.stride < qint64(p.width)*bytes || qint64(p.stride)*p.height > 512*1024*1024) return {};
    return copyPrimary(p.format, r.width(), r.height(), p.stride, p.data + qint64(r.y())*p.stride + qint64(r.x())*bytes);
}
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
