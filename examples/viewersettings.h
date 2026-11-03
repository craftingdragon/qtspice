#pragma once
#include <QSettings>

namespace ViewerSettings {
inline bool autoResizeGuest() {
    return QSettings().value(QStringLiteral("display/autoResizeGuest"), true).toBool();
}
inline void setAutoResizeGuest(bool enabled) {
    QSettings().setValue(QStringLiteral("display/autoResizeGuest"), enabled);
}
}
