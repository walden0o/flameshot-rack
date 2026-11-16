// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "rackwindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QDrag>
#include <QEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QStandardPaths>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

namespace {
class RackList final : public QListWidget
{
public:
    using QListWidget::QListWidget;
    std::function<void(const QString&)> drag;

protected:
    void startDrag(Qt::DropActions) override
    {
        if (currentItem() && drag) {
            drag(currentItem()->data(Qt::UserRole).toString());
        }
    }
};

// Screenshots hang like photos on a clothesline: a pin and string drop from
// the rope (painted by the window) to a slightly tilted paper card.
inline constexpr int RackRopeOffset = 3;
inline constexpr int RackPinLength = 11;

class RackCardDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter,
               const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        const auto* view = qobject_cast<const QListView*>(option.widget);
        if (!view || view->viewMode() != QListView::IconMode) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const QRect cell = option.rect;
        const qreal centerX = cell.x() + cell.width() / 2.0;
        const QPointF ropePoint(centerX, cell.top() + RackRopeOffset);
        const QPointF pinPoint(centerX, ropePoint.y() + RackPinLength);

        const QRectF cardBounds(
          0, 0, cell.width() - 26, cell.height() - RackPinLength - 14);
        // Alternate the tilt so neighbouring cards hang at slightly different
        // angles.
        const qreal tilt = (index.row() % 4 - 1.5) * 1.6;

        QPen pinString(option.palette.color(QPalette::Mid), 1.0);
        pinString.setCapStyle(Qt::RoundCap);
        painter->setPen(pinString);
        painter->drawLine(ropePoint, pinPoint);

        painter->translate(pinPoint);
        painter->rotate(tilt);
        painter->translate(-cardBounds.width() / 2.0, 0);
        const QRectF paper(0, 0, cardBounds.width(), cardBounds.height());

        QColor shadow = option.palette.color(QPalette::Shadow);
        shadow.setAlpha(70);
        painter->setPen(Qt::NoPen);
        painter->setBrush(shadow);
        painter->drawRoundedRect(paper.translated(0, 2.5), 7, 7);

        const bool selected = option.state & QStyle::State_Selected;
        const bool hovered = option.state & QStyle::State_MouseOver;
        QLinearGradient paperGrad(paper.topLeft(), paper.bottomLeft());
        paperGrad.setColorAt(0, option.palette.color(QPalette::Base));
        paperGrad.setColorAt(1,
                             option.palette.color(QPalette::Base).darker(104));
        painter->setBrush(paperGrad);
        painter->setPen(QPen(selected
                               ? option.palette.color(QPalette::Highlight)
                               : option.palette.color(QPalette::Mid),
                             selected ? 1.6 : 1.0));
        painter->drawRoundedRect(paper, 6, 6);
        if (hovered && !selected) {
            QColor tint = option.palette.color(QPalette::Highlight);
            tint.setAlpha(20);
            painter->setPen(Qt::NoPen);
            painter->setBrush(tint);
            painter->drawRoundedRect(paper, 6, 6);
        }

        const QRectF imageRect = paper.adjusted(5, 5, -5, -20);
        const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
        const QPixmap thumbnail =
          icon.isNull() ? QPixmap() : icon.pixmap(QSize(288, 200));
        if (!thumbnail.isNull()) {
            const QSizeF fit = QSizeF(thumbnail.size())
                                 .scaled(imageRect.size(), Qt::KeepAspectRatio);
            const QRectF thumbRect((paper.center().x() - fit.width() / 2.0),
                                   imageRect.top() +
                                     (imageRect.height() - fit.height()) / 2.0,
                                   fit.width(),
                                   fit.height());
            painter->setPen(Qt::NoPen);
            painter->drawPixmap(thumbRect, thumbnail, QRectF(thumbnail.rect()));
        }
        painter->setPen(option.palette.color(QPalette::WindowText));
        painter->drawText(QRectF(4, paper.bottom() - 16, paper.width() - 8, 13),
                          Qt::AlignCenter,
                          index.data(Qt::DisplayRole).toString());
        painter->restore();

