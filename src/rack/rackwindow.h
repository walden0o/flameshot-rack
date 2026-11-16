// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#pragma once

#include "rackcontentmodel.h"
#include "rackentry.h"
#include "rackhoverpolicy.h"

#include <QPropertyAnimation>
#include <QSet>
#include <QTimer>
#include <QWidget>
#include <optional>

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QToolButton;
class QScreen;

// Frameless always-on-top strip: a thin capsule handle at the top screen edge
// that expands into a clothesline of screenshot cards on hover.
class RackWindow final : public QWidget
{
    Q_OBJECT
public:
    RackWindow(RackContentModel& model, RackContentModel& clipboardModel);

    enum class View
    {
        Screenshots,
        Clipboard
    };

    void setClipboardHistoryEnabled(bool enabled);
    void setView(View view);
    [[nodiscard]] View view() const;
    void showClipboardHistory();
    void setDisplayMode(const QString& mode);
    void setCaptureActive(const QString& source, bool active);
    void toggle();
    void expand(bool activate = false);
    void dismiss();
    void collapse();
    [[nodiscard]] bool expanded() const;

signals:
    void copyRequested(const RackEntry& entry);
    void settingsRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void changeEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    [[nodiscard]] RackContentModel& currentModel() const;
    [[nodiscard]] bool interacting() const;
    [[nodiscard]] RackInteractionState interactionState() const;
    [[nodiscard]] QRect targetGeometry(bool expanded) const;
    [[nodiscard]] bool pointerInHoverArea(const QPoint& cursor) const;
    void animate(bool expanded);
    void trackPointer();
    void refresh();
    [[nodiscard]] std::optional<RackEntry> entry(const QString& id) const;
    QString exportEntry(const RackEntry& value);
    void dragEntry(const QString& id);
    void openEntry(const RackEntry& selected);
    void contextMenu(const QPoint& point);
    void finishInteraction();
    void retranslateUi();

    RackContentModel& m_model;
    RackContentModel& m_clipboardModel;
    QScreen* m_screen;
    View m_view = View::Screenshots;
    QString m_mode = QStringLiteral("hover");
    bool m_expanded = false;
    bool m_clipboardEnabled = false;
    bool m_refreshPending = false;
    int m_interactionDepth = 0;
    QSet<QString> m_captureSources;
    QWidget* m_body;
    QLabel* m_title;
    QLabel* m_empty;
    QLabel* m_hint;
    QToolButton* m_settings;
    QToolButton* m_close;
    QToolButton* m_screenshotsTab;
    QToolButton* m_clipboardTab;
    QToolButton* m_clearHistory;
    QLineEdit* m_search;
    QListWidget* m_list;
    QTimer m_closeTimer;
    QTimer m_pointerTimer;
    QPropertyAnimation m_animation;
};
