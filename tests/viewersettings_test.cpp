#include "../examples/viewersettings.h"
#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

class ViewerSettingsTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void separateViewerPreferences() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
        QCoreApplication::setOrganizationName(QStringLiteral("QtSpiceTest"));
        QCoreApplication::setApplicationName(QStringLiteral("simple-viewer"));
        QVERIFY(ViewerSettings::autoResizeGuest());
        ViewerSettings::setAutoResizeGuest(false);
        QVERIFY(!ViewerSettings::autoResizeGuest());
        QCoreApplication::setApplicationName(QStringLiteral("ProxmoxViewer"));
        QVERIFY(ViewerSettings::autoResizeGuest());
        ViewerSettings::setAutoResizeGuest(true);
        QCoreApplication::setApplicationName(QStringLiteral("simple-viewer"));
        QVERIFY(!ViewerSettings::autoResizeGuest());
    }
};
QTEST_GUILESS_MAIN(ViewerSettingsTest)
#include "viewersettings_test.moc"