        // Pin stays upright regardless of card tilt, clipped over the card
        // edge.
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        painter->setBrush(option.palette.color(QPalette::Highlight));
        painter->drawRoundedRect(
          QRectF(pinPoint.x() - 4, pinPoint.y() - 4, 8, 15), 3, 3);
        painter->restore();
    }
};
} // namespace

RackWindow::RackWindow(RackContentModel& model,
                       RackContentModel& clipboardModel)
  : QWidget(nullptr,
            Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
  , m_model(model)
  , m_clipboardModel(clipboardModel)
  , m_screen(QGuiApplication::primaryScreen())
  , m_animation(this, "geometry")
{
    setObjectName(QStringLiteral("rack-window"));
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_QuitOnClose, false);
    m_body = new QWidget(this);
    auto* layout = new QVBoxLayout(m_body);
    layout->setContentsMargins(8, 4, 8, 8);
    layout->setSpacing(8);
    auto* header = new QHBoxLayout;
    header->setSpacing(4);
    m_settings = new QToolButton(m_body);
    m_close = new QToolButton(m_body);
    m_settings->setObjectName(QStringLiteral("rack-settings"));
    m_close->setObjectName(QStringLiteral("rack-collapse"));
    m_screenshotsTab = new QToolButton(m_body);
    m_clipboardTab = new QToolButton(m_body);
    m_clearHistory = new QToolButton(m_body);
    m_screenshotsTab->setObjectName(QStringLiteral("rack-screenshots-tab"));
    m_clipboardTab->setObjectName(QStringLiteral("rack-clipboard-tab"));
    m_clearHistory->setObjectName(QStringLiteral("rack-clear-clipboard"));
    m_screenshotsTab->setCheckable(true);
    m_clipboardTab->setCheckable(true);
    m_screenshotsTab->setAutoExclusive(true);
    m_clipboardTab->setAutoExclusive(true);
    m_search = new QLineEdit(m_body);
    m_search->setObjectName(QStringLiteral("rack-clipboard-search"));
    m_search->setClearButtonEnabled(true);
    // macOS-style segmented control: both tabs inside one rounded track,
    // centered like a Dynamic Island pill.
    auto* tabs = new QWidget(m_body);
    tabs->setObjectName(QStringLiteral("rack-segmented"));
    auto* tabsLayout = new QHBoxLayout(tabs);
    tabsLayout->setContentsMargins(2, 2, 2, 2);
    tabsLayout->setSpacing(0);
    tabsLayout->addWidget(m_screenshotsTab);
    tabsLayout->addWidget(m_clipboardTab);
    header->addStretch();
    header->addWidget(tabs);
    header->addWidget(m_search, 1);
    header->addStretch();
    header->addWidget(m_clearHistory);
    header->addWidget(m_settings);
    header->addWidget(m_close);
    layout->addLayout(header);
    m_body->setStyleSheet(QStringLiteral(
      "#rack-segmented { background: rgba(127,127,127,45);"
      "  border-radius: 9px; }"
      "QToolButton { border: none; background: transparent;"
      "  padding: 3px 10px; border-radius: 7px; }"
      "QToolButton:hover { background: rgba(127,127,127,50); }"
      "QToolButton:checked { background: rgba(127,127,127,90); }"
      "QLineEdit { border: none; background: rgba(127,127,127,45);"
      "  border-radius: 9px; padding: 3px 10px; }"));
    auto* list = new RackList(m_body);
    m_list = list;
    list->drag = [this](const QString& id) { dragEntry(id); };
    list->setObjectName(QStringLiteral("rack-cards"));
    list->setViewMode(QListView::IconMode);
    list->setFlow(QListView::LeftToRight);
    list->setWrapping(false);
    list->setIconSize(QSize(144, 100));
    list->setGridSize(QSize(168, 140));
    list->setSpacing(4);
    list->setItemDelegate(new RackCardDelegate(list));
    list->setDragEnabled(true);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setContextMenuPolicy(Qt::CustomContextMenu);
    list->setFrameShape(QFrame::NoFrame);
    list->setMouseTracking(true);
    layout->addWidget(list, 1);
    m_empty = new QLabel(m_body);
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setWordWrap(true);
    layout->addWidget(m_empty);
    m_hint = new QLabel(m_body);
    m_hint->setWordWrap(true);
    layout->addWidget(m_hint);
    connect(
      list, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
          if (const auto selected =
                entry(item->data(Qt::UserRole).toString())) {
              emit copyRequested(*selected);
              m_hint->setText(selected->image.isNull()
                                ? tr("Text copied to clipboard")
                                : tr("Copied to clipboard"));
          }
      });
    connect(list,
            &QWidget::customContextMenuRequested,
            this,
            &RackWindow::contextMenu);
    connect(m_settings, &QAbstractButton::clicked, this, [this] {
        dismiss();
        emit settingsRequested();
    });
    connect(m_close, &QAbstractButton::clicked, this, &RackWindow::dismiss);
    connect(&m_model, &RackContentModel::changed, this, &RackWindow::refresh);
    connect(&m_clipboardModel,
            &RackContentModel::changed,
            this,
            &RackWindow::refresh);
    connect(m_screenshotsTab, &QAbstractButton::clicked, this, [this] {
        setView(View::Screenshots);
    });
    connect(m_clipboardTab, &QAbstractButton::clicked, this, [this] {
        setView(View::Clipboard);
    });
    connect(m_clearHistory, &QAbstractButton::clicked, this, [this] {
        for (const auto& value : currentModel().entries()) {
            removeExportedFile(value.id);
        }
        currentModel().clear();
    });
    connect(m_search, &QLineEdit::textChanged, this, &RackWindow::refresh);
    connect(qApp, &QGuiApplication::screenRemoved, this, [this] {
        m_screen = QGuiApplication::primaryScreen();
        m_animation.stop();
        setGeometry(targetGeometry(m_expanded));
    });
    m_animation.setDuration(RackAnimationMs);
    m_animation.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_animation, &QPropertyAnimation::finished, this, [this] {
        m_body->setVisible(m_expanded);
        if (!m_expanded) {
            updateVisibility();
        }
    });
    m_closeTimer.setSingleShot(true);
    m_closeTimer.setInterval(RackCollapseDelayMs);
    connect(&m_closeTimer, &QTimer::timeout, this, [this] {
        if (rackCollapseAfterDelay(pointerInHoverArea(QCursor::pos()),
                                   interacting())) {
            collapse();
        }
    });
    m_pointerTimer.setInterval(RackPointerPollMs);
    connect(&m_pointerTimer, &QTimer::timeout, this, &RackWindow::trackPointer);
    m_pointerTimer.start();
    setView(View::Screenshots);
    setGeometry(targetGeometry(false));
    m_body->hide();
}

