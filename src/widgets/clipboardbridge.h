#pragma once

#include <QObject>
#include <QString>

namespace QtSpice {
class ClipboardBridge final : public QObject {
    Q_OBJECT
public:
    explicit ClipboardBridge(QObject *parent = nullptr);
    void setActive(bool active);
    void applyGuestText(const QString &text);
Q_SIGNALS:
    void hostTextChanged(const QString &text);
private:
    bool active_ = false;
    bool applyingGuest_ = false;
    QString guestText_;
};
}
