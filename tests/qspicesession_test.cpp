// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QtSpice/QSpiceSession.h>
#include "../src/core/clipboardcapability.h"
#include <QSignalSpy>
#include "../examples/compressionmenu.h"
#include "../examples/guestinfo.h"
#include <QTemporaryDir>
#undef signals
#include "../src/core/qspicesession.h"
namespace QtSpice { struct SessionTestAccess { static void status(Session &s, const QString &text) { s.setMonitorStatus(text); } static void info(Session &s, QString name, QString uuid) { s.updateGuestInfo(name, uuid); } static auto &worker(Session &s) { return *s.worker_; } static void observed(Session &s, const QList<MonitorInfo> &layout, bool pending) { s.worker_->layoutPending = pending; s.worker_->observeMonitorLayout(layout); } static void actual(Session &s, const QList<MonitorInfo> &layout) { s.monitors_ = layout; } static bool pending(Session &s) { return s.worker_->layoutPending; } static auto &desired(Session &s) { return s.worker_->desiredMonitors; } }; }
#include <QtTest>
#include <QLoggingCategory>
#include <QScopeGuard>

namespace {
QStringList connectionMessages;
void captureConnectionLog(QtMsgType, const QMessageLogContext &context, const QString &message) {
    if (QString::fromLatin1(context.category) == QStringLiteral("qtspice.connection"))
        connectionMessages.append(message);
}
}

