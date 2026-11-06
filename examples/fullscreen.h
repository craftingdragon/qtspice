#pragma once
#include <QtSpice/QSpiceWidget.h>
#include <QAction>
#include <QEvent>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QPainter>
#include "viewersettings.h"
#include <QSignalBlocker>
#include <QStatusBar>

// Some desktop styles leave menu bars transparent. Always paint a solid
// palette background before the style paints menu items over the guest image.
class FullscreenMenuBar final : public QMenuBar {
public:
    using QMenuBar::QMenuBar;
protected:
    void paintEvent(QPaintEvent *event) override {
        {
            QPainter painter(this);
            QColor background = palette().color(QPalette::Window);
            background.setAlpha(255);
            painter.fillRect(rect(), background);
        }
        QMenuBar::paintEvent(event);
    }
};

// Viewer policy belongs here, rather than reserving a guest key in Widget.
class Fullscreen final : public QObject {
public:
    Fullscreen(QMainWindow *window, QMenu *view, QtSpice::Widget *console)
        : QObject(window), window_(window), console_(console), menu_(window->menuBar()), status_(window->statusBar()) {
        action_ = view->addAction(QStringLiteral("Fullscreen"));
        action_->setCheckable(true);
        action_->setShortcut(QKeySequence(Qt::Key_F11));
        window_->addAction(action_);
        connect(action_, &QAction::toggled, this, [this](bool enabled) { setFullscreen(enabled); });
        exitAction_ = view->addAction(QStringLiteral("Exit Fullscreen"));
        exitAction_->setObjectName(QStringLiteral("exitFullscreenAction"));
        // Display the shortcut without registering it with Qt's shortcut map:
        // the application filter must also handle it during keyboard capture.
        exitAction_->setText(QStringLiteral("Exit Fullscreen\tCtrl+Alt+Enter"));
        exitAction_->setEnabled(false);
        connect(exitAction_, &QAction::triggered, this, [this] { setFullscreen(false); });
        edgeAction_ = view->addAction(QStringLiteral("Show Menu at Top Edge"));
        edgeAction_->setObjectName(QStringLiteral("fullscreenEdgeMenuAction"));
        edgeAction_->setCheckable(true);
        edgeAction_->setChecked(ViewerSettings::fullscreenEdgeMenu());
        overlay_ = new FullscreenMenuBar(window_);
        overlay_->setNativeMenuBar(false);
        overlay_->setObjectName(QStringLiteral("fullscreenEdgeMenu"));
        overlay_->setMouseTracking(true);
        overlay_->hide();
        animation_ = new QPropertyAnimation(overlay_, "pos", this);
        animation_->setDuration(150);
        hideTimer_.setSingleShot(true);
        hideTimer_.setInterval(300);
        connect(&hideTimer_, &QTimer::timeout, this, [this] {
            if (!QApplication::activePopupWidget()) hideMenu();
        });
        connect(edgeAction_, &QAction::toggled, this, [this](bool enabled) {
            ViewerSettings::setFullscreenEdgeMenu(enabled);
            if (!enabled) hideMenu();
        });
        qApp->installEventFilter(this);
    }

    void setFullscreen(bool enabled) {
        if (enabled == window_->isFullScreen()) return;
        // A resize can invalidate pointer constraints. Leave capture suspended
        // until a deliberate console click, preserving the user's preference.
        console_->releaseInputCapture();
        if (enabled) {
            geometry_ = window_->saveGeometry();
            state_ = window_->windowState();
            menuVisible_ = !menu_->isHidden();
            statusVisible_ = !status_->isHidden();
            menu_->hide();
            status_->hide();
            window_->showFullScreen();
        } else {
            window_->setWindowState(state_ & ~Qt::WindowFullScreen & ~Qt::WindowMinimized);
            window_->restoreGeometry(geometry_);
            restoreBars();
        }
        refresh();
    }

protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        if (object == window_ && (event->type() == QEvent::Resize || event->type() == QEvent::WindowStateChange)) {
            // Also keep the action and chrome correct when the WM leaves fullscreen.
            if (event->type() == QEvent::WindowStateChange && !window_->isFullScreen() && action_->isChecked()) {
                console_->releaseInputCapture();
                restoreBars();
            }
            refresh();
        }
        auto *target = qobject_cast<QWidget *>(object);
        const bool local = target && target->window() == window_;
        if (local && (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)) {
            auto *key = static_cast<QKeyEvent *>(event);
            const bool enter = key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter;
            const auto mods = key->modifiers() & ~Qt::KeypadModifier;
            const bool chord = enter && mods == (Qt::ControlModifier | Qt::AltModifier);
            if ((window_->isFullScreen() && chord) || (enter && exitKeyDown_)) {
                event->accept();
                if (event->type() == QEvent::KeyPress && !key->isAutoRepeat()) {
                    exitKeyDown_ = true;
                    setFullscreen(false);
                }
                if (event->type() == QEvent::KeyRelease && !key->isAutoRepeat()) exitKeyDown_ = false;
                return true;
            }
        }
        if (window_->isFullScreen() && edgeAction_->isChecked()) {
            if (local && event->type() == QEvent::MouseMove) {
                const auto pos = window_->mapFromGlobal(static_cast<QMouseEvent *>(event)->globalPosition().toPoint());
                if (pos.y() >= 0 && pos.y() < 3) revealMenu();
                if (overlay_->isVisible()) {
                    if (pos.y() <= overlay_->height()) hideTimer_.stop();
                    else hideTimer_.start();
                }
            }
            if ((object == overlay_ && event->type() == QEvent::Leave) ||
                (qobject_cast<QMenu *>(object) && menu_->actions().contains(static_cast<QMenu *>(object)->menuAction()) && event->type() == QEvent::Hide))
                hideTimer_.start();
        }
        return QObject::eventFilter(object, event);
    }

private:
    void restoreBars() {
        menu_->setVisible(menuVisible_);
        status_->setVisible(statusVisible_);
    }
    void refresh() {
        const QSignalBlocker blocker(action_);
        action_->setChecked(window_->isFullScreen());
        exitAction_->setEnabled(window_->isFullScreen());
        if (!window_->isFullScreen()) hideMenu();
        else if (overlay_->isVisible()) overlay_->resize(window_->width(), overlay_->sizeHint().height());
    }
    void hideMenu() {
        hideTimer_.stop();
        animation_->stop();
        overlay_->hide();
    }
    void revealMenu() {
        hideTimer_.stop();
        if (overlay_->isVisible()) return;
        console_->releaseInputCapture();
        overlay_->clear();
        for (auto *action : menu_->actions()) overlay_->addAction(action);
        overlay_->resize(window_->width(), overlay_->sizeHint().height());
        overlay_->move(0, -overlay_->height());
        overlay_->show();
        overlay_->raise();
        animation_->setStartValue(QPoint(0, -overlay_->height()));
        animation_->setEndValue(QPoint(0, 0));
        animation_->start();
    }
    QMainWindow *window_;
    QtSpice::Widget *console_;
    QMenuBar *menu_;
    QStatusBar *status_;
    QAction *action_;
    QAction *exitAction_;
    QAction *edgeAction_;
    QMenuBar *overlay_;
    QPropertyAnimation *animation_;
    QTimer hideTimer_;
    bool exitKeyDown_ = false;
    QByteArray geometry_;
    Qt::WindowStates state_;
    bool menuVisible_ = true;
    bool statusVisible_ = true;
};