void RackWindow::setClipboardHistoryEnabled(bool enabled)
{
    m_clipboardEnabled = enabled;
    refresh();
}

void RackWindow::setView(View view)
{
    m_view = view;
    if (view == View::Screenshots) {
        m_search->clear();
    }
    m_screenshotsTab->setChecked(view == View::Screenshots);
    m_clipboardTab->setChecked(view == View::Clipboard);
    retranslateUi();
}

RackWindow::View RackWindow::view() const
{
    return m_view;
}

void RackWindow::showClipboardHistory()
{
    if (m_expanded && m_view == View::Clipboard) {
        dismiss();
        return;
    }
    setView(View::Clipboard);
    expand(true);
    m_search->setFocus();
}

RackContentModel& RackWindow::currentModel() const
{
    return m_view == View::Clipboard ? m_clipboardModel : m_model;
}

void RackWindow::setDisplayMode(const QString& mode)
{
    m_mode = mode;
    if (mode == QStringLiteral("always")) {
        expand();
    } else {
        collapse();
    }
}

void RackWindow::setPosition(const QString& position)
{
    if (m_position == position) {
        return;
    }
    m_position = position;
    m_animation.stop();
    setGeometry(targetGeometry(m_expanded));
}

// Hover mode keeps the window fully hidden while collapsed: it only
// exists on screen while expanded. Always mode stays visible.
void RackWindow::updateVisibility()
{
    if (m_serviceEnabled &&
        (m_expanded || m_mode == QStringLiteral("always"))) {
        show();
    } else {
        hide();
    }
}

