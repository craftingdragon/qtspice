#pragma once
#include "clipboardcontent.h"
#include <QObject>
#include <QPointer>
#include <QThreadPool>
#include <QMap>
#include <QQueue>
#include <functional>

namespace QtSpice {
// All protocol transitions run on the owning Qt/GLib thread. Only codecs run in the pool.
class ClipboardTransfer final : public QObject {
public:
    std::function<void(QList<quint32>)> grab;
    std::function<void()> release;
    std::function<void(unsigned)> request;
    std::function<void(unsigned, QByteArray)> notify;
    std::function<void(ClipboardContent)> received;
    std::function<QList<QByteArray>()> readers = [] { return QImageReader::supportedImageFormats(); };
    std::function<QList<QByteArray>()> writers = [] { return QImageWriter::supportedImageFormats(); };
    explicit ClipboardTransfer(QObject *parent = nullptr) : QObject(parent) { pool.setMaxThreadCount(1); }
    ~ClipboardTransfer() override { pool.clear(); encoding.clear(); encodingBusy = false; pool.waitForDone(); }
    void setMode(Session::ClipboardMode mode) {
        const bool send = mode == Session::ClipboardMode::Bidirectional || mode == Session::ClipboardMode::HostToGuest;
        const bool receive = mode == Session::ClipboardMode::Bidirectional || mode == Session::ClipboardMode::GuestToHost;
        if (sendAllowed && !send) {
            ++encodeGeneration; encoding.clear(); encodingBusy = false;
            const bool owned = owns; owns = false; host = {}; cache.clear();
            if (active && owned) release();
        }
        if (receiveAllowed && !receive) {
            ++generation; candidates.clear(); incoming = {};
        }
        sendAllowed = send; receiveAllowed = receive;
    }
    void reset(bool ready) {
        ++generation; ++encodeGeneration; pool.clear(); encoding.clear(); encodingBusy = false; active = ready; owns = false; pending = 0; incoming = {}; candidates.clear(); cache.clear();
        if (!ready) host = {};
        else if (host.text || !host.image.isNull()) setHost(host);
    }
    void setHost(ClipboardContent content) {
        if (!sendAllowed) return;
        ++generation; ++encodeGeneration; pool.clear(); encoding.clear(); encodingBusy = false; candidates.clear(); incoming = {}; cache.clear();
        if (content.text && content.text->toUtf8().size() > 4 * 1024 * 1024) content.text.reset();
        if (!clipboardImageSize(content.image.size()) || content.image.sizeInBytes() > 256LL * 1024 * 1024) content.image = {};
        host = content;
        if (!active) return;
        QList<quint32> types;
        if (host.text && !host.text->isEmpty()) types << VD_AGENT_CLIPBOARD_UTF8_TEXT;
        const auto writable = writers();
        if (!host.image.isNull()) for (auto type : imageTypes()) if (writable.contains(clipboardFormat(type))) types << type;
        const bool previouslyOwned = owns;
        owns = !types.isEmpty();
        if (owns) grab(types); else if (previouslyOwned) release();
    }
    bool offer(const QList<quint32> &types) {
        if (!active || !receiveAllowed) return false;
        ++generation; ++encodeGeneration; pool.clear(); encoding.clear(); encodingBusy = false; owns = false; host = {}; cache.clear(); incoming = {}; candidates.clear();
        if (types.contains(VD_AGENT_CLIPBOARD_UTF8_TEXT)) candidates.enqueue(VD_AGENT_CLIPBOARD_UTF8_TEXT);
        const auto readable = readers();
        for (auto type : imageTypes()) if (types.contains(type) && readable.contains(clipboardFormat(type))) candidates.enqueue(type);
        if (candidates.isEmpty()) return false;
        const auto serial = generation;
        QMetaObject::invokeMethod(this, [this, serial] { if (generation == serial && !pending) next(); }, Qt::QueuedConnection);
        return true;
    }
    void guestRelease() { if (!receiveAllowed || owns) return; ++generation; ++encodeGeneration; pool.clear(); encoding.clear(); encodingBusy = false; candidates.clear(); incoming = {}; }
    bool hostRequest(unsigned type) {
        if (!active || !sendAllowed || !owns) return false;
        if (type == VD_AGENT_CLIPBOARD_UTF8_TEXT && host.text) { notify(type, host.text->toUtf8()); return true; }
        if (host.image.isNull() || clipboardFormat(type).isEmpty()) { notify(VD_AGENT_CLIPBOARD_NONE, {}); return true; }
        if (cache.contains(type)) { notify(type, cache.value(type)); return true; }
        // Duplicate requests share the single encode job.
        if (encoding.contains(type)) { ++encoding[type]; return true; }
        encoding[type] = 1;
        startEncoding();
        return true;
    }
    void data(unsigned type, QByteArray bytes) {
        if (!active || !pending || (type != pending && type != VD_AGENT_CLIPBOARD_NONE)) return;
        const auto expected = pending; pending = 0;
        if (!receiveAllowed || requestGeneration != generation) { next(); return; }
        if (type == VD_AGENT_CLIPBOARD_NONE || bytes.size() > clipboardImageLimit) { next(); return; }
        if (expected == VD_AGENT_CLIPBOARD_UTF8_TEXT) {
            if (bytes.size() <= 4 * 1024 * 1024) { incoming.text = QString::fromUtf8(bytes); received(incoming); }
            next(); return;
        }
        const auto serial = generation; QPointer<ClipboardTransfer> guard(this);
        pool.start([guard, serial, bytes, expected] {
            const auto image = decodeClipboardImage(bytes, clipboardFormat(expected));
            if (guard) QMetaObject::invokeMethod(guard, [guard, serial, image] {
                if (!guard || guard->generation != serial || !guard->receiveAllowed || guard->owns) return;
                if (image.isNull()) { guard->next(); return; }
                guard->incoming.image = image; guard->candidates.clear(); guard->received(guard->incoming);
            }, Qt::QueuedConnection);
        });
    }
private:
    void startEncoding() {
        if (encodingBusy || encoding.isEmpty() || !active || !owns) return;
        encodingBusy = true;
        const auto type = encoding.firstKey();
        const auto serial = encodeGeneration; const auto image = host.image;
        QPointer<ClipboardTransfer> guard(this);
        pool.start([guard, serial, image, type] {
            const auto bytes = encodeClipboardImage(image, type);
            if (guard) QMetaObject::invokeMethod(guard, [guard, serial, type, bytes] {
                if (!guard || guard->encodeGeneration != serial || !guard->sendAllowed || !guard->owns) return;
                const int count = guard->encoding.take(type);
                if (!bytes.isEmpty()) guard->cache[type] = bytes;
                for (int i = 0; i < count; ++i) guard->notify(bytes.isEmpty() ? VD_AGENT_CLIPBOARD_NONE : type, bytes);
                if (guard->encodeGeneration != serial) return;
                guard->encodingBusy = false;
                guard->startEncoding();
            }, Qt::QueuedConnection);
        });
    }
    bool encodingBusy = false;
    static QList<quint32> imageTypes() { return {VD_AGENT_CLIPBOARD_IMAGE_PNG, VD_AGENT_CLIPBOARD_IMAGE_TIFF, VD_AGENT_CLIPBOARD_IMAGE_BMP, VD_AGENT_CLIPBOARD_IMAGE_JPG}; }
    void next() { if (active && receiveAllowed && !pending && !owns && !candidates.isEmpty()) { pending = candidates.dequeue(); requestGeneration = generation; request(pending); } }
    QThreadPool pool;
    quint64 generation = 0, requestGeneration = 0, encodeGeneration = 0;
    bool sendAllowed = true, receiveAllowed = true;
    bool active = false, owns = false;
    unsigned pending = 0;
    ClipboardContent host, incoming;
    QQueue<quint32> candidates;
    QMap<unsigned, QByteArray> cache;
    QMap<unsigned, int> encoding;
};
}