class SessionTest : public QObject {
    Q_OBJECT
private slots:
    void monitorDetailsPresentation() {
        using namespace QtSpice;
        QList<MonitorInfo> actual = {{{0,0}, {0,0,1920,953}, true}, {{0,1}, {}, false}};
        auto text = GuestInfo::monitorDetails(actual, actual, {});
        QCOMPARE(text.count(QStringLiteral("Monitor 0:0")), 1);
        QCOMPARE(text.count(QStringLiteral("Monitor 0:1")), 1);
        QVERIFY(!text.contains(QStringLiteral("requested")));
        auto requested = actual;
        requested[0].geometry.setSize({1280,720});
        text = GuestInfo::monitorDetails(actual, requested, QStringLiteral("Unconfirmed"));
        QCOMPARE(text.count(QStringLiteral("Monitor 0:0")), 1);
        QVERIFY(text.contains(QStringLiteral("Actual: 1920×953 at 0,0; last requested: 1280×720 at 0,0")));
        QVERIFY(text.contains(QStringLiteral("Unconfirmed")));
        requested.append({{1,0}, {0,0,800,600}, true});
        text = GuestInfo::monitorDetails(actual, requested, {});
        QVERIFY(text.contains(QStringLiteral("Monitor 1:0 — Actual: Unavailable; last requested: 800×600 at 0,0")));
        QVERIFY(GuestInfo::monitorDetails({}, {}, {}).contains(QStringLiteral("No monitors available")));
    }
    void sessionDiagnostics() {
        using namespace QtSpice;
        Session s; auto &w = SessionTestAccess::worker(s);
        QSignalSpy changed(&s, &Session::diagnosticsChanged);
        w.session = spice_session_new();
        auto *channel = spice_channel_new(w.session, SPICE_CHANNEL_DISPLAY, 7);
        w.onChannelNew(w.session, channel, &w);
        QCOMPARE(s.diagnostics().channels.size(), 1);
        QCOMPARE(s.diagnostics().channels[0].state, ChannelDiagnostics::State::Created);
        w.onChannelEvent(channel, SPICE_CHANNEL_OPENED, &w);
        QCOMPARE(s.diagnostics().channels[0].state, ChannelDiagnostics::State::Open);
        int count = changed.size();
        w.onChannelEvent(channel, SPICE_CHANNEL_OPENED, &w);
        QCOMPARE(changed.size(), count);
        w.onChannelEvent(channel, SPICE_CHANNEL_SWITCHING, &w);
        QCOMPARE(s.diagnostics().channels[0].state, ChannelDiagnostics::State::Switching);
        w.onChannelEvent(channel, SPICE_CHANNEL_ERROR_TLS, &w);
        QCOMPARE(s.diagnostics().channels[0].state, ChannelDiagnostics::State::Error);
        QVERIFY(GuestInfo::report(&s).contains(QStringLiteral("Error")));
        w.onChannelEvent(channel, SPICE_CHANNEL_CLOSED, &w);
        QCOMPARE(s.diagnostics().channels[0].state, ChannelDiagnostics::State::Closed);
        w.onChannelDestroy(w.session, channel, &w);
        QVERIFY(s.diagnostics().channels.isEmpty());
        w.disconnect();
        QCOMPARE(s.diagnostics(), SessionDiagnostics{});
    }
    void detailsReport() {
        using namespace QtSpice;
        Session s; QMainWindow window;
        GuestInfo::add(&window, &s);
        SessionTestAccess::info(s, QStringLiteral("<b>Guest</b>"), QStringLiteral("test-uuid"));
        window.findChild<QAction *>(QStringLiteral("guestDetailsAction"))->trigger();
        auto *dialog = window.findChild<QDialog *>(QStringLiteral("guestDetailsDialog")); QVERIFY(dialog);
        QCOMPARE(dialog->windowTitle(), QStringLiteral("Session Details"));
        auto *tabs = dialog->findChild<QTabWidget *>(); QCOMPARE(tabs->count(), 3);
        QCOMPARE(tabs->tabText(2), QStringLiteral("Channels && Features"));
        SessionTestAccess::status(s, QStringLiteral("Guest monitor configuration unconfirmed"));
        QVERIFY(qobject_cast<QPlainTextEdit *>(tabs->widget(1))->toPlainText().contains(QStringLiteral("unconfirmed")));
        s.setClipboardMode(Session::ClipboardMode::Disabled);
        QVERIFY(qobject_cast<QPlainTextEdit *>(tabs->widget(2))->toPlainText().contains(QStringLiteral("Clipboard direction: Disabled")));
        const auto parts = GuestInfo::sections(&s);
        for (int i = 0; i < 3; ++i) QCOMPARE(qobject_cast<QPlainTextEdit *>(tabs->widget(i))->toPlainText(), parts[i]);
        QVERIFY(parts[0].contains(QStringLiteral("<b>Guest</b>")));
        dialog->findChild<QPushButton *>(QStringLiteral("copyDetails"))->click();
        QCOMPARE(QApplication::clipboard()->text(), GuestInfo::report(&s));
        SessionTestAccess::info(s, QStringLiteral("New guest"), {});
        QVERIFY(qobject_cast<QPlainTextEdit *>(tabs->widget(0))->toPlainText().contains(QStringLiteral("New guest")));
        QVERIFY(!GuestInfo::report(&s).contains(QStringLiteral("test-uuid")));
        SessionTestAccess::worker(s).disconnect();
        QVERIFY(qobject_cast<QPlainTextEdit *>(tabs->widget(2))->toPlainText().contains(QStringLiteral("No channels available")));
    }
    void guestInformation() {
        using namespace QtSpice;
        Session s; QSignalSpy changes(&s, &Session::guestInfoChanged);
        QMainWindow primary, secondary;
        GuestInfo::add(&primary, &s);
        GuestInfo::add(&secondary, &s, QStringLiteral("Monitor 0:1"));
        QCOMPARE(primary.windowTitle(), QStringLiteral("QtSpice Console"));
        auto &w = SessionTestAccess::worker(s);
        w.session = spice_session_new();
        w.refreshGuestInfo(); // Native all-zero UUID means unavailable.
        QVERIFY(s.guestUuid().isEmpty()); QVERIFY(changes.isEmpty());
        const QString uuid = QStringLiteral("6d23cf36-b402-4e92-bd87-c8af1b25cbb4");
        SessionTestAccess::info(s, QStringLiteral("Alpine <test>"), uuid);
        QCOMPARE(changes.size(), 1);
        QCOMPARE(primary.windowTitle(), QStringLiteral("Alpine <test> — QtSpice Console"));
        QCOMPARE(secondary.windowTitle(), QStringLiteral("Alpine <test> — Monitor 0:1"));
        primary.findChild<QAction *>(QStringLiteral("guestDetailsAction"))->trigger();
        auto *dialog = primary.findChild<QDialog *>(QStringLiteral("guestDetailsDialog")); QVERIFY(dialog);
        primary.findChild<QAction *>(QStringLiteral("guestDetailsAction"))->trigger();
        QCOMPARE(primary.findChildren<QDialog *>(QStringLiteral("guestDetailsDialog")).size(), 1);
        QCOMPARE(dialog->findChild<QLabel *>(QStringLiteral("guestName"))->textFormat(), Qt::PlainText);
        QCOMPARE(dialog->findChild<QLabel *>(QStringLiteral("guestUuid"))->text(), uuid);
        SessionTestAccess::info(s, QStringLiteral("Alpine <test>"), uuid); QCOMPARE(changes.size(), 1);
        w.disconnect();
        QVERIFY(s.guestName().isEmpty()); QVERIFY(s.guestUuid().isEmpty());
        QCOMPARE(primary.windowTitle(), QStringLiteral("QtSpice Console"));
        QCOMPARE(dialog->findChild<QLabel *>(QStringLiteral("guestName"))->text(), QStringLiteral("Not available"));
        SessionTestAccess::info(s, QStringLiteral("Another VM"), {});
        QCOMPARE(secondary.windowTitle(), QStringLiteral("Another VM — Monitor 0:1"));
    }
    void compressionPreferences() {
        using namespace QtSpice;
        using Mode = Session::ImageCompression;
        Session s; auto &w = SessionTestAccess::worker(s);
        QCOMPARE(s.imageCompression(), Mode::ServerDefault);
        QSignalSpy changes(&s, &Session::imageCompressionChanged);
        s.setImageCompression(static_cast<Mode>(100));
        s.setImageCompression(Mode::ServerDefault); QVERIFY(changes.isEmpty());
        auto *native = spice_session_new();
        auto cleanup = qScopeGuard([&] { w.compressionChannels.clear(); w.compressionOverrides.clear(); w.session = nullptr; g_object_unref(native); });
        w.session = native;
        auto *a = spice_channel_new(native, SPICE_CHANNEL_DISPLAY, 0);
        auto *b = spice_channel_new(native, SPICE_CHANNEL_DISPLAY, 1);
        auto *c = spice_channel_new(native, SPICE_CHANNEL_DISPLAY, 2);
        QList<int> sent;
        w.compressionSender = [&](SpiceChannel *channel, int value) { QVERIFY(channel == a || channel == c); sent.append(value); };
        w.compressionOpened(a, true); w.compressionOpened(b, false);
        s.setImageCompression(Mode::Automatic);
        QCOMPARE(sent, QList<int>{SPICE_IMAGE_COMPRESSION_AUTO_GLZ});
        int value = -1; g_object_get(native, "preferred-compression", &value, nullptr);
        QCOMPARE(value, int(SPICE_IMAGE_COMPRESSION_AUTO_GLZ));
        QVERIFY(s.imageCompressionStatus().contains("unsupported display channels"));
        s.setImageCompression(Mode::Automatic); QCOMPARE(sent.size(), 1);
        w.compressionOpened(c, true); // Native startup uses the latest session property.
        QVERIFY(w.compressionOverrides.contains(c)); QCOMPARE(sent.size(), 1);
        s.setImageCompression(Mode::ServerDefault); QCOMPARE(sent.size(), 1);
        QVERIFY(s.imageCompressionStatus().contains("Reconnect"));
        g_object_get(native, "preferred-compression", &value, nullptr);
        QCOMPARE(value, int(SPICE_IMAGE_COMPRESSION_INVALID));
        s.setImageCompression(Mode::Uncompressed); QCOMPARE(sent.size(), 3);
        QVERIFY(!s.imageCompressionStatus().contains("Reconnect"));
        QCOMPARE(sent.last(), int(SPICE_IMAGE_COMPRESSION_OFF));
        w.compressionChannels.remove(a); w.compressionOverrides.remove(a);
        w.compressionChannels.remove(c); w.compressionOverrides.remove(c);
        QVERIFY(s.imageCompressionStatus().contains("unsupported;"));
        w.session = nullptr; // Synthetic session is owned by this test.
        w.disconnect(); QCOMPARE(s.imageCompression(), Mode::Uncompressed);
        QCOMPARE(s.imageCompressionStatus(), QStringLiteral("Applies on connection"));
        s.setImageCompression(Mode::ServerDefault);
        QCOMPARE(s.imageCompressionStatus(), QStringLiteral("Server default"));
    }
    void compressionMenus() {
        using namespace QtSpice;
        using Mode = Session::ImageCompression;
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
        QCoreApplication::setOrganizationName("QtSpiceCompressionTest");
        QCoreApplication::setApplicationName("menus");
        Session s, independent;
        QSettings().setValue("connection/imageCompression", "invalid");
        CompressionMenu::initialize(&s); QCOMPARE(s.imageCompression(), Mode::ServerDefault);
        QMenuBar first, second;
        CompressionMenu::add(&first, &s); CompressionMenu::add(&second, &s);
        QCOMPARE(CompressionMenu::connectionMenu(&first), first.actions().first()->menu());
        QCOMPARE(first.actions().size(), 1);
        auto *menu = first.actions().first()->menu()->actions().first()->menu();
        auto *other = second.actions().first()->menu()->actions().first()->menu();
        auto *connection = first.actions().first()->menu();
        connection->ensurePolished();
        const auto item = connection->actionGeometry(connection->actions().first());
        QVERIFY(connection->styleSheet().isEmpty());
        auto *style = dynamic_cast<CompressionMenu::ConnectionMenuStyle *>(connection->style());
        QVERIFY(style);
        QStyleOptionMenuItem option;
        option.initFrom(connection); option.menuItemType = QStyleOptionMenuItem::SubMenu;
        option.text = QStringLiteral("Image Compression");
        const QSize content(connection->fontMetrics().horizontalAdvance(option.text), connection->fontMetrics().height());
        const auto nativeSize = style->baseStyle()->sizeFromContents(QStyle::CT_MenuItem, &option, content, connection);
        const auto spacedSize = style->sizeFromContents(QStyle::CT_MenuItem, &option, content, connection);
        QVERIFY(spacedSize.width() > nativeSize.width());
        QCOMPARE(spacedSize.height(), nativeSize.height());
        QVERIFY(item.width() >= spacedSize.width());
        option.menuItemType = QStyleOptionMenuItem::Normal;
        QCOMPARE(style->sizeFromContents(QStyle::CT_MenuItem, &option, content, connection),
                 style->baseStyle()->sizeFromContents(QStyle::CT_MenuItem, &option, content, connection));
        connection->resize(connection->sizeHint());
        connection->setActiveAction(nullptr);
        const auto inactive = connection->grab().toImage();
        connection->setActiveAction(connection->actions().first());
        QVERIFY(connection->grab().toImage() != inactive);
        connection->setActiveAction(nullptr);
        menu->actions()[1]->trigger();
        QCOMPARE(s.imageCompression(), Mode::Automatic);
        QVERIFY(other->actions()[1]->isChecked()); QVERIFY(!other->actions()[0]->isChecked());
        QCOMPARE(independent.imageCompression(), Mode::ServerDefault);
        QCOMPARE(QSettings().value("connection/imageCompression").toString(), QStringLiteral("automatic"));
        CompressionMenu::initialize(&independent); QCOMPARE(independent.imageCompression(), Mode::Automatic);
        other->actions()[2]->trigger(); QVERIFY(menu->actions()[2]->isChecked());
        QCOMPARE(independent.imageCompression(), Mode::Automatic);
        QCOMPARE(menu->actions().last()->text(), s.imageCompressionStatus());
        QSettings().clear();
    }
    void batchedDisplayAndStaleWork() {
        using namespace QtSpice;
        Session s;
        auto &worker = SessionTestAccess::worker(s);
        SpiceSession *native = spice_session_new();
        auto *channel = spice_channel_new(native, SPICE_CHANNEL_DISPLAY, 7);
        auto cleanup = qScopeGuard([&] { worker.displays.clear(); g_object_unref(native); });
        worker.displays[7] = SPICE_DISPLAY_CHANNEL(channel);
        QImage pixels(100,80,QImage::Format_RGB32); pixels.fill(Qt::red);
        worker.primaryReader = [&](SpiceChannel *, SpiceDisplayPrimary *p) {
            p->format = SPICE_SURFACE_FMT_32_xRGB; p->width = pixels.width(); p->height = pixels.height();
            p->stride = pixels.bytesPerLine(); p->data = pixels.bits(); return true;
        };
        SessionTestAccess::actual(s, {{{7,0},{0,0,50,80},true},{{7,1},{50,0,50,80},true}});
        QSignalSpy updates(&s, &Session::monitorFrameUpdated);
        worker.scheduleDisplay(channel);
        QCoreApplication::sendPostedEvents();
        QCOMPARE(updates.size(),2); updates.clear();
        auto retained = s.monitorFrame({7,0});
        pixels.setPixelColor(3,4,Qt::blue); pixels.setPixelColor(51,4,Qt::green);
        worker.scheduleDisplay(channel,{3,4,1,1}); worker.scheduleDisplay(channel,{51,4,1,1});
        QCOMPARE(updates.size(),0);
        QCoreApplication::sendPostedEvents();
        QCOMPARE(updates.size(),2);
        QCOMPARE(s.monitorFrame({7,0}).pixelColor(3,4),QColor(Qt::blue));
        QCOMPARE(s.monitorFrame({7,1}).pixelColor(1,4),QColor(Qt::green));
        QCOMPARE(retained.pixelColor(3,4),QColor(Qt::red));
        updates.clear();
        worker.scheduleDisplay(channel,{0,0,1,1}); worker.clearDisplay(7);
        QVERIFY(s.monitorFrame({7,0}).isNull()); updates.clear();
        QCoreApplication::sendPostedEvents(); QVERIFY(updates.isEmpty());
        int nonemptyLegacy = 0;
        connect(&s, &Session::monitorFrameReady, &s, [&](MonitorId, const QImage &image) { if (!image.isNull()) ++nonemptyLegacy; });
        bool cleared = false;
        connect(&s, &Session::monitorFrameUpdated, &s, [&](MonitorId, const QRegion &) {
            if (!cleared) { cleared = true; worker.clearDisplay(7); }
        });
        worker.scheduleDisplay(channel); QCoreApplication::sendPostedEvents();
        QCOMPARE(nonemptyLegacy,0);
        worker.scheduleDisplay(channel); s.disconnectFromHost(); updates.clear();
        QCoreApplication::sendPostedEvents(); QVERIFY(updates.isEmpty());
    }