void RackWindow::setCaptureActive(const QString& source, bool active)
{
    if (active) {
        m_captureSources.insert(source);
    } else {
        m_captureSources.remove(source);
    }
    m_closeTimer.stop();
    if (!m_captureSources.isEmpty()) {
        m_animation.stop();
        hide();
    } else if (m_serviceEnabled) {
        m_expanded = m_mode == QStringLiteral("always");
        setGeometry(targetGeometry(m_expanded));
        m_body->setVisible(m_expanded);
        updateVisibility();
    }
}

void RackWindow::setServiceEnabled(bool enabled)
{
    m_serviceEnabled = enabled;
}

void RackWindow::toggle()
{
    if (m_expanded) {
        dismiss();
    } else {
        expand(true);
    }
}

void RackWindow::expand(bool activate)
{
    if (!m_captureSources.isEmpty()) {
        return;
    }
    m_closeTimer.stop();
    if (!m_screen) {
        m_screen = QGuiApplication::primaryScreen();
    }
    animate(true);
    show();
    if (activate) {
        raise();
        activateWindow();
        m_list->setFocus();
    }
}

RackWindow::~RackWindow()
{
    purgeExportedFiles();
}

void RackWindow::dismiss()
{
    if (m_mode == QStringLiteral("always") ||
        rackCollapseBlocked(interactionState(),
                            RackCollapseTrigger::Explicit)) {
        return;
    }
    if (m_search->hasFocus()) {
        m_search->clearFocus();
    }
    m_closeTimer.stop();
    animate(false);
}

void RackWindow::collapse()
{
    if (m_mode == QStringLiteral("always") || interacting()) {
        return;
    }
    m_closeTimer.stop();
    animate(false);
}

bool RackWindow::expanded() const
{
    return m_expanded;
}

bool RackWindow::interacting() const
{
    return rackCollapseBlocked(interactionState(),
                               RackCollapseTrigger::Automatic);
}

RackInteractionState RackWindow::interactionState() const
{
    return { .contentInteraction = m_interactionDepth > 0,
             .popupOpen = QApplication::activePopupWidget() != nullptr,
             .modalOpen = QApplication::activeModalWidget() != nullptr,
             .searchFocused = isActiveWindow() && m_search->hasFocus() };
}

QRect RackWindow::targetGeometry(bool expanded) const
{
    const QRect screen =
      m_screen ? m_screen->geometry() : QRect(0, 0, 1024, 768);
    return rackTargetGeometry(screen,
                              m_screen ? m_screen->availableGeometry() : screen,
                              expanded,
                              rackPositionFromName(m_position));
}

bool RackWindow::pointerInHoverArea(const QPoint& cursor) const
{
    return rackInHoverArea(
      geometry(), m_screen ? m_screen->geometry() : geometry(), cursor);
}

void RackWindow::animate(bool expanded)
{
    const QRect target = targetGeometry(expanded);
    if (m_expanded == expanded &&
        (m_animation.state() == QAbstractAnimation::Running ||
         geometry() == target)) {
        return;
    }
    m_expanded = expanded;
    const QRect panel = targetGeometry(true);
    m_body->setGeometry(8, 22, panel.width() - 16, panel.height() - 28);
    if (expanded) {
        m_body->show();
    }
    m_animation.stop();
    m_animation.setStartValue(geometry());
    m_animation.setEndValue(target);
    m_animation.start();
    update();
}

