#pragma once
#include <QtSpice/QSpiceSession.h>
#include <QBuffer>
#include <QImageReader>
#include <QImageWriter>
#include <QPainter>
#include <QLoggingCategory>
#include <spice/vd_agent.h>

namespace QtSpice {
inline const QLoggingCategory &clipboardLog() { static const QLoggingCategory category("qtspice.clipboard", QtWarningMsg); return category; }
inline constexpr qsizetype clipboardImageLimit = 64 * 1024 * 1024;
inline constexpr qint64 clipboardPixelLimit = 256LL * 1024 * 1024 / 4;
inline QByteArray clipboardFormat(unsigned type) {
    switch (type) {
    case VD_AGENT_CLIPBOARD_IMAGE_PNG: return "png";
    case VD_AGENT_CLIPBOARD_IMAGE_BMP: return "bmp";
    case VD_AGENT_CLIPBOARD_IMAGE_TIFF: return "tiff";
    case VD_AGENT_CLIPBOARD_IMAGE_JPG: return "jpeg";
    default: return {};
    }
}
inline bool clipboardImageSize(QSize size) {
    return size.width() > 0 && size.height() > 0 && qint64(size.width()) * size.height() <= clipboardPixelLimit;
}
inline QImage decodeClipboardImage(QByteArray bytes, QByteArray format) {
    if (bytes.isEmpty() || format.isEmpty()) return {};
    if (bytes.size() > clipboardImageLimit) {
        qCWarning(clipboardLog()) << "Rejected encoded clipboard image size" << bytes.size(); return {};
    }
    QBuffer buffer(&bytes); buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, format);
    reader.setAutoDetectImageFormat(false);
    const QSize dimensions = reader.size();
    if (!dimensions.isValid() || dimensions.isEmpty()) {
        qCDebug(clipboardLog()) << "Unreadable clipboard image header" << format << "bytes" << bytes.size() << "decoder error" << reader.errorString();
        return {};
    }
    if (!clipboardImageSize(dimensions)) {
        qCWarning(clipboardLog()) << "Rejected clipboard image dimensions" << dimensions; return {};
    }
    const auto depth = QImage::toPixelFormat(reader.imageFormat()).bitsPerPixel();
    if (depth && qint64(reader.size().width()) * reader.size().height() > (256LL * 1024 * 1024 * 8) / depth) {
        qCWarning(clipboardLog()) << "Rejected decoded clipboard image size"; return {};
    }
    QImage image = reader.read();
    if (image.isNull()) qCDebug(clipboardLog()) << "Clipboard image representation decoding failed" << format << "bytes" << bytes.size() << "decoder error" << reader.errorString();
    if (!clipboardImageSize(image.size()) || image.sizeInBytes() > 256LL * 1024 * 1024) return {};
    return image;
}
inline QByteArray encodeClipboardImage(QImage image, unsigned type) {
    if (!clipboardImageSize(image.size()) || image.sizeInBytes() > 256LL * 1024 * 1024) return {};
    const auto format = clipboardFormat(type);
    if (format.isEmpty()) return {};
    if (type == VD_AGENT_CLIPBOARD_IMAGE_JPG && image.hasAlphaChannel()) {
        QImage opaque(image.size(), QImage::Format_RGB32); opaque.fill(Qt::white);
        QPainter painter(&opaque); painter.drawImage(0, 0, image); painter.end(); image = opaque;
    }
    // A seekable bounded device is needed by TIFF and BMP writers.
    class BoundedBuffer final : public QBuffer {
    public:
        bool failed = false;
    protected:
        qint64 writeData(const char *data, qint64 size) override {
            if (failed || size < 0 || pos() > clipboardImageLimit - size) { failed = true; return -1; }
            return QBuffer::writeData(data, size);
        }
    } buffer;
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, format);
    if (!writer.write(image) || buffer.failed) { qCWarning(clipboardLog()) << "Clipboard image encoding failed or exceeded size limit"; return {}; }
    return buffer.data();
}
}