    void alreadyAppliedLayoutNeedsNoNewGuestEvent() {
        QtSpice::Session s;
        const QList<QtSpice::MonitorInfo> actual{{{0,0},{0,0,800,600},true},{{0,1},{800,0,1024,768},true}};
        QtSpice::SessionTestAccess::actual(s, actual);
        QSignalSpy status(&s, &QtSpice::Session::monitorConfigurationStatusChanged);
        s.requestMonitorLayout(actual);
        QVERIFY(!QtSpice::SessionTestAccess::pending(s));
        QVERIFY(!status.isEmpty()); QVERIFY(status.last().first().toString().isEmpty());
        s.setDesiredMonitorSize({0,1}, QSize(1280,720));
        QVERIFY(QtSpice::SessionTestAccess::pending(s));
        s.requestMonitorLayout(actual); // Restore the already-applied layout while another request is pending.
        QVERIFY(!QtSpice::SessionTestAccess::pending(s));
    }
    void lateGuestTopologyRetiresTimedOutWarning() {
        QtSpice::Session s;
        const QList<QtSpice::MonitorInfo> requested{{{0,0}, {0,0,800,600}, true}, {{0,1}, {800,0,1024,768}, true}};
        s.requestMonitorLayout(requested);
        QSignalSpy status(&s, &QtSpice::Session::monitorConfigurationStatusChanged);
        const QList<QtSpice::MonitorInfo> actual{{{0,0}, {0,0,800,600}, true}, {{0,1}, {800,0,640,480}, true}};
        QtSpice::SessionTestAccess::observed(s, actual, true);
        QVERIFY(status.isEmpty()); // Still pending: a different size is not confirmation.
        QtSpice::SessionTestAccess::observed(s, actual, false); // New topology after timeout/manual activation.
        QVERIFY(!status.isEmpty()); QVERIFY(status.last().first().toString().isEmpty());
        QCOMPARE(QtSpice::SessionTestAccess::desired(s).value({0,1}).geometry.size(), QSize(640,480));
        s.requestMonitorLayout(requested);
        status.clear();
        QtSpice::SessionTestAccess::observed(s, requested, true);
        QVERIFY(!status.isEmpty()); QVERIFY(status.last().first().toString().isEmpty());
    }
    void connectionDiagnostics() {
        connectionMessages.clear();
        const auto previousHandler = qInstallMessageHandler(captureConnectionLog);
        const auto restore = qScopeGuard([previousHandler] {
            qInstallMessageHandler(previousHandler);
            QLoggingCategory::setFilterRules({});
        });
        QLoggingCategory::setFilterRules(QStringLiteral("qtspice.connection.debug=false"));
        {
            QtSpice::Session session;
            session.connectToHost(QStringLiteral("127.0.0.1"), 1);
            session.disconnectFromHost();
        }
        QVERIFY(connectionMessages.isEmpty());
        QLoggingCategory::setFilterRules(QStringLiteral("qtspice.connection.debug=true"));
        {
            QtSpice::Session session;
            QSignalSpy errors(&session, &QtSpice::Session::errorOccurred);
            QtSpice::ConnectionConfig config;
            config.host = QStringLiteral("127.0.0.1");
            config.port = 1;
            config.password = QStringLiteral("diagnostic-test-secret");
            session.connectToConfig(config);
            QTRY_VERIFY_WITH_TIMEOUT(!errors.isEmpty(), 5000);
        }
        const QString messages = connectionMessages.join(QLatin1Char('\n'));
        QVERIFY(messages.contains(QStringLiteral("connection requested")));
        QVERIFY(messages.contains(QStringLiteral("channel created main id 0")));
        QVERIFY(messages.contains(QStringLiteral("error-connect")));
        QVERIFY(messages.contains(QStringLiteral("error-domain")));
        QVERIFY(messages.contains(QStringLiteral("local teardown requested")));
        QVERIFY(!messages.contains(QStringLiteral("diagnostic-test-secret")));
    }
    void clipboardRequiresByDemandCapability() {
        QVERIFY(!QtSpice::clipboardByDemandReady(false, false));
        QVERIFY(!QtSpice::clipboardByDemandReady(true, false));
        QVERIFY(!QtSpice::clipboardByDemandReady(false, true));
        QVERIFY(QtSpice::clipboardByDemandReady(true, true));
    }
    void monitorLayoutPreservesOtherHeads() {
        QtSpice::Session s;
        s.requestMonitorLayout({{{0,0}, {0,0,800,600}, true}, {{0,1}, {800,0,640,480}, true}});
        s.setDesiredMonitorSize({0,1}, {1280,720});
        auto &desired = QtSpice::SessionTestAccess::desired(s);
        QCOMPARE(desired.size(), 2);
        QCOMPARE(desired.value({0,0}).geometry.size(), QSize(800,600));
        QCOMPARE(desired.value({0,1}).geometry, QRect(800,0,1280,720));
        s.setMonitorEnabled({0,1}, false);
        QVERIFY(!desired.value({0,1}).enabled);
        s.setMonitorEnabled({0,1}, true);
        QCOMPARE(desired.value({0,1}).geometry.size(), QSize(1280,720));
        s.setMonitorEnabled({}, false);
        QVERIFY(desired.value({}).enabled);
        const auto before = desired;
        s.requestMonitorLayout({{{0,1},{0,0,800,600},true}, {{1,0},{800,0,800,600},true}});
        QCOMPARE(desired, before); // Colliding wire IDs must not send a partial layout.
        s.requestMonitorLayout({{{0,0},{0,0,20000,600},true}});
        QCOMPARE(desired, before);
    }
    void cancelledAttemptsCannotPublishLateFailure() {
        QtSpice::Session session;
        QSignalSpy errors(&session, &QtSpice::Session::errorOccurred);
        for (int i = 0; i < 10; ++i) {
            session.connectToHost(QStringLiteral("127.0.0.1"), 1);
            session.disconnectFromHost();
            QCoreApplication::processEvents();
            QCOMPARE(session.state(), QtSpice::Session::State::Disconnected);
        }
        QTest::qWait(30);
        QVERIFY(errors.isEmpty());
        QVERIFY(!session.inputReady()); QVERIFY(!session.clipboardReady());
        QVERIFY(session.monitors().isEmpty());
    }
    void initialState() {
        QtSpice::Session session;
        QCOMPARE(session.state(), QtSpice::Session::State::Disconnected);
    }
    void rejectInvalidAddress() {
        QtSpice::Session session;
        QSignalSpy errors(&session, &QtSpice::Session::errorOccurred);
        session.connectToHost({}, 0);
        QCOMPARE(errors.size(), 1);
        QCOMPARE(session.state(), QtSpice::Session::State::Disconnected);
    }
    void disconnectIsIdempotent() {
        QtSpice::Session session;
        QSignalSpy states(&session, &QtSpice::Session::stateChanged);
        session.disconnectFromHost();
        session.disconnectFromHost();
        QCOMPARE(states.size(), 0);
    }
    void rejectInvalidConfig() {
        QtSpice::Session session;
        QSignalSpy errors(&session, &QtSpice::Session::errorOccurred);
        QtSpice::ConnectionConfig config;
        config.host = QStringLiteral("example.invalid");
        session.connectToConfig(config);
        QCOMPARE(errors.size(), 1);
        QCOMPARE(session.state(), QtSpice::Session::State::Disconnected);
    }
    void failedConnectionReportsError() {
        QtSpice::Session session;
        QSignalSpy errors(&session, &QtSpice::Session::errorOccurred);
        session.connectToHost(QStringLiteral("127.0.0.1"), 1);
        QTRY_VERIFY_WITH_TIMEOUT(!errors.isEmpty(), 5000);
        QCOMPARE(session.state(), QtSpice::Session::State::Disconnected);
    }
    void invalidProxyIsRejectedBeforeConnecting() {
        QtSpice::Session session;
        QSignalSpy errors(&session, &QtSpice::Session::errorOccurred);
        QtSpice::ConnectionConfig config;
        config.host = QStringLiteral("guest.invalid"); config.port = 5900;
        config.networkProxy = QStringLiteral("ftp://user:secret@proxy.invalid");
        session.connectToConfig(config);
        QCOMPARE(errors.size(), 1);
        QVERIFY(!errors.first().first().toString().contains(QStringLiteral("secret")));
        QCOMPARE(session.state(), QtSpice::Session::State::Disconnected);
    }
    void disconnectedInputIsIgnored() {
        QtSpice::Session session;
        QCOMPARE(session.mouseMode(), QtSpice::Session::MouseMode::Client);
        QVERIFY(!session.inputReady());
        QVERIFY(!session.clipboardReady());
        session.setHostClipboardText(QStringLiteral("test"));
        session.setHostClipboardText(QString(4 * 1024 * 1024 + 1, QLatin1Char('x')));
        session.sendKey(0x1e, true);
        session.sendKey(0x1e, false);
        session.sendPointer(10, 20, 0);
        session.sendRelativePointer(1, -1, 0);
        session.sendButton(1, true, 1);
        session.releaseAllInputs();
        session.disconnectFromHost();
        session.sendButton(1, false, 0);
        QCOMPARE(session.state(), QtSpice::Session::State::Disconnected);
    }
    void destroyDuringHandshake() {
        for (int i = 0; i < 20; ++i) {
            auto *session = new QtSpice::Session;
            session->connectToHost(QStringLiteral("127.0.0.1"), 1);
            QCoreApplication::processEvents();
            delete session;
            QCoreApplication::processEvents();
        }
    }
};
QTEST_MAIN(SessionTest)
#include "qspicesession_test.moc"
