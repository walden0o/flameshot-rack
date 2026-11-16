// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "rackservice.h"

#include "rackwindow.h"
#include "utils/confighandler.h"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QMimeData>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>

RackService* RackService::m_instance = nullptr;

namespace {

// Entries live under <app data>/rack/{screenshots,clipboard}/ as one
// <id>.png or <id>.txt plus a <id>.meta sidecar per entry.
QDir rackStorageDir(const QString& name)
{
    QDir dir(QStandardPaths::writableLocation(
               QStandardPaths::AppDataLocation) +
             QStringLiteral("/rack/") + name);
    dir.mkpath(QStringLiteral("."));
    return dir;
}

void persistModel(const RackContentModel& model, const QString& name)
{
    QDir dir = rackStorageDir(name);
    QSet<QString> live;
    for (const auto& entry : model.entries()) {
        live.insert(entry.id);
    }
    for (const QString& file :
         dir.entryList({ QStringLiteral("*.png"),
                         QStringLiteral("*.txt"),
                         QStringLiteral("*.meta") },
                       QDir::Files)) {
        const QString id = file.left(file.lastIndexOf(u'.'));
        if (!live.contains(id)) {
            dir.remove(file);
        }
    }
    for (const auto& entry : model.entries()) {
        const QString metaPath =
          dir.filePath(entry.id + QStringLiteral(".meta"));
        if (QFile::exists(metaPath)) {
            continue;
        }
        if (!entry.image.isNull()) {
            entry.image.save(dir.filePath(entry.id + QStringLiteral(".png")),
                             "PNG");
        } else {
            QFile file(dir.filePath(entry.id + QStringLiteral(".txt")));
            if (file.open(QIODevice::WriteOnly)) {
                file.write(entry.text.toUtf8());
            }
        }
        QSettings meta(metaPath, QSettings::IniFormat);
        meta.setValue(QStringLiteral("createdUtc"),
                      entry.createdUtc.toString(Qt::ISODateWithMs));
        meta.setValue(QStringLiteral("digest"),
                      QString::fromLatin1(entry.digest.toHex()));
    }
}

QVector<RackEntry> loadEntries(const QString& name)
{
    const QDir dir = rackStorageDir(name);
    QVector<RackEntry> loaded;
    for (const QString& metaName :
         dir.entryList({ QStringLiteral("*.meta") }, QDir::Files)) {
        const QString id =
          metaName.left(metaName.size() - QStringLiteral(".meta").size());
        QSettings meta(dir.filePath(metaName), QSettings::IniFormat);
        RackEntry entry;
        entry.id = id;
        entry.createdUtc =
          QDateTime::fromString(
            meta.value(QStringLiteral("createdUtc")).toString(),
            Qt::ISODateWithMs)
            .toUTC();
        entry.digest = QByteArray::fromHex(
          meta.value(QStringLiteral("digest")).toString().toLatin1());
        const QString pngPath =
          dir.filePath(id + QStringLiteral(".png"));
        const QString txtPath =
          dir.filePath(id + QStringLiteral(".txt"));
        if (QFile::exists(pngPath)) {
            entry.image = QImage(pngPath);
        } else if (QFile::exists(txtPath)) {
            QFile file(txtPath);
            if (file.open(QIODevice::ReadOnly)) {
                entry.text = QString::fromUtf8(file.readAll());
            }
        }
        if (entry.createdUtc.isValid() &&
            (!entry.image.isNull() || !entry.text.isEmpty())) {
            loaded.append(entry);
        }
    }
    std::sort(loaded.begin(),
              loaded.end(),
              [](const RackEntry& a, const RackEntry& b) {
                  return a.createdUtc > b.createdUtc;
              });
    return loaded;
}

void wipeStorage(const QString& name)
{
    rackStorageDir(name).removeRecursively();
}

} // namespace

RackService::RackService(QObject* parent)
  : QObject(parent)
  , m_window(nullptr)
  , m_enabled(false)
  , m_clipboardBlocked(false)
  , m_storageLoaded(false)
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
    if (!m_storageLoaded) {
        m_storageLoaded = true;
        m_model.restore(loadEntries(QStringLiteral("screenshots")));
        if (ConfigHandler().rackClipboardHistory()) {
            m_clipboardModel.restore(
              loadEntries(QStringLiteral("clipboard")));
        }
        connect(&m_model,
                &RackContentModel::changed,
                this,
                [this] {
                    persistModel(m_model, QStringLiteral("screenshots"));
                });
        connect(&m_clipboardModel,
                &RackContentModel::changed,
                this,
                [this] {
                    persistModel(m_clipboardModel,
                                 QStringLiteral("clipboard"));
                });
    }
    setEnabled(ConfigHandler().rackEnabled());
    m_window->setDisplayMode(ConfigHandler().rackDisplayMode());
    m_window->setPosition(ConfigHandler().rackPosition());
}

void RackService::addScreenshot(const QImage& image)
{
    if (image.isNull()) {
        return;
    }
    ensureStarted();
    m_model.add(QString(), image);
    if (m_enabled) {
        m_window->updateVisibility();
    }
}

void RackService::setEnabled(bool enabled)
{
    m_enabled = enabled;
    if (!m_window) {
        return;
    }
    m_window->setServiceEnabled(enabled);
    m_window->updateVisibility();
}

bool RackService::enabled() const
{
    return m_enabled;
}

void RackService::setClipboardHistoryEnabled(bool enabled)
{
    if (m_window) {
        m_window->setClipboardHistoryEnabled(enabled);
    }
    if (!enabled) {
        // Turning the feature off also drops anything already collected,
        // in memory and on disk.
        m_clipboardModel.clear();
        wipeStorage(QStringLiteral("clipboard"));
    }
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
