#pragma once
#include <QRectF>
#include <QPoint>
#include <algorithm>
#include <cmath>
namespace QtSpice {
inline QRectF displayRect(QSize frame, QSize viewport, qreal dpr, bool native, QPoint offset = {}) {
    if (frame.isEmpty() || viewport.isEmpty()) return {};
    QSize physical(qFloor(viewport.width() * dpr), qFloor(viewport.height() * dpr));
    QSize target = native ? frame : frame.scaled(physical, Qt::KeepAspectRatio);
    QPoint origin(native ? std::max(0, (physical.width() - target.width()) / 2) - offset.x()
                         : (physical.width() - target.width()) / 2,
                  native ? std::max(0, (physical.height() - target.height()) / 2) - offset.y()
                         : (physical.height() - target.height()) / 2);
    return QRectF(origin.x()/dpr, origin.y()/dpr, target.width()/dpr, target.height()/dpr);
}
inline QPoint displayPoint(QPointF point, QSize frame, QRectF area) {
    if (frame.isEmpty() || area.isEmpty()) return {};
    return {std::clamp(int(std::floor((point.x()-area.x())*frame.width()/area.width())), 0, frame.width()-1),
            std::clamp(int(std::floor((point.y()-area.y())*frame.height()/area.height())), 0, frame.height()-1)};
}
}
