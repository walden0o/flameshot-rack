// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#pragma once

#include "rackentry.h"

#include <QObject>
#include <QVector>

// In-memory model for rack screenshots and clipboard history. Persistence
// lives in RackService; the model only knows how to restore entries and
// trims itself to the byte cap.
class RackContentModel final : public QObject
{
    Q_OBJECT
public:
    static constexpr qint64 MaximumEntryBytes = 32 * 1024 * 1024;
    static constexpr qint64 MaximumBytes = 128 * 1024 * 1024;

    explicit RackContentModel(int limit = 100, QObject* parent = nullptr);

    [[nodiscard]] const QVector<RackEntry>& entries() const;
    [[nodiscard]] QVector<RackEntry> search(const QString& query) const;
    [[nodiscard]] int limit() const;
    [[nodiscard]] bool contains(const QString& id) const;

    bool add(QString text, QImage image = {});
    // Bulk-load persisted entries (newest first) without re-hashing.
    void restore(QVector<RackEntry> entries);
    void setLimit(int limit);
    void remove(const QString& id);
    void clear();

signals:
    void changed();

private:
    void trim();

    QVector<RackEntry> m_entries;
    int m_limit;
};
