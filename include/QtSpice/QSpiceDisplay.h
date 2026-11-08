// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <QImage>
#include <QPoint>
#include <QRect>

namespace QtSpice {
// SPICE primary formats: 32 xRGB (32), 32 ARGB (96), 16 RGB565 (80).
QImage copyPrimary(int format, int width, int height, int stride, const unsigned char *data);
QRect fittedRect(QSize source, QSize target);
QPoint guestPoint(QPoint widgetPoint, QSize guestSize, QSize widgetSize);
quint32 xtScanCode(int qtKey);
quint32 nativeXtScanCode(quint32 nativeScanCode, int qtKey);
}
