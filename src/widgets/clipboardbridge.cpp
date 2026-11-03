#include "clipboardbridge.h"
#include <QGuiApplication>
#include <QClipboard>

namespace QtSpice {
ClipboardBridge::ClipboardBridge(QObject *parent) : QObject(parent) {
    connect(QGuiApplication::clipboard(), &QClipboard::changed, this, [this](QClipboard::Mode mode) {
        if (mode != QClipboard::Clipboard || !active_ || applyingGuest_) return;
        const QString text = QGuiApplication::clipboard()->text(QClipboard::Clipboard);
        if (text != guestText_) {
            guestText_.clear();
            Q_EMIT hostTextChanged(text);
        }
    });
}
void ClipboardBridge::setActive(bool active) {
    if (active_ == active) return;
    active_ = active;
    guestText_.clear();
    if (active_) Q_EMIT hostTextChanged(QGuiApplication::clipboard()->text(QClipboard::Clipboard));
}
void ClipboardBridge::applyGuestText(const QString &text) {
    if (!active_) return;
    guestText_ = text;
    applyingGuest_ = true;
    QGuiApplication::clipboard()->setText(text, QClipboard::Clipboard);
    applyingGuest_ = false;
}
}
