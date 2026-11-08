// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <QString>

inline bool isVvDownload(const QString &suggestedName) {
    return suggestedName.endsWith(QStringLiteral(".vv"), Qt::CaseInsensitive) &&
           !suggestedName.contains(QLatin1Char('/')) && !suggestedName.contains(QLatin1Char('\\'));
}
