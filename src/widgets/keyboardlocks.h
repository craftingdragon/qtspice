#pragma once
#include <QObject>
#include <functional>
#include <memory>
class QWindow;
namespace QtSpice {
struct LockSnapshot {
    unsigned locks = 0;
    unsigned known = 0;
    bool operator==(const LockSnapshot &) const = default;
};
class KeyboardLocks final : public QObject {
public:
    explicit KeyboardLocks(QObject *parent = nullptr);
    ~KeyboardLocks() override;
    LockSnapshot snapshot(QWindow *window);
    bool available() const;
    std::function<void()> changed;
private:
    struct Native;
    std::unique_ptr<Native> native_;
};
}
