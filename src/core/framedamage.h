#pragma once
#include <QImage>
#include <QRegion>
#include <cstring>
namespace QtSpice {
// Bound damage complexity before walking source rows.
inline QRegion boundedDamage(QRegion damage, QRect bounds) {
    damage &= bounds;
    if (damage.rectCount() > 64) damage = damage.boundingRect();
    qint64 area = 0;
    for (const auto &r : damage) area += qint64(r.width()) * r.height();
    if (!bounds.isEmpty() && area * 2 >= qint64(bounds.width()) * bounds.height()) return bounds;
    return damage;
}
inline quint64 applyFrameDamage(QImage &target, const QImage &source, const QRegion &damage) {
    if (source.isNull()) { target = {}; return 0; }
    if (target.size() != source.size() || target.format() != source.format()) {
        target = source.copy(); return target.sizeInBytes();
    }
    quint64 bytes = target.isDetached() ? 0 : target.sizeInBytes();
    const int pixelBytes = source.depth()/8;
    for (const auto &r : damage.intersected(source.rect())) {
        for (int y = r.top(); y <= r.bottom(); ++y)
            std::memcpy(target.scanLine(y) + r.x()*pixelBytes,
                        source.constScanLine(y) + r.x()*pixelBytes, r.width()*pixelBytes);
        bytes += quint64(r.width()) * r.height() * pixelBytes;
    }
    return bytes;
}
}
