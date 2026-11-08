// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QtSpice/QSpiceWidget.h>
#include <QApplication>
#include <QDebug>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    if (app.arguments().size() != 3) {
        qWarning() << "Usage: embedded-viewer host port";
        return 1;
    }
    bool valid = false;
    const int port = app.arguments().at(2).toInt(&valid);
    if (!valid || port < 1 || port > 65535) return 1;
    QtSpice::Widget view;
    auto *session = new QtSpice::Session(&view);
    view.setSession(session);
    QObject::connect(session, &QtSpice::Session::errorOccurred, &view,
                     [](const QString &error) { qWarning().noquote() << error; });
    view.resize(800, 600);
    view.show();
    session->connectToHost(app.arguments().at(1), quint16(port));
    return app.exec();
}
