// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QImage>
#include <QString>

class QMimeData;

// True when the producer marked the content as off-limits to clipboard
// history tools (password managers, Klipper hints and friends).
[[nodiscard]] bool clipboardExcludedFromHistory(const QMimeData& data);

struct RackEntry
{
    QString id;
    QString text;
    QImage image;
    QByteArray digest;
    QDateTime createdUtc;

    [[nodiscard]] qint64 byteSize() const
    {
        return image.sizeInBytes() + text.size() * qint64(sizeof(QChar));
    }
};
