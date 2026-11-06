#include "../src/core/keyboardcommands.h"
#include "../src/widgets/locksynchronizer.h"
#include "../src/widgets/focusedlocks.h"
#include <QtSpice/QSpiceDisplay.h>
#include <QtSpice/QSpiceSession.h>
#include <QtTest>
#ifdef QTSPICE_TEST_XKB
#include "../src/widgets/xkblocks.h"
#endif
using namespace QtSpice;
class KeyboardOptionsTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void ctrlAltDelete() {
        QList<QPair<quint32, bool>> messages;
        QSet<quint32> held{0x2a, 0x1e};
        const auto send = [&](quint32 code, bool down) { messages.append({code, down}); };
        dispatchCtrlAltDelete(false, held, send);
        QVERIFY(messages.isEmpty());
        QCOMPARE(held.size(), 2);
        dispatchCtrlAltDelete(true, held, send);
        QVERIFY(held.isEmpty());
        QCOMPARE(messages.size(), 8);
        QVERIFY(messages.first() == qMakePair(0x2au, false) || messages.first() == qMakePair(0x1eu, false));
        QVERIFY(messages[0] != messages[1]);
        const QList<QPair<quint32, bool>> expected{{0x1d, true}, {0x38, true}, {0x153, true},
                                                {0x153, false}, {0x38, false}, {0x1d, false}};
        QCOMPARE(messages.mid(2), expected);
        messages.clear();
        dispatchCtrlAltDelete(true, held, send);
        QCOMPARE(messages, expected);
        QCOMPARE(xtScanCode(Qt::Key_Delete), 0x153u);
        QCOMPARE(xtScanCode(Qt::Key_CapsLock), 0x3au);
        QCOMPARE(xtScanCode(Qt::Key_NumLock), 0x45u);
        QCOMPARE(xtScanCode(Qt::Key_ScrollLock), 0x46u);
    }
    void inputInitializationOrdering() {
        KeyboardReadiness readiness;
        QVERIFY(!readiness.ready());
        readiness.opened = true;
        QVERIFY(!readiness.ready());
        readiness.initialized = true;
        QVERIFY(readiness.ready());
        readiness = {}; // channel teardown
        readiness.initialized = true;
        QVERIFY(!readiness.ready());
        readiness.opened = true;
        QVERIFY(readiness.ready());
    }
    void partialLockMasks() {
        QCOMPARE(int(Session::KeyboardLock::ScrollLock), 1);
        QCOMPARE(int(Session::KeyboardLock::NumLock), 2);
        QCOMPARE(int(Session::KeyboardLock::CapsLock), 4);
        for (unsigned guest = 0; guest < 8; ++guest)
            for (unsigned local = 0; local < 8; ++local)
                for (unsigned known = 0; known < 8; ++known) {
                    const auto merged = mergedKeyLocks(guest, local, known);
                    QCOMPARE(merged & known, local & known);
                    QCOMPARE(merged & ~known, guest & ~known);
                }
        QCOMPARE(mergedKeyLocks(0x80, 0xff, 0xff), 0x87u);
    }
    void synchronizationLifecycle() {
        LockSynchronizer one, two;
        QList<unsigned> requests;
        const auto send = [&](LockSnapshot value) { requests.append(value.locks); };
        one.apply({4, 7}, false, send); // disabled/unfocused/disconnected/suspended
        one.apply({}, true, send); // unknown state is never guessed
        QVERIFY(requests.isEmpty());
        one.apply({4, 7}, true, send);
        one.apply({4, 7}, true, send);
        QCOMPARE(requests, QList<unsigned>{4});
        one.apply({6, 7}, true, send); // native lock update
        QCOMPARE(requests.last(), 6u);
        one.reset(); // raw lock press, focus entry or reconnect
        one.apply({6, 7}, true, send);
        QCOMPARE(requests.size(), 3);
        one.apply({6, 7}, false, send);
        one.apply({6, 7}, true, send); // resume must resend
        QCOMPARE(requests.size(), 4);
        two.apply({6, 7}, true, send); // independent console
        QCOMPARE(requests.size(), 5);
        one.apply({0, 4}, true, send);
        QCOMPARE(requests.last(), 0u);
    }
    void waylandFocusAndKeymapLifecycle() {
        FocusedLocks state;
        int first = 0, second = 0;
        state.update({4, 7}); // Modifiers without focus cannot leak into a console.
        QCOMPARE(state.forSurface(&first).known, 0u);
        state.enter(&first);
        QCOMPARE(state.forSurface(&first).known, 0u);
        state.update({4, 7});
        QCOMPARE(state.forSurface(&first).locks, 4u);
        QCOMPARE(state.forSurface(&second).known, 0u);
        state.invalidate(); // New keymap requires fresh modifiers.
        QCOMPARE(state.forSurface(&first).known, 0u);
        state.update({2, 7});
        state.leave();
        QCOMPARE(state.forSurface(&first).known, 0u);
        state.enter(&second);
        QCOMPARE(state.forSurface(&second).known, 0u);
        state.update({0, 4});
        QCOMPARE(state.forSurface(&second).known, 4u);
        QCOMPARE(state.forSurface(&second).locks, 0u);
    }
    void nativeLifecycle() {
        KeyboardLocks source;
        QVERIFY(!source.snapshot(nullptr).known);
        if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
            QVERIFY(!source.available());
            QWindow window;
            QVERIFY(!source.snapshot(&window).known);
        }
    }
#ifdef QTSPICE_TEST_XKB
    void nativeXkbDecoding() {
        auto *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        QVERIFY(context);
        auto cleanupContext = qScopeGuard([&] { xkb_context_unref(context); });
        xkb_rule_names names{};
        names.layout = "us";
        names.options = "scrolllock:mod3";
        auto *keymap = xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
        QVERIFY(keymap);
        auto cleanupKeymap = qScopeGuard([&] { xkb_keymap_unref(keymap); });
        auto *state = xkb_state_new(keymap);
        QVERIFY(state);
        auto cleanupState = qScopeGuard([&] { xkb_state_unref(state); });
        QCOMPARE(xkbLocks(nullptr).known, 0u);
        QCOMPARE(xkbLocks(state).known, 7u);
        for (unsigned combination = 0; combination < 8; ++combination) {
            xkb_mod_mask_t mask = 0;
            for (auto [name, bit] : {std::pair{"Mod3", 1u}, std::pair{"Mod2", 2u}, std::pair{"Lock", 4u}}) {
                const auto index = xkb_keymap_mod_get_index(keymap, name);
                QVERIFY(index != XKB_MOD_INVALID);
                if (combination & bit) mask |= xkb_mod_mask_t(1) << index;
            }
            xkb_state_update_mask(state, 0, 0, mask, 0, 0, 0);
            QCOMPARE(xkbLocks(state).locks, combination);
        }
    }
#endif
};
QTEST_MAIN(KeyboardOptionsTest)
#include "keyboardoptions_test.moc"
