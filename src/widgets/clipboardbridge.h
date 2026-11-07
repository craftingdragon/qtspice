#pragma once

#include <QObject>
#include <QThreadPool>
#include <QtSpice/QSpiceSession.h>

namespace QtSpice {
class ClipboardBridge final : public QObject {
    Q_OBJECT
public:
    explicit ClipboardBridge(QObject *parent = nullptr);
    ~ClipboardBridge() override;
    void setActive(bool active);
    void applyGuestContent(const ClipboardContent &content);
    void applyGuestText(const QString &text);
Q_SIGNALS:
    void hostContentChanged(const QtSpice::ClipboardContent &content);
    void hostTextChanged(const QString &text);
private:
    bool active_ = false;
    bool applyingGuest_ = false;
    ClipboardContent guest_;
    bool hasGuest_ = false;
    void readHost();
    void publishHost(const ClipboardContent &content);
    quint64 generation_ = 0;
    QThreadPool pool_;
};
}
