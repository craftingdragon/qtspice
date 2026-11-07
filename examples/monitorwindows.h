#pragma once
#include <QtSpice/QSpiceSession.h>
#include <QtSpice/QSpiceWidget.h>
#include "fullscreen.h"
#include "inputmenu.h"
#include "scalingmenu.h"
#include "audiomenu.h"
#include "sharedfolder.h"
#include <QMainWindow>
#include <QCloseEvent>
#include <QFileDialog>
#include <QPointer>
#include <QStatusBar>

// One controller owns the auxiliary windows; the primary window owns the session.
class MonitorWindows final : public QObject {
public:
    MonitorWindows(QMainWindow *primary, QtSpice::Session *session, QMenu *view)
        : QObject(primary), primary_(primary), session_(session) {
        primary->installEventFilter(this);
        addMenu(view);
        connect(session, &QtSpice::Session::monitorsChanged, this, [this] { refresh(); });
        connect(session, &QtSpice::Session::monitorConfigurationAvailableChanged, this, [this] { refreshMenus(); });
        connect(session, &QtSpice::Session::monitorConfigurationStatusChanged, this, [this](const QString &status) { primary_->statusBar()->showMessage(status); for (auto w : windows_) if (w) w->statusBar()->showMessage(status); });
        refresh();
    }
    ~MonitorWindows() override { closing_ = true; for (auto window : windows_) if (window) delete window; }
private:
    QMainWindow *primary_;
    QtSpice::Session *session_;
    QMap<QtSpice::MonitorId, QPointer<QMainWindow>> windows_;
    QList<QPointer<QMenu>> menus_;
    QSet<QtSpice::MonitorId> pending_;
    bool closing_ = false;
    QSet<QtSpice::MonitorId> disabling_;
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() == QEvent::Close && !closing_) {
            if (object == primary_) {
                closing_ = true;
                for (auto window : windows_) if (window) window->hide();
                session_->disconnectFromHost();
            } else {
                for (auto it = windows_.begin(); it != windows_.end(); ++it) if (it.value() == object) {
                    disabling_.insert(it.key());
                    session_->setMonitorEnabled(it.key(), false);
                    pending_.remove(it.key());
                    break;
                }
            }
        }
        return QObject::eventFilter(object, event);
    }
    void addMenu(QMenu *view) { menus_.append(view->addMenu(QStringLiteral("Monitors"))); refreshMenus(); }
    void refreshMenus() {
        for (auto menu : menus_) {
            if (!menu) continue;
            menu->clear(); int number = 0;
            for (const auto &m : session_->monitors()) {
                auto *action = menu->addAction(QStringLiteral("Monitor %1").arg(++number));
                action->setCheckable(true); action->setChecked((m.enabled && !disabling_.contains(m.id)) || pending_.contains(m.id));
                action->setEnabled(m.id != QtSpice::MonitorId{} && session_->monitorConfigurationAvailable());
                if (!session_->monitorConfigurationAvailable()) action->setToolTip(QStringLiteral("Guest agent unavailable"));
                connect(action, &QAction::triggered, this, [this, id = m.id](bool enabled) {
                    if (enabled) { disabling_.remove(id); pending_.insert(id); open(id); }
                    else { disabling_.insert(id); pending_.remove(id); if (windows_.value(id)) windows_.value(id)->hide(); }
                    session_->setMonitorEnabled(id, enabled);
                });
            }
        }
    }
    void open(QtSpice::MonitorId id) {
        if (id == QtSpice::MonitorId{}) return;
        if (auto existing = windows_.value(id)) { existing->show(); return; }
        auto *window = new QMainWindow;
        windows_[id] = window;
        window->installEventFilter(this);
        window->setWindowTitle(QStringLiteral("QtSpice · Monitor %1:%2").arg(id.channelId).arg(id.monitorId));
        auto *widget = new QtSpice::Widget(window);
        widget->setMonitor(id); widget->setSession(session_); window->setCentralWidget(widget);
        AudioMenu::add(window->menuBar(), session_); SharedFolder::addMenu(window->menuBar(), session_);
        auto *view = window->menuBar()->addMenu(QStringLiteral("View"));
        addScalingMenu(view, widget); new Fullscreen(window, view, widget);
        auto *snapshot = view->addAction(QStringLiteral("Save Next Frame…"));
        connect(snapshot, &QAction::triggered, window, [this, window, snapshot, id] {
            const QString path = QFileDialog::getSaveFileName(window, QStringLiteral("Save next guest frame"), QStringLiteral("frame.png"), QStringLiteral("PNG image (*.png)"));
            if (path.isEmpty()) return;
            snapshot->setEnabled(false);
            auto connection = std::make_shared<QMetaObject::Connection>();
            *connection = connect(session_, &QtSpice::Session::monitorFrameReady, window, [window, snapshot, id, path, connection](QtSpice::MonitorId source, const QImage &image) {
                if (source != id || image.isNull()) return;
                QObject::disconnect(*connection); snapshot->setEnabled(true);
                window->statusBar()->showMessage(image.save(path) ? QStringLiteral("Frame saved") : QStringLiteral("Could not save frame"));
            });
        });
        addInputMenu(window->menuBar(), window->statusBar(), widget, session_);
        auto *resize = view->addAction(QStringLiteral("Auto Resize Guest"));
        resize->setCheckable(true); resize->setChecked(ViewerSettings::autoResizeGuest());
        widget->setAutoResizeGuest(resize->isChecked());
        connect(resize, &QAction::toggled, widget, &QtSpice::Widget::setAutoResizeGuest);
        addMenu(view); window->resize(1024, 768); window->show();
    }
    void refresh() {
        if (closing_) return;
        for (const auto &m : session_->monitors()) if (m.enabled && !disabling_.contains(m.id)) { pending_.remove(m.id); open(m.id); }
        for (auto it = windows_.begin(); it != windows_.end(); ++it) {
            bool active = pending_.contains(it.key());
            for (const auto &m : session_->monitors()) if (m.id == it.key() && m.enabled && !disabling_.contains(m.id)) active = true;
            if (!active && it.value()) it.value()->hide();
        }
        refreshMenus();
    }
};
