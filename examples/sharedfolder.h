#pragma once
#include <QtSpice/QSpiceSession.h>
#include <QCommandLineParser>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>
#include <QCheckBox>

namespace SharedFolder {
inline void addOptions(QCommandLineParser &parser) {
    parser.addOption({QStringLiteral("shared-dir"), QStringLiteral("Share a local directory with each console"), QStringLiteral("path")});
    parser.addOption({QStringLiteral("shared-dir-mode"), QStringLiteral("Shared directory access: ro or rw (required with --shared-dir)"), QStringLiteral("mode")});
}
inline bool parse(const QCommandLineParser &parser, std::optional<QtSpice::SharedDirectory> &share, QString &error) {
    if (!parser.isSet(QStringLiteral("shared-dir")) && !parser.isSet(QStringLiteral("shared-dir-mode"))) return true;
    const QString mode = parser.value(QStringLiteral("shared-dir-mode"));
    if (!parser.isSet(QStringLiteral("shared-dir")) || (mode != QStringLiteral("ro") && mode != QStringLiteral("rw"))) {
        error = QStringLiteral("Use --shared-dir PATH together with --shared-dir-mode ro|rw");
        return false;
    }
    share = QtSpice::validateSharedDirectory({parser.value(QStringLiteral("shared-dir")), mode == QStringLiteral("ro")
        ? QtSpice::SharedDirectoryAccess::ReadOnly : QtSpice::SharedDirectoryAccess::ReadWrite}, &error);
    return share.has_value();
}
inline QString status(QtSpice::Session *session) {
    using S = QtSpice::Session::SharingState;
    switch (session->sharingState()) {
    case S::Disabled: return QStringLiteral("Sharing disabled");
    case S::WaitingForChannel: return QStringLiteral("Waiting for WebDAV channel. Configure org.spice-space.webdav.0 in the VM.");
    case S::Connecting: return QStringLiteral("Connecting WebDAV channel…");
    case S::WaitingForGuest: return QStringLiteral("Waiting for guest WebDAV service (spice-webdavd).");
    case S::Active: return QStringLiteral("WebDAV channel active. Open the shared folder in the guest file manager.");
    case S::Error: return session->sharingError();
    }
    return {};
}
inline void addMenu(QMenuBar *bar, QtSpice::Session *session) {
    auto *action = bar->addMenu(QStringLiteral("Files"))->addAction(QStringLiteral("Shared Folder…"));
    QObject::connect(action, &QAction::triggered, session, [bar, session] {
        QDialog dialog(bar->window());
        dialog.setWindowTitle(QStringLiteral("Shared Folder"));
        auto *layout = new QVBoxLayout(&dialog);
        auto *enabled = new QCheckBox(QStringLiteral("Enable sharing"), &dialog);
        auto *path = new QLineEdit(&dialog);
        path->setObjectName(QStringLiteral("sharedDirectoryPath"));
        auto *browse = new QPushButton(QStringLiteral("Choose Folder…"), &dialog);
        auto *ro = new QRadioButton(QStringLiteral("Read-only"), &dialog);
        auto *rw = new QRadioButton(QStringLiteral("Read/write (guest can modify and delete files)"), &dialog);
        if (const auto share = session->sharedDirectory()) {
            enabled->setChecked(true); path->setText(share->path);
            (share->access == QtSpice::SharedDirectoryAccess::ReadOnly ? ro : rw)->setChecked(true);
        }
        auto *label = new QLabel(status(session), &dialog);
        label->setWordWrap(true);
        QObject::connect(session, &QtSpice::Session::sharingChanged, label, [label, session] { label->setText(status(session)); });
        layout->addWidget(enabled); layout->addWidget(path); layout->addWidget(browse);
        layout->addWidget(ro); layout->addWidget(rw); layout->addWidget(label);
        QObject::connect(browse, &QPushButton::clicked, &dialog, [&] {
            const QString folder = QFileDialog::getExistingDirectory(&dialog, QStringLiteral("Share Folder"), path->text());
            if (!folder.isEmpty()) path->setText(folder);
        });
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close, &dialog);
        layout->addWidget(buttons);
        QObject::connect(buttons->button(QDialogButtonBox::Close), &QPushButton::clicked, &dialog, &QDialog::reject);
        QObject::connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, &dialog, [&] {
            if (!enabled->isChecked()) { session->disableSharedDirectory(); return; }
            QString error;
            if (!ro->isChecked() && !rw->isChecked()) error = QStringLiteral("Select Read-only or Read/write.");
            else session->setSharedDirectory({path->text(), ro->isChecked() ? QtSpice::SharedDirectoryAccess::ReadOnly
                : QtSpice::SharedDirectoryAccess::ReadWrite}, &error);
            if (!error.isEmpty()) QMessageBox::warning(&dialog, QStringLiteral("Shared Folder"), error);
        });
        dialog.resize(520, 280);
        dialog.exec();
    });
}
}
