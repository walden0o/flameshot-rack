// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "rackservice.h"

#include "rackwindow.h"
#include "utils/confighandler.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>

RackService* RackService::m_instance = nullptr;

RackService::RackService(QObject* parent)
  : QObject(parent)
  , m_window(nullptr)
  , m_enabled(false)
  , m_clipboardBlocked(false)
{
    connect(QGuiApplication::clipboard(),
            &QClipboard::dataChanged,
            this,
            &RackService::onClipboardChanged);
}

RackService* RackService::instance()
{
    if (!m_instance) {
        m_instance = new RackService();
    }
    return m_instance;
}

void RackService::ensureStarted()
{
    if (!m_window) {
        m_window = new RackWindow(m_model, m_clipboardModel);
        m_window->setClipboardHistoryEnabled(
          ConfigHandler().rackClipboardHistory());
        connect(m_window,
                &RackWindow::copyRequested,
                this,
                &RackService::copyToClipboard);
        connect(m_window, &RackWindow::settingsRequested, this, [this] {
            emit settingsRequested();
        });
    }
    setEnabled(ConfigHandler().rackEnabled());
    m_window->setDisplayMode(ConfigHandler().rackDisplayMode());
}

void RackService::addScreenshot(const QImage& image)
{
    if (image.isNull()) {
        return;
    }
    ensureStarted();
    m_model.add(QString(), image);
    if (m_enabled) {
        m_window->show();
    }
}

void RackService::setEnabled(bool enabled)
{
    m_enabled = enabled;
    if (!m_window) {
        return;
    }
    if (enabled) {
        m_window->show();
    } else {
        m_window->hide();
    }
}

bool RackService::enabled() const
{
    return m_enabled;
}

RackWindow* RackService::window() const
{
    return m_window;
}

RackContentModel* RackService::screenshotsModel()
{
    return &m_model;
}

RackContentModel* RackService::clipboardModel()
{
    return &m_clipboardModel;
}

QWidget* RackService::hostWidget() const
{
    return m_window;
}

void RackService::copyToClipboard(const RackEntry& entry)
{
    QClipboard* clipboard = QGuiApplication::clipboard();
    setClipboardBlocked(true);
    auto* mime = new QMimeData;
    if (entry.image.isNull()) {
        mime->setText(entry.text);
    } else {
        mime->setImageData(entry.image);
    }
    clipboard->setMimeData(mime);
    setClipboardBlocked(false);
}

void RackService::setClipboardBlocked(bool blocked)
{
    m_clipboardBlocked = blocked;
}

void RackService::onClipboardChanged()
{
    if (m_clipboardBlocked || !ConfigHandler().rackClipboardHistory()) {
        return;
    }
    const QMimeData* data = QGuiApplication::clipboard()->mimeData();
    if (!data || clipboardExcludedFromHistory(*data)) {
        return;
    }
    if (data->hasImage()) {
        const QImage image = data->imageData().value<QImage>();
        if (!image.isNull()) {
            m_clipboardModel.add(QString(), image);
            return;
        }
    }
    if (data->hasText()) {
        m_clipboardModel.add(data->text());
    }
}