void RackWindow::trackPointer()
{
    if (!m_serviceEnabled || !m_captureSources.isEmpty()) {
        return;
    }
    const QPoint cursor = QCursor::pos();
    QScreen* hovered = m_expanded ? nullptr : QGuiApplication::screenAt(cursor);
    const RackPointerState state{
        .mode = rackDisplayModeFromName(m_mode),
        .expanded = m_expanded,
        .inHotZone = hovered && rackInHotZone(hovered->geometry(),
                                              hovered->availableGeometry(),
                                              cursor,
                                              rackPositionFromName(m_position)),
        .inHoverArea = m_expanded && pointerInHoverArea(cursor),
        .interacting = interacting(),
        .collapsePending = m_closeTimer.isActive()
    };
    switch (rackPointerAction(state)) {
        case RackPointerAction::Expand:
            m_screen = hovered;
            expand();
            break;
        case RackPointerAction::CancelCollapse:
            m_closeTimer.stop();
            break;
        case RackPointerAction::ScheduleCollapse:
            m_closeTimer.start();
            break;
        case RackPointerAction::None:
            break;
    }
}

void RackWindow::refresh()
{
    if (m_interactionDepth > 0) {
        m_refreshPending = true;
        return;
    }
    const QString selected =
      m_list->currentItem()
        ? m_list->currentItem()->data(Qt::UserRole).toString()
        : QString();
    m_list->clear();
    const bool clipboard = m_view == View::Clipboard;
    // Search and Clear stay put in both views so switching tabs does not
    // reflow the header.
    m_search->setEnabled(clipboard || !m_model.entries().isEmpty());
    m_search->setPlaceholderText(clipboard ? tr("Search clipboard history")
                                           : tr("Search screenshots"));
    m_clearHistory->setEnabled(clipboard ? m_clipboardEnabled
                                         : !m_model.entries().isEmpty());
    if (clipboard) {
        m_list->setViewMode(QListView::ListMode);
        m_list->setFlow(QListView::TopToBottom);
        m_list->setIconSize(QSize(48, 32));
        m_list->setGridSize({});
    } else {
        m_list->setViewMode(QListView::IconMode);
        m_list->setFlow(QListView::LeftToRight);
        m_list->setIconSize(QSize(144, 100));
        m_list->setGridSize(QSize(168, 140));
    }
    QVector<RackEntry> values;
    if (clipboard) {
        values = m_clipboardEnabled ? m_clipboardModel.search(m_search->text())
                                    : QVector<RackEntry>();
    } else {
        values = m_model.entries();
        const QString query = m_search->text().trimmed();
        if (!query.isEmpty()) {
            QVector<RackEntry> matched;
            for (const auto& value : values) {
                const QString time = value.createdUtc.toLocalTime().toString(
                  QStringLiteral("HH:mm:ss"));
                if (time.contains(query, Qt::CaseInsensitive) ||
                    value.text.contains(query, Qt::CaseInsensitive)) {
                    matched.append(value);
                }
            }
            values = matched;
        }
    }
    for (const auto& value : values) {
        auto* item = new QListWidgetItem(m_list);
        item->setData(Qt::UserRole, value.id);
        const QString time =
          value.createdUtc.toLocalTime().toString(QStringLiteral("HH:mm:ss"));
        if (value.image.isNull()) {
            const QString line = value.text.simplified();
            item->setText(QStringLiteral("%1  %2").arg(time, line.left(120)));
            item->setToolTip(value.text.left(1000));
        } else {
            item->setText(clipboard ? QStringLiteral("%1  %2 × %3")
                                        .arg(time)
                                        .arg(value.image.width())
                                        .arg(value.image.height())
                                    : time);
            item->setToolTip(tr("Click to copy image; drag to share"));
            item->setIcon(QPixmap::fromImage(value.image.scaled(
              288, 200, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        }
        if (value.id == selected) {
            m_list->setCurrentItem(item);
        }
    }
    if (clipboard) {
        m_empty->setText(m_clipboardEnabled
                           ? (m_search->text().isEmpty()
                                ? tr("Copied text and images appear here")
                                : tr("No matching clipboard items"))
                           : tr("Clipboard history is off. Turn it on in "
                                "Settings"));
    } else {
        m_empty->setText(m_search->text().isEmpty()
                           ? tr("Your next screenshot will appear here")
                           : tr("No matching screenshots"));
    }
    m_empty->setVisible(m_list->count() == 0);
    m_list->setVisible(m_list->count() > 0);
}

std::optional<RackEntry> RackWindow::entry(const QString& id) const
{
    for (const auto& value : currentModel().entries()) {
        if (value.id == id) {
            return value;
        }
    }
    return std::nullopt;
}

QDir RackWindow::exportDirectory()
{
    return QDir(
      QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
      QStringLiteral("/rack"));
}

void RackWindow::removeExportedFile(const QString& id)
{
    exportDirectory().remove(QStringLiteral("Flameshot-%1.png").arg(id));
}

void RackWindow::purgeExportedFiles()
{
    QDir directory = exportDirectory();
    const auto files =
      directory.entryList({ QStringLiteral("Flameshot-*.png") }, QDir::Files);
    for (const QString& file : files) {
        directory.remove(file);
    }
}

QString RackWindow::exportEntry(const RackEntry& value)
{
    const QDir directory = exportDirectory();
    if (!QDir().mkpath(directory.path())) {
        m_hint->setText(tr("Could not prepare the image file"));
        return {};
    }
    const QString path =
      directory.filePath(QStringLiteral("Flameshot-%1.png").arg(value.id));
    if (!value.image.save(path, "PNG")) {
        m_hint->setText(tr("Could not prepare the image file"));
        return {};
    }
    return path;
}

void RackWindow::dragEntry(const QString& id)
{
    const auto selected = entry(id);
    if (!selected) {
        return;
    }
    ++m_interactionDepth;
    m_closeTimer.stop();
    if (selected->image.isNull()) {
        QDrag drag(m_list);
        auto* mime = new QMimeData;
        mime->setText(selected->text);
        drag.setMimeData(mime);
        drag.exec(Qt::CopyAction, Qt::CopyAction);
        finishInteraction();
        return;
    }
    const QString path = exportEntry(*selected);
    if (!path.isEmpty()) {
        QDrag drag(m_list);
        auto* mime = new QMimeData;
        mime->setUrls({ QUrl::fromLocalFile(path) });
        mime->setImageData(selected->image);
        drag.setMimeData(mime);
        drag.setPixmap(QPixmap::fromImage(selected->image.scaled(
          144, 100, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        drag.exec(Qt::CopyAction, Qt::CopyAction);
    }
    finishInteraction();
}

void RackWindow::openEntry(const RackEntry& selected)
{
    const QString path = exportEntry(selected);
    if (!path.isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
}

void RackWindow::contextMenu(const QPoint& point)
{
    const auto* item = m_list->itemAt(point);
    if (!item) {
        return;
    }
    const auto selected = entry(item->data(Qt::UserRole).toString());
    if (!selected) {
        return;
    }
    ++m_interactionDepth;
    m_closeTimer.stop();
    QMenu menu(this);
    const bool image = !selected->image.isNull();
    auto& model = currentModel();
    menu.addAction(image ? tr("Copy image") : tr("Copy text"),
                   this,
                   [this, selected] { emit copyRequested(*selected); });
    if (image) {
        menu.addAction(
          tr("Open image"), this, [this, selected] { openEntry(*selected); });
    }
    menu.addSeparator();
    menu.addAction(m_view == View::Clipboard ? tr("Remove from history")
                                             : tr("Remove from Rack"),
                   this,
                   [this, &model, selected] {
                       removeExportedFile(selected->id);
                       model.remove(selected->id);
                   });
    menu.exec(m_list->viewport()->mapToGlobal(point));
    finishInteraction();
}

void RackWindow::finishInteraction()
{
    --m_interactionDepth;
    if (m_refreshPending) {
        m_refreshPending = false;
        refresh();
    }
    trackPointer();
}

void RackWindow::retranslateUi()
{
    setWindowTitle(tr("Rack"));
    setAccessibleName(tr("Screenshot Rack"));
    m_settings->setText(QStringLiteral("\u2699"));
    m_settings->setToolTip(tr("Settings"));
    m_close->setText(QStringLiteral("\u2715"));
    m_close->setToolTip(tr("Collapse"));
    m_screenshotsTab->setText(tr("Screenshots"));
    m_clipboardTab->setText(tr("Clipboard"));
    m_clearHistory->setText(tr("Clear"));
    m_hint->setText(m_view == View::Clipboard
                      ? tr("Click to copy again; drag to share")
                      : tr("Click to copy image; drag to share"));
    m_list->setAccessibleName(m_view == View::Clipboard
                                ? tr("Clipboard history")
                                : tr("Rack screenshots"));
    refresh();
}

void RackWindow::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF bodyRect = QRectF(rect()).adjusted(0.5, -12, -0.5, -0.5);
    QLinearGradient bodyGrad(bodyRect.topLeft(), bodyRect.bottomLeft());
    QColor windowColor = palette().color(QPalette::Window);
    // Slight translucency gives the capsule a frosted, mac-like feel.
    windowColor.setAlpha(232);
    bodyGrad.setColorAt(0, windowColor.lighter(106));
    bodyGrad.setColorAt(1, windowColor.darker(104));
    painter.setPen(palette().color(QPalette::Mid));
    painter.setBrush(bodyGrad);
    painter.drawRoundedRect(bodyRect, 18, 18);
    if (m_expanded && m_view == View::Screenshots && m_list->count() > 0) {
        // The clothesline: one rope anchored across the cards row. Delegates
        // drop a string and pin from each card up to this line.
        const QRect itemRect = m_list->visualItemRect(m_list->item(0));
        const qreal top =
          m_list->viewport()->mapTo(this, QPoint(0, itemRect.top())).y() +
          RackRopeOffset;
        const qreal left = m_body->geometry().left() + 14;
        const qreal right = m_body->geometry().right() - 14;
        // Gentle sag makes the rope read as a rope, not a divider.
        painter.setPen(QPen(palette().color(QPalette::Dark), 1.4));
        painter.setBrush(Qt::NoBrush);
        QPainterPath rope;
        rope.moveTo(QPointF(left, top));
        rope.quadTo(QPointF((left + right) / 2.0, top + 9),
                    QPointF(right, top));
        painter.drawPath(rope);
        painter.setPen(Qt::NoPen);
        painter.setBrush(palette().color(QPalette::Dark));
        painter.drawEllipse(QPointF(left, top), 2.6, 2.6);
        painter.drawEllipse(QPointF(right, top), 2.6, 2.6);
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(palette().color(QPalette::Text));
    painter.drawRoundedRect(QRectF(width() / 2.0 - 20, 2, 40, 3), 1.5, 1.5);
}

void RackWindow::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
}

void RackWindow::closeEvent(QCloseEvent* event)
{
    // The rack is always-on: closing only dismisses the panel. The event
    // must still be accepted — ignoring it vetoes QApplication::quit.
    event->accept();
    dismiss();
}

void RackWindow::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && event->position().y() < 22) {
        toggle();
    } else {
        QWidget::mousePressEvent(event);
    }
}

void RackWindow::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        dismiss();
        return;
    }
    if (auto* item = m_list->currentItem()) {
        const auto selected = entry(item->data(Qt::UserRole).toString());
        if (selected &&
            (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
             event->matches(QKeySequence::Copy))) {
            emit copyRequested(*selected);
            return;
        }
        if (selected && event->key() == Qt::Key_Delete) {
            currentModel().remove(selected->id);
            return;
        }
    }
    QWidget::keyPressEvent(event);
}
