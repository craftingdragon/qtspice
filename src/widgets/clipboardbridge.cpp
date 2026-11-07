#include "clipboardbridge.h"
#include "../core/clipboardcontent.h"
#include <QGuiApplication>
#include <QClipboard>
#include <QMimeData>
#include <QPointer>

namespace QtSpice {
ClipboardBridge::ClipboardBridge(QObject *parent) : QObject(parent) {
    pool_.setMaxThreadCount(1);
    connect(QGuiApplication::clipboard(), &QClipboard::changed, this, [this](QClipboard::Mode mode) {
        if (mode == QClipboard::Clipboard && active_ && !applyingGuest_) readHost();
    });
}
ClipboardBridge::~ClipboardBridge() { pool_.clear(); pool_.waitForDone(); }
void ClipboardBridge::readHost() {
    const auto serial = ++generation_; pool_.clear();
    const auto *mime = QGuiApplication::clipboard()->mimeData();
    ClipboardContent content;
    if (mime) {
        if (mime->hasText()) content.text = mime->text();
        if (mime->hasImage()) content.image = qvariant_cast<QImage>(mime->imageData());
        if (content.image.isNull()) {
            QList<QPair<QByteArray, QByteArray>> encoded;
            auto formats = mime->formats();
            formats.removeAll("image/png"); formats.prepend("image/png");
            for (const auto &format : formats) {
                if (!format.startsWith("image/") || !mime->hasFormat(format)) continue;
                const auto codecs = QImageReader::imageFormatsForMimeType(format.toLatin1());
                if (codecs.isEmpty()) continue;
                auto bytes = mime->data(format);
                if (!bytes.isEmpty() && bytes.size() <= clipboardImageLimit) encoded.append({bytes, codecs.first()});
                if (encoded.size() == 4) break;
            }
            if (!encoded.isEmpty()) {
                QPointer<ClipboardBridge> guard(this);
                pool_.start([guard, serial, content, encoded]() mutable {
                    for (const auto &item : encoded) {
                        content.image = decodeClipboardImage(item.first, item.second);
                        if (!content.image.isNull()) break;
                    }
                    if (guard) QMetaObject::invokeMethod(guard, [guard, serial, content] {
                        if (guard && guard->active_ && guard->generation_ == serial) guard->publishHost(content);
                    }, Qt::QueuedConnection);
                });
                return;
            }
        }
    }
    publishHost(content);
}
void ClipboardBridge::publishHost(const ClipboardContent &content) {
    if (hasGuest_ && content.text == guest_.text && content.image == guest_.image) return;
    guest_ = {}; hasGuest_ = false;
    Q_EMIT hostContentChanged(content);
    Q_EMIT hostTextChanged(content.text.value_or(QString()));
}
void ClipboardBridge::setActive(bool active) {
    if (active_ == active) return;
    ++generation_; pool_.clear();
    active_ = active; guest_ = {}; hasGuest_ = false;
    if (active_) readHost();
}
void ClipboardBridge::applyGuestContent(const ClipboardContent &content) {
    if (!active_) return;
    ++generation_; pool_.clear();
    guest_ = content; hasGuest_ = true;
    auto *mime = new QMimeData;
    if (content.text) mime->setText(*content.text);
    if (!content.image.isNull()) mime->setImageData(content.image);
    applyingGuest_ = true;
    QGuiApplication::clipboard()->setMimeData(mime);
    applyingGuest_ = false;
}
void ClipboardBridge::applyGuestText(const QString &text) { applyGuestContent({text, {}}); }
}
