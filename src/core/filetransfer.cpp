// SPDX-License-Identifier: LGPL-3.0-or-later
#include "filetransfer.h"
#include <QFileInfo>
#include <QLoggingCategory>
#include <QTimer>
Q_LOGGING_CATEGORY(transferLog, "qtspice.filetransfer", QtWarningMsg)
namespace QtSpice {
using S = FileTransferInfo::State;
static bool terminal(S s) { return s == S::Completed || s == S::Failed || s == S::Cancelled; }
FileTransferController::~FileTransferController() { detach(); life->self = nullptr; }
void FileTransferController::changed() { Q_EMIT owner->fileTransfersChanged(); }
void FileTransferController::attach(SpiceMainChannel *main) {
    channel = main;
    operation = [this](QString path, Progress progress, Completion done) {
        struct Request {
            Progress progress; Completion done; QString detail; SpiceFileTransferTask *task = nullptr;
            ~Request() { if (task) { g_signal_handlers_disconnect_by_data(task, this); g_object_unref(task); } }
        };
        auto *request = new Request{std::move(progress), std::move(done)};
        pending = g_cancellable_new();
        GFile *files[] = {g_file_new_for_path(path.toUtf8().constData()), nullptr};
        const auto handler = g_signal_connect(channel, "new-file-transfer", G_CALLBACK(+[](SpiceMainChannel *, SpiceFileTransferTask *task, gpointer data) {
            auto *request = static_cast<Request *>(data);
            request->task = SPICE_FILE_TRANSFER_TASK(g_object_ref(task));
            g_signal_connect(task, "finished", G_CALLBACK(+[](SpiceFileTransferTask *, GError *error, gpointer data) {
                if (error) static_cast<Request *>(data)->detail = QString::fromUtf8(error->message);
            }), request);
        }), request);
        spice_main_channel_file_copy_async(channel, files, G_FILE_COPY_NONE, pending,
            +[](goffset bytes, goffset total, gpointer data) {
                static_cast<Request *>(data)->progress(qMax<goffset>(0, bytes), qMax<goffset>(0, total));
            }, request, +[](GObject *source, GAsyncResult *result, gpointer data) {
                std::unique_ptr<Request> request(static_cast<Request *>(data));
                GError *error = nullptr;
                const bool ok = spice_main_channel_file_copy_finish(SPICE_MAIN_CHANNEL(source), result, &error);
                request->done(ok ? QString() : !request->detail.isEmpty() ? request->detail : error ? QString::fromUtf8(error->message) : QStringLiteral("File transfer failed"));
                g_clear_error(&error);
            }, request);
        g_signal_handler_disconnect(channel, handler);
        g_object_unref(files[0]);
    };
    cancellation = [this] { if (pending) g_cancellable_cancel(pending); };
}
void FileTransferController::availability(bool value, QString why) {
    if (ready == value && reason == why) return;
    ready = value; reason = std::move(why);
    if (!ready) {
        for (auto &row : rows) if (row.state == S::Queued || row.state == S::Sending || row.state == S::Cancelling) row.error = reason;
        cancelAll();
        changed();
    }
    Q_EMIT owner->fileTransferReadinessChanged();
}
void FileTransferController::detach() {
    availability(false, QStringLiteral("Console disconnected"));
    cancelAll(); ++serial; active = 0;
    for (auto &row : rows) if (row.state == S::Cancelling) row.state = S::Cancelled;
    if (pending) { g_object_unref(pending); pending = nullptr; }
    channel = nullptr; operation = {}; cancellation = {}; changed();
}
bool FileTransferController::send(const QStringList &paths, QString *error) {
    auto fail = [&](QString why) { if (error) *error = why; return false; };
    if (!ready) return fail(reason);
    if (paths.isEmpty()) return fail(QStringLiteral("Select at least one file"));
    for (const auto &path : paths) {
        QFileInfo info(path);
        if (!info.isAbsolute() || !info.isFile() || !info.isReadable())
            return fail(QStringLiteral("Not a readable local regular file: %1").arg(path));
    }
    if (error) error->clear();
    for (const auto &path : paths) {
        QFileInfo info(path); FileTransferInfo row;
        row.id = ++nextId; row.sourcePath = info.absoluteFilePath(); row.filename = info.fileName(); row.totalBytes = info.size(); rows.append(row);
    }
    changed(); start(); return true;
}
void FileTransferController::start() {
    if (active || !ready || !operation) return;
    for (auto &row : rows) {
        if (row.state != S::Queued) continue;
        QFileInfo file(row.sourcePath);
        if (!file.isFile() || !file.isReadable()) { row.state = S::Failed; row.error = QStringLiteral("Source file is no longer readable"); changed(); continue; }
        active = row.id; row.state = S::Sending;
        const auto id = active, attempt = ++serial; auto token = life;
        const QString path = row.sourcePath;
        changed();
        operation(path, [token, id, attempt](quint64 bytes, quint64 total) {
            auto *self = token->self; if (!self || self->serial != attempt || self->active != id) return;
            for (auto &r : self->rows) if (r.id == id) { r.transferredBytes = bytes; r.totalBytes = total; break; }
            self->changed();
        }, [token, id, attempt](QString error) {
            auto *self = token->self; if (!self || self->serial != attempt || self->active != id) return;
            if (self->pending) { g_object_unref(self->pending); self->pending = nullptr; }
            for (auto &r : self->rows) if (r.id == id) {
                if (r.state == S::Cancelling) r.state = S::Cancelled;
                else { r.state = error.isEmpty() ? S::Completed : S::Failed; r.error = error; if (!error.isEmpty()) qCWarning(transferLog) << error; }
                break;
            }
            self->active = 0; self->changed();
            QTimer::singleShot(0, self->owner, [token] { if (token->self) token->self->start(); });
        });
        return;
    }
}
void FileTransferController::cancel(quint64 id) {
    for (auto &row : rows) if (row.id == id) {
        if (row.state == S::Queued) row.state = S::Cancelled;
        else if (row.state == S::Sending) { row.state = S::Cancelling; if (cancellation) cancellation(); }
        break;
    }
    changed();
}
void FileTransferController::cancelAll() {
    for (auto &row : rows) if (row.state == S::Queued) row.state = S::Cancelled;
    if (active) cancel(active); else changed();
}
void FileTransferController::clearFinished() {
    rows.removeIf([](const auto &row) { return terminal(row.state); }); changed();
}
}
