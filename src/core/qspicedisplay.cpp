#include <QtSpice/QSpiceDisplay.h>
#include <Qt>
#include <algorithm>
#include <limits>

namespace QtSpice {
QImage copyPrimary(int format, int width, int height, int stride, const unsigned char *data) {
    if (!data || width <= 0 || height <= 0 || stride <= 0 || width > 16384 || height > 16384) return {};
    QImage::Format qtFormat;
    int bytes;
    switch (format) {
    case 32: qtFormat = QImage::Format_RGB32; bytes = 4; break;
    case 96: qtFormat = QImage::Format_ARGB32; bytes = 4; break;
    case 80: qtFormat = QImage::Format_RGB16; bytes = 2; break;
    default: return {};
    }
    if (stride < width * bytes || qsizetype(stride) * height > 512 * 1024 * 1024) return {};
    return QImage(data, width, height, stride, qtFormat).copy();
}

QRect fittedRect(QSize source, QSize target) {
    if (source.isEmpty() || target.isEmpty()) return {};
    QSize scaled = source.scaled(target, Qt::KeepAspectRatio);
    return QRect(QPoint((target.width() - scaled.width()) / 2, (target.height() - scaled.height()) / 2), scaled);
}

QPoint guestPoint(QPoint widgetPoint, QSize guestSize, QSize widgetSize) {
    QRect area = fittedRect(guestSize, widgetSize);
    if (area.isEmpty()) return {};
    int x = (widgetPoint.x() - area.x()) * guestSize.width() / area.width();
    int y = (widgetPoint.y() - area.y()) * guestSize.height() / area.height();
    return QPoint(std::clamp(x, 0, guestSize.width() - 1), std::clamp(y, 0, guestSize.height() - 1));
}

quint32 xtScanCode(int key) {
    switch (key) {
    case Qt::Key_Escape: return 0x01;
    case Qt::Key_Backspace: return 0x0e;
    case Qt::Key_Tab: return 0x0f;
    case Qt::Key_Return: case Qt::Key_Enter: return 0x1c;
    case Qt::Key_Control: return 0x1d;
    case Qt::Key_Shift: return 0x2a;
    case Qt::Key_Alt: return 0x38;
    case Qt::Key_Space: return 0x39;
    case Qt::Key_CapsLock: return 0x3a;
    case Qt::Key_F1: return 0x3b; case Qt::Key_F2: return 0x3c;
    case Qt::Key_F3: return 0x3d; case Qt::Key_F4: return 0x3e;
    case Qt::Key_F5: return 0x3f; case Qt::Key_F6: return 0x40;
    case Qt::Key_F7: return 0x41; case Qt::Key_F8: return 0x42;
    case Qt::Key_F9: return 0x43; case Qt::Key_F10: return 0x44;
    case Qt::Key_F11: return 0x57; case Qt::Key_F12: return 0x58;
    case Qt::Key_Up: return 0x148; case Qt::Key_Down: return 0x150;
    case Qt::Key_Left: return 0x14b; case Qt::Key_Right: return 0x14d;
    case Qt::Key_Delete: return 0x153;
    case Qt::Key_Minus: return 0x0c; case Qt::Key_Equal: return 0x0d;
    case Qt::Key_BracketLeft: return 0x1a; case Qt::Key_BracketRight: return 0x1b;
    case Qt::Key_Semicolon: return 0x27; case Qt::Key_Apostrophe: return 0x28;
    case Qt::Key_QuoteLeft: return 0x29; case Qt::Key_Backslash: return 0x2b;
    case Qt::Key_Comma: return 0x33; case Qt::Key_Period: return 0x34;
    case Qt::Key_Slash: return 0x35;
    default: break;
    }
    static constexpr quint8 letters[] = {0x1e,0x30,0x2e,0x20,0x12,0x21,0x22,0x23,0x17,0x24,0x25,0x26,0x32,0x31,0x18,0x19,0x10,0x13,0x1f,0x14,0x16,0x2f,0x11,0x2d,0x15,0x2c};
    static constexpr quint8 digits[] = {0x0b,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a};
    if (key >= Qt::Key_A && key <= Qt::Key_Z) return letters[key - Qt::Key_A];
    if (key >= Qt::Key_0 && key <= Qt::Key_9) return digits[key - Qt::Key_0];
    return 0;
}
}
