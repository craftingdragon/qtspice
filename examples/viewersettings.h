#pragma once
#include <QSettings>

namespace ViewerSettings {
inline bool fullscreenEdgeMenu() {
    return QSettings().value(QStringLiteral("display/fullscreenEdgeMenu"), false).toBool();
}
inline void setFullscreenEdgeMenu(bool enabled) {
    QSettings().setValue(QStringLiteral("display/fullscreenEdgeMenu"), enabled);
}
inline int scalingMode() {
    bool ok = false;
    const int value = QSettings().value(QStringLiteral("display/scalingMode"), 0).toInt(&ok);
    return ok && value >= 0 && value <= 2 ? value : 0;
}
inline void setScalingMode(int mode) {
    QSettings().setValue(QStringLiteral("display/scalingMode"), mode);
}
inline bool autoResizeGuest() {
    return QSettings().value(QStringLiteral("display/autoResizeGuest"), true).toBool();
}
inline void setAutoResizeGuest(bool enabled) {
    QSettings().setValue(QStringLiteral("display/autoResizeGuest"), enabled);
}
}
