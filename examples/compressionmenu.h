// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <QtSpice/QSpiceSession.h>
#include <QActionGroup>
#include <QMenuBar>
#include <QSettings>
#include <QProxyStyle>
#include <QStyleOptionMenuItem>

namespace CompressionMenu {
using Mode = QtSpice::Session::ImageCompression;
// Adjust layout only. All painting, including the selected item, stays with
// the desktop style. Own a proxy-created base style, never QApplication's style.
class ConnectionMenuStyle final : public QProxyStyle {
public:
    QSize sizeFromContents(ContentsType type, const QStyleOption *option,
                           const QSize &contents, const QWidget *widget = nullptr) const override {
        QSize size = QProxyStyle::sizeFromContents(type, option, contents, widget);
        const auto *item = qstyleoption_cast<const QStyleOptionMenuItem *>(option);
        if (type == CT_MenuItem && item && item->menuItemType == QStyleOptionMenuItem::SubMenu)
            size.rwidth() += pixelMetric(PM_MenuButtonIndicator, option, widget)
                + item->fontMetrics.horizontalAdvance(QStringLiteral("  "));
        return size;
    }
};
inline QString key(Mode mode) {
    return mode == Mode::Automatic ? QStringLiteral("automatic") : mode == Mode::Uncompressed ? QStringLiteral("uncompressed") : QStringLiteral("server-default");
}
inline void initialize(QtSpice::Session *session) {
    const auto value = QSettings().value(QStringLiteral("connection/imageCompression")).toString();
    session->setImageCompression(value == "automatic" ? Mode::Automatic : value == "uncompressed" ? Mode::Uncompressed : Mode::ServerDefault);
}
inline QMenu *connectionMenu(QMenuBar *bar) {
    for (auto *action : bar->actions()) if (action->text() == QStringLiteral("Connection") && action->menu()) return action->menu();
    auto *menu = bar->addMenu(QStringLiteral("Connection"));
    auto *style = new ConnectionMenuStyle;
    style->setParent(menu);
    menu->setStyle(style);
    return menu;
}
inline void add(QMenuBar *bar, QtSpice::Session *session) {
    auto *menu = connectionMenu(bar)->addMenu(QStringLiteral("Image Compression"));
    auto *group = new QActionGroup(menu);
    const QStringList names{QStringLiteral("Server Default"), QStringLiteral("Automatic Compression"), QStringLiteral("Uncompressed")};
    for (int i = 0; i < names.size(); ++i) {
        const auto mode = static_cast<Mode>(i);
        auto *action = menu->addAction(names[i]); action->setCheckable(true); group->addAction(action);
        action->setChecked(session->imageCompression() == mode);
        QObject::connect(action, &QAction::triggered, session, [session, mode] {
            session->setImageCompression(mode);
            QSettings().setValue(QStringLiteral("connection/imageCompression"), key(mode));
        });
        QObject::connect(session, &QtSpice::Session::imageCompressionChanged, action, [action, mode](Mode current) { action->setChecked(current == mode); });
    }
    menu->addSeparator();
    auto *status = menu->addAction(session->imageCompressionStatus()); status->setEnabled(false);
    QObject::connect(session, &QtSpice::Session::imageCompressionStatusChanged, status, &QAction::setText);
}
}
