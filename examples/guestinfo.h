#pragma once
#include "compressionmenu.h"
#include <QMainWindow>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>

namespace GuestInfo {
inline void add(QMainWindow *window, QtSpice::Session *session, const QString &suffix = QStringLiteral("QtSpice Console")) {
    auto title = [window, session, suffix] {
        QString name = session->guestName();
        // Keep server-provided control characters out of window decorations.
        for (auto &c : name) if (!c.isPrint()) c = QChar(' ');
        window->setWindowTitle(name.isEmpty() ? suffix : name + QStringLiteral(" — ") + suffix);
    };
    title();
    QObject::connect(session, &QtSpice::Session::guestInfoChanged, window, title);
    auto *action = CompressionMenu::connectionMenu(window->menuBar())->addAction(QStringLiteral("Guest Details…"));
    action->setObjectName(QStringLiteral("guestDetailsAction"));
    QObject::connect(action, &QAction::triggered, window, [window, session] {
        if (auto *existing = window->findChild<QDialog *>(QStringLiteral("guestDetailsDialog"))) {
            existing->show(); existing->raise(); existing->activateWindow(); return;
        }
        auto *dialog = new QDialog(window);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setObjectName(QStringLiteral("guestDetailsDialog"));
        dialog->setWindowTitle(QStringLiteral("Guest Details"));
        auto *layout = new QFormLayout(dialog);
        auto *name = new QLabel(dialog); auto *uuid = new QLabel(dialog);
        name->setObjectName(QStringLiteral("guestName")); uuid->setObjectName(QStringLiteral("guestUuid"));
        for (auto *label : {name, uuid}) { label->setTextFormat(Qt::PlainText); label->setTextInteractionFlags(Qt::TextSelectableByMouse); }
        layout->addRow(QStringLiteral("Name:"), name); layout->addRow(QStringLiteral("UUID:"), uuid);
        auto refresh = [session, name, uuid] {
            name->setText(session->guestName().isEmpty() ? QStringLiteral("Not available") : session->guestName());
            uuid->setText(session->guestUuid().isEmpty() ? QStringLiteral("Not available") : session->guestUuid());
        };
        refresh(); QObject::connect(session, &QtSpice::Session::guestInfoChanged, dialog, refresh);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
        layout->addRow(buttons);
        QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
        dialog->show();
    });
}
}
