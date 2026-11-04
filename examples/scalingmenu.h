#pragma once
#include "viewersettings.h"
#include <QtSpice/QSpiceWidget.h>
#include <QActionGroup>
#include <QMenu>

inline void addScalingMenu(QMenu *view, QtSpice::Widget *widget) {
    auto *menu = view->addMenu(QStringLiteral("Scaling"));
    auto *group = new QActionGroup(menu);
    group->setExclusive(true);
    using Mode = QtSpice::Widget::ScalingMode;
    widget->setScalingMode(static_cast<Mode>(ViewerSettings::scalingMode()));
    const QStringList names{QStringLiteral("Smooth Fit"), QStringLiteral("Unfiltered Fit"), QStringLiteral("Native 1:1 Pixels")};
    for (int i = 0; i < names.size(); ++i) {
        auto *action = menu->addAction(names[i]);
        action->setCheckable(true);
        action->setChecked(int(widget->scalingMode()) == i);
        group->addAction(action);
        QObject::connect(action, &QAction::triggered, widget, [widget, i] {
            widget->setScalingMode(static_cast<Mode>(i));
            ViewerSettings::setScalingMode(i);
        });
        QObject::connect(widget, &QtSpice::Widget::scalingModeChanged, action, [action, i](Mode mode) {
            action->setChecked(int(mode) == i);
        });
    }
}
