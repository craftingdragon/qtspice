#pragma once
#include <QtSpice/QSpiceWidget.h>
#include <QDialog>
#include <QFileDialog>
#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QLabel>
#include <QTreeWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMessageBox>

class FileSending final : public QObject {
public:
    FileSending(QMainWindow *primary, QtSpice::Session *session) : QObject(primary), primary_(primary), session_(session) {
        connect(primary, &QObject::destroyed, this, [this] { if (dialog_) delete dialog_; });
    }
    ~FileSending() override { if (dialog_) delete dialog_; }
    void add(QMainWindow *window) {
        QMenu *files = nullptr;
        for (auto *action : window->menuBar()->actions()) if (action->menu() && action->text() == QStringLiteral("Files")) files = action->menu();
        if (!files) files = window->menuBar()->addMenu(QStringLiteral("Files"));
        auto *send = files->addAction(QStringLiteral("Send Files…")); send->setObjectName(QStringLiteral("sendFilesAction"));
        auto *transfers = files->addAction(QStringLiteral("File Transfers…"));
        auto update = [this, send] { send->setEnabled(session_->fileTransferReady()); send->setToolTip(session_->fileTransferReadinessReason()); };
        connect(session_, &QtSpice::Session::fileTransferReadinessChanged, send, update);
        connect(session_, &QtSpice::Session::stateChanged, send, update); update();
        connect(send, &QAction::triggered, window, [this, window] {
            release(window);
            const auto paths = QFileDialog::getOpenFileNames(window, QStringLiteral("Send files to guest"));
            if (!paths.isEmpty()) submit(paths, window);
        });
        connect(transfers, &QAction::triggered, window, [this, window] { release(window); show(); });
        window->installEventFilter(this);
        for (auto *widget : window->findChildren<QtSpice::Widget *>()) { widget->setAcceptDrops(true); widget->installEventFilter(this); }
    }
private:
    QMainWindow *primary_;
    QtSpice::Session *session_;
    QPointer<QDialog> dialog_;
    static void release(QWidget *window) { for (auto *widget : window->findChildren<QtSpice::Widget *>()) widget->releaseInputCapture(); }
    static QStringList paths(const QMimeData *mime) {
        if (!mime->hasUrls()) return {};
        QStringList result;
        for (const auto &url : mime->urls()) { if (!url.isLocalFile()) return {}; result.append(url.toLocalFile()); }
        return result;
    }
    void submit(const QStringList &paths, QWidget *window) {
        release(window); QString error;
        if (!session_->sendFiles(paths, &error)) QMessageBox::warning(window, QStringLiteral("Send Files"), error);
        else show();
    }
    bool eventFilter(QObject *object, QEvent *event) override {
        if (object == primary_ && event->type() == QEvent::Close) { session_->cancelAllFileTransfers(); if (dialog_) dialog_->close(); }
        if (event->type() == QEvent::DragEnter) {
            auto *drag = static_cast<QDragEnterEvent *>(event);
            if (!paths(drag->mimeData()).isEmpty()) drag->acceptProposedAction();
            return true;
        }
        if (event->type() == QEvent::Drop) {
            auto *drop = static_cast<QDropEvent *>(event); const auto files = paths(drop->mimeData());
            if (!files.isEmpty()) { drop->setDropAction(Qt::CopyAction); drop->accept(); submit(files, qobject_cast<QWidget *>(object)->window()); }
            return true;
        }
        return QObject::eventFilter(object, event);
    }
    void show() {
        if (dialog_) { dialog_->show(); dialog_->raise(); dialog_->activateWindow(); return; }
        auto *dialog = new QDialog; dialog_ = dialog; dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setObjectName(QStringLiteral("fileTransfersDialog")); dialog->setWindowTitle(QStringLiteral("File Transfers"));
        connect(session_, &QObject::destroyed, dialog, [dialog] { delete dialog; });
        auto *layout = new QVBoxLayout(dialog);
        layout->addWidget(new QLabel(QStringLiteral("The guest agent chooses the destination folder. Closing this window leaves transfers running."), dialog));
        auto *status = new QLabel(dialog); layout->addWidget(status);
        auto *tree = new QTreeWidget(dialog); tree->setHeaderLabels({QStringLiteral("File"), QStringLiteral("State"), QStringLiteral("Progress"), QStringLiteral("Details")}); layout->addWidget(tree);
        auto *cancel = new QPushButton(QStringLiteral("Cancel Selected"), dialog);
        auto *all = new QPushButton(QStringLiteral("Cancel All"), dialog);
        auto *clear = new QPushButton(QStringLiteral("Clear Finished"), dialog);
        auto *close = new QPushButton(QStringLiteral("Close"), dialog);
        auto *buttons = new QHBoxLayout; for (auto *b : {cancel, all, clear, close}) buttons->addWidget(b); layout->addLayout(buttons);
        auto update = [=, this] {
            const quint64 selected = tree->currentItem() ? tree->currentItem()->data(0, Qt::UserRole).toULongLong() : 0;
            tree->clear(); bool pending = false;
            const QStringList names{QStringLiteral("Queued"), QStringLiteral("Sending"), QStringLiteral("Cancelling"), QStringLiteral("Completed"), QStringLiteral("Failed"), QStringLiteral("Cancelled")};
            for (const auto &row : session_->fileTransfers()) {
                const bool active = row.state == QtSpice::FileTransferInfo::State::Queued || row.state == QtSpice::FileTransferInfo::State::Sending;
                pending |= active;
                const QString progress = QStringLiteral("%1 / %2 bytes (%3%)").arg(row.transferredBytes).arg(row.totalBytes).arg(row.totalBytes ? int(100.0 * row.transferredBytes / row.totalBytes) : row.state == QtSpice::FileTransferInfo::State::Completed ? 100 : 0);
                auto *item = new QTreeWidgetItem(tree, {row.filename, names.value(int(row.state)), progress, row.error});
                item->setToolTip(0, row.sourcePath); item->setData(0, Qt::UserRole, QVariant::fromValue(row.id)); item->setData(0, Qt::UserRole + 1, active);
                if (row.id == selected) tree->setCurrentItem(item);
            }
            cancel->setEnabled(tree->currentItem() && tree->currentItem()->data(0, Qt::UserRole + 1).toBool()); all->setEnabled(pending);
            status->setText(session_->fileTransferReady() ? QStringLiteral("Ready to send files") : session_->fileTransferReadinessReason());
        };
        connect(tree, &QTreeWidget::itemSelectionChanged, dialog, [=] { cancel->setEnabled(tree->currentItem() && tree->currentItem()->data(0, Qt::UserRole + 1).toBool()); });
        connect(session_, &QtSpice::Session::fileTransfersChanged, dialog, update);
        connect(session_, &QtSpice::Session::fileTransferReadinessChanged, dialog, update);
        connect(session_, &QtSpice::Session::stateChanged, dialog, update);
        connect(cancel, &QPushButton::clicked, dialog, [=, this] { if (tree->currentItem()) session_->cancelFileTransfer(tree->currentItem()->data(0, Qt::UserRole).toULongLong()); });
        connect(all, &QPushButton::clicked, session_, &QtSpice::Session::cancelAllFileTransfers);
        connect(clear, &QPushButton::clicked, session_, &QtSpice::Session::clearFinishedFileTransfers);
        connect(close, &QPushButton::clicked, dialog, &QDialog::close);
        update(); dialog->resize(850, 380); dialog->show();
    }
};
