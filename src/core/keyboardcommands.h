// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <QSet>
#include <QtGlobal>

namespace QtSpice {
struct KeyboardReadiness {
    bool opened = false;
    bool initialized = false;
    bool ready() const { return opened && initialized; }
};
// Stateful dispatch used by the worker; callbacks are the transport boundary.
template<class Send>
void dispatchCtrlAltDelete(bool ready, QSet<quint32> &held, Send send) {
    if (!ready) return;
    for (quint32 key : std::as_const(held)) send(key, false);
    held.clear();
    for (quint32 key : {0x1du, 0x38u, 0x153u}) send(key, true);
    for (quint32 key : {0x153u, 0x38u, 0x1du}) send(key, false);
}
inline unsigned mergedKeyLocks(unsigned guest, unsigned locks, unsigned known) {
    known &= 7u;
    return (guest & ~known) | (locks & known);
}
}
