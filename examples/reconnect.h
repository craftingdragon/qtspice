#pragma once
#include "compressionmenu.h"
#include <QtSpice/QSpiceSession.h>
#include <QMainWindow>
#include <QEvent>
#include <QMenuBar>
#include <QPushButton>
#include <QVBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QPointer>
#include <functional>

// Credential acquisition belongs to the viewer, not the SPICE library.
class Reconnect final : public QObject {
public:
    using Result = std::function<void(std::optional<QtSpice::ConnectionConfig>, QString)>;
    using Source = std::function<void(Result)>;
    Reconnect(QMainWindow *window, QtSpice::Session *session) : QObject(window), session_(session) {
        timer_.setSingleShot(true); timer_.setInterval(30000);
        connect(&timer_, &QTimer::timeout, this, [this] { cancel(QStringLiteral("Connection attempt timed out")); });
        connect(session, &QtSpice::Session::stateChanged, this, [this](auto state) {
            if (state == QtSpice::Session::State::Connected) { busy_ = false; timer_.stop(); message_ = QStringLiteral("Disconnected"); }
            if (state == QtSpice::Session::State::Disconnected && busy_) { busy_ = false; timer_.stop(); ++serial_; }
            refresh();
        });
        connect(session, &QtSpice::Session::errorOccurred, this, [this](const QString &error) { if (busy_ && session_->state() == QtSpice::Session::State::Disconnected) { busy_ = false; timer_.stop(); ++serial_; } message_ = error; refresh(); });
        connect(session, &QtSpice::Session::sharingChanged, this, [this] { if (session_->state() == QtSpice::Session::State::Connected) sharing_ = session_->sharedDirectory(); });
        attach(window);
    }
    Source source;
    void attach(QMainWindow *window) {
        auto *menu = CompressionMenu::connectionMenu(window->menuBar());
        auto *retry = menu->addAction(QStringLiteral("Reconnect"));
        auto *cancelAction = menu->addAction(QStringLiteral("Cancel Reconnect"));
        auto *panel = new QWidget(window->centralWidget()); panel->setObjectName(QStringLiteral("reconnectPanel"));
        panel->setAutoFillBackground(true);
        auto *layout = new QVBoxLayout(panel); auto *label = new QLabel(panel); label->setWordWrap(true);
        auto *button = new QPushButton(QStringLiteral("Reconnect"), panel);
        layout->addWidget(label); layout->addWidget(button);
        entries_.append({panel, label, button, retry, cancelAction});
        connect(retry, &QAction::triggered, this, [this] { start(); });
        connect(button, &QPushButton::clicked, this, [this] { start(); });
        connect(cancelAction, &QAction::triggered, this, [this] { cancel(); });
        window->centralWidget()->installEventFilter(this); refresh();
    }
    void start() {
        if (busy_ || session_->state() != QtSpice::Session::State::Disconnected) return;
        busy_ = true; message_ = QStringLiteral("Obtaining connection settings…"); timer_.start(); refresh();
        const auto serial = ++serial_; QPointer<Reconnect> guard(this);
        if (!source) { finish({}, QStringLiteral("Fresh-ticket reconnect is unavailable for this console"), serial); return; }
        source([guard, serial](auto config, QString error) { if (guard) guard->finish(config, error, serial); });
    }
    void cancel(QString reason = QStringLiteral("Connection attempt cancelled")) {
        ++serial_; busy_ = false; timer_.stop(); session_->disconnectFromHost(); message_ = reason; refresh();
    }
private:
    struct Entry { QPointer<QWidget> panel; QPointer<QLabel> label; QPointer<QPushButton> button; QPointer<QAction> retry, cancel; };
    QList<Entry> entries_; QtSpice::Session *session_; QTimer timer_; bool busy_ = false; quint64 serial_ = 0;
    QString message_ = QStringLiteral("Disconnected");
    bool eventFilter(QObject *, QEvent *event) override { if (event->type() == QEvent::Resize) refresh(); return false; }
    void finish(std::optional<QtSpice::ConnectionConfig> config, QString error, quint64 serial) {
        if (serial != serial_ || !busy_) return;
        if (!config) { busy_ = false; timer_.stop(); if (busy_ && session_->state() == QtSpice::Session::State::Disconnected) { busy_ = false; timer_.stop(); ++serial_; } message_ = error; refresh(); return; }
        ++serial_; // Consume this credential response exactly once.
        // User changes made after initial connection supersede downloaded sharing settings.
        config->sharedDirectory = sharing_;
        message_ = QStringLiteral("Connecting…"); session_->connectToConfig(*config); refresh();
    }
public:
    std::optional<QtSpice::SharedDirectory> sharing_;
private:
    void refresh() {
        if (session_->state() == QtSpice::Session::State::Connected) sharing_ = session_->sharedDirectory();
        for (auto &e : entries_) if (e.panel) {
            const bool disconnected = session_->state() == QtSpice::Session::State::Disconnected;
            e.retry->setEnabled(disconnected && !busy_); e.cancel->setEnabled(busy_);
            e.button->setEnabled(disconnected && !busy_); e.label->setText(message_);
            e.panel->setVisible(disconnected || busy_);
            e.panel->resize(qMin(440, e.panel->parentWidget()->width()), e.panel->sizeHint().height());
            e.panel->move((e.panel->parentWidget()->width()-e.panel->width())/2, qMax(0,(e.panel->parentWidget()->height()-e.panel->height())/2)); e.panel->raise();
        }
    }
};
