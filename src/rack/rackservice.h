// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#pragma once

#include "rackcontentmodel.h"

#include <QObject>

class QImage;
class RackWindow;
struct RackEntry;

// Owns the rack window and its models. Lives inside the flameshot daemon so
// hung screenshots survive the `flameshot gui` process that produced them.
class RackService final : public QObject
{
    Q_OBJECT
public:
    static RackService* instance();

    // Creates the window and shows the collapsed capsule. Called once by the
    // daemon; the service exists regardless so screenshots received over D-Bus
    // are never dropped.
    void ensureStarted();

    void addScreenshot(const QImage& image);
    void setEnabled(bool enabled);
    void setClipboardHistoryEnabled(bool enabled);
    [[nodiscard]] bool enabled() const;
    [[nodiscard]] RackWindow* window() const;
    [[nodiscard]] RackContentModel* screenshotsModel();
    [[nodiscard]] RackContentModel* clipboardModel();
    [[nodiscard]] QWidget* hostWidget() const;

signals:
    void settingsRequested();

private:
    explicit RackService(QObject* parent = nullptr);
    void copyToClipboard(const RackEntry& entry);
    void onClipboardChanged();
    void setClipboardBlocked(bool blocked);

    RackContentModel m_model{ 30 };
    RackContentModel m_clipboardModel{ 100 };
    RackWindow* m_window;
    bool m_enabled;
    bool m_clipboardBlocked;
    bool m_storageLoaded;

    static RackService* m_instance;
};
