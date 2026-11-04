#include "../examples/browserlogging.h"
#include "../examples/viewersettings.h"
#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

class ViewerSettingsTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void chromiumLoggingDefaults() {
        QCOMPARE(browserLoggingFlags({}), QByteArray("--disable-logging --log-level=3"));
        const QByteArray proxy("--proxy-server=socks5://localhost:1080 --proxy-bypass-list=<-loopback>");
        const auto quiet = browserLoggingFlags(proxy);
        QCOMPARE(quiet, proxy + " --disable-logging --log-level=3");
        QCOMPARE(browserLoggingFlags(quiet), quiet);
        for (const QByteArray flags : {QByteArray("--enable-logging --log-level=0"),
             QByteArray("--enable-logging=stderr"), QByteArray("--log-level=1"),
             QByteArray("--log-level 0"), QByteArray("--disable-logging"),
             QByteArray("--v=1"), QByteArray("--vmodule=ssl*=2")}) {
            QCOMPARE(browserLoggingFlags(flags), flags);
        }
        // A substring in an unrelated argument must not enable internal logs.
        QVERIFY(browserLoggingFlags("--user-agent=--enable-logging").endsWith("--log-level=3"));
    }
    void separateViewerPreferences() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
        QCoreApplication::setOrganizationName(QStringLiteral("QtSpiceTest"));
        QCoreApplication::setApplicationName(QStringLiteral("simple-viewer"));
        QCOMPARE(ViewerSettings::scalingMode(), 0);
        ViewerSettings::setScalingMode(2);
        QCOMPARE(ViewerSettings::scalingMode(), 2);
        QVERIFY(ViewerSettings::autoResizeGuest());
        ViewerSettings::setAutoResizeGuest(false);
        QVERIFY(!ViewerSettings::autoResizeGuest());
        QCoreApplication::setApplicationName(QStringLiteral("ProxmoxViewer"));
        QVERIFY(ViewerSettings::autoResizeGuest());
        QCOMPARE(ViewerSettings::scalingMode(), 0);
        ViewerSettings::setScalingMode(99);
        QCOMPARE(ViewerSettings::scalingMode(), 0);
        ViewerSettings::setAutoResizeGuest(true);
        QCoreApplication::setApplicationName(QStringLiteral("simple-viewer"));
        QVERIFY(!ViewerSettings::autoResizeGuest());
    }
};
QTEST_GUILESS_MAIN(ViewerSettingsTest)
#include "viewersettings_test.moc"
