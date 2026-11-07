#pragma once
#include <QtSpice/QSpiceSession.h>
#include <QtSpice/QSpiceWidget.h>
#include "fullscreen.h"
#include "reconnect.h"
#include "inputmenu.h"
#include "scalingmenu.h"
#include "audiomenu.h"
#include "sharedfolder.h"
#include "usbdevices.h"
#include "filesending.h"
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
        UsbDevices::add(primary, session);
        sending_ = new FileSending(primary, session); sending_->add(primary);
        addMenu(view);
        connect(session, &QtSpice::Session::monitorsChanged, this, [this] { refresh(); });
        connect(session, &QtSpice::Session::monitorConfigurationAvailableChanged, this, [this] { refreshMenus(); });
        connect(session, &QtSpice::Session::monitorConfigurationStatusChanged, this, [this](const QString &status) { primary_->statusBar()->showMessage(status); for (auto w : windows_) if (w) w->statusBar()->showMessage(status); });
        connect(session, &QtSpice::Session::monitorLayoutRequested, this, [this](const auto &layout) { if (!restoring_) { requested_ = layout; for (auto &head : saved_) for (const auto &requested : layout) if (head.id == requested.id) head = requested; } });
        connect(session, &QtSpice::Session::stateChanged, this, [this](auto state) {
            if (state == QtSpice::Session::State::Disconnected) { recovering_ = !saved_.isEmpty(); restored_ = false; }
        });
        connect(session, &QtSpice::Session::monitorConfigurationAvailableChanged, this, [this](bool) { restore(); });
        refresh();
    }
    ~MonitorWindows() override { closing_ = true; for (auto window : windows_) if (window) delete window; }
private:
    FileSending *sending_;
    QMainWindow *primary_;
    QtSpice::Session *session_;
    QMap<QtSpice::MonitorId, QPointer<QMainWindow>> windows_;
    QList<QPointer<QMenu>> menus_;
    QSet<QtSpice::MonitorId> pending_;
    bool closing_ = false;
    bool recovering_ = false, restored_ = false, restoring_ = false;
    QList<QtSpice::MonitorInfo> saved_, requested_;
    QSet<QtSpice::MonitorId> disabling_;
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() == QEvent::Close && !closing_) {
            if (object == primary_) {
                closing_ = true;
                if (auto *dialog = qobject_cast<QDialog *>(session_->property("usbDialog").value<QObject *>())) dialog->close();
                for (auto window : windows_) if (window) window->hide();
                session_->disconnectFromHost();
            } else {
                for (auto it = windows_.begin(); it != windows_.end(); ++it) if (it.value() == object) {
                    for (auto &head : saved_) if (head.id == it.key()) head.enabled = false;
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
        UsbDevices::add(window, session_);
        CompressionMenu::add(window->menuBar(), session_);
        AudioMenu::add(window->menuBar(), session_); SharedFolder::addMenu(window->menuBar(), session_);
        sending_->add(window);
        auto *view = window->menuBar()->addMenu(QStringLiteral("View"));
        addScalingMenu(view, widget); new Fullscreen(window, view, widget);
        auto *snapshot = view->addAction(QStringLiteral("Save Next Frame…"));
        connect(snapshot, &QAction::triggered, window, [this, window, snapshot, id] {
            const QString path = QFileDialog::getSaveFileName(window, QStringLiteral("Save next guest frame"), QStringLiteral("frame.png"), QStringLiteral("PNG image (*.png)"));
            if (path.isEmpty()) return;
            snapshot->setEnabled(false);
            auto *pending = new QObject(window);
            connect(session_, &QtSpice::Session::stateChanged, pending, [pending, snapshot](auto state) {
                if (state == QtSpice::Session::State::Disconnected) { snapshot->setEnabled(true); delete pending; }
            });
            auto connection = std::make_shared<QMetaObject::Connection>();
            *connection = connect(session_, &QtSpice::Session::monitorFrameReady, pending, [window, snapshot, id, path, connection, pending](QtSpice::MonitorId source, const QImage &image) {
                if (source != id || image.isNull()) return;
                QObject::disconnect(*connection); pending->deleteLater(); snapshot->setEnabled(true);
                window->statusBar()->showMessage(image.save(path) ? QStringLiteral("Frame saved") : QStringLiteral("Could not save frame"));
            });
        });
        addInputMenu(window->menuBar(), window->statusBar(), widget, session_);
        auto *resize = view->addAction(QStringLiteral("Auto Resize Guest"));
        resize->setCheckable(true); resize->setChecked(ViewerSettings::autoResizeGuest());
        widget->setAutoResizeGuest(resize->isChecked());
        connect(resize, &QAction::toggled, widget, &QtSpice::Widget::setAutoResizeGuest);
        for (auto *child : primary_->children()) if (auto *retry = dynamic_cast<Reconnect *>(child)) retry->attach(window);
        addMenu(view); window->resize(1024, 768); window->show();
    }
    void restore() {
        if (!session_->monitorConfigurationAvailable() || !recovering_ || restored_) return;
            auto layout = session_->monitors();
            if (layout.isEmpty()) return;
            for (auto &head : layout) for (const auto &saved : saved_) if (saved.id == head.id) head = saved;
            restored_ = true; restoring_ = true; session_->requestMonitorLayout(layout); restoring_ = false;
    }
    void refresh() {
        if (closing_) return;
        if (session_->monitors().isEmpty()) return; // Transient teardown must not hide windows.
        if (!recovering_) {
            saved_ = session_->monitors();
            for (auto &head : saved_) for (const auto &requested : requested_) if (requested.id == head.id) head = requested;
        }
        restore();
        if (recovering_ && !restored_) return;
        if (restored_) recovering_ = false;
        for (const auto &m : session_->monitors()) if (m.enabled && !disabling_.contains(m.id)) { pending_.remove(m.id); open(m.id); }
        for (auto it = windows_.begin(); it != windows_.end(); ++it) {
            bool active = pending_.contains(it.key());
            for (const auto &m : session_->monitors()) if (m.id == it.key() && m.enabled && !disabling_.contains(m.id)) active = true;
            if (!active && it.value()) it.value()->hide();
        }
        refreshMenus();
    }
};
