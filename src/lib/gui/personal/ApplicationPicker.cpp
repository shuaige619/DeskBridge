/*
 * SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "ApplicationPicker.h"
#include "PersonalStyle.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFileDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMap>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace deskflow::gui::personal {
namespace {
QMap<QString, ApplicationInfo> installedApplications()
{
  QMap<QString, ApplicationInfo> result;
#ifdef Q_OS_MACOS
  QFileIconProvider icons;
  const auto visit = [&](auto &&self, const QString &path, int depth) -> void {
    const auto folders = QDir(path).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable, QDir::Name);
    for (const auto &folder : folders) {
      if (folder.suffix().compare(QStringLiteral("app"), Qt::CaseInsensitive) == 0) {
        const auto plist = folder.filePath() + QStringLiteral("/Contents/Info.plist");
        if (!QFileInfo::exists(plist))
          continue;
        const QSettings metadata(plist, QSettings::NativeFormat);
        const auto id = metadata.value(QStringLiteral("CFBundleIdentifier")).toString();
        if (id.isEmpty() || result.contains(id))
          continue;
        auto name = metadata.value(QStringLiteral("CFBundleDisplayName")).toString();
        if (name.isEmpty()) name = metadata.value(QStringLiteral("CFBundleName")).toString();
        if (name.isEmpty()) name = folder.completeBaseName();
        if (id == QStringLiteral("com.openai.codex")) name = QStringLiteral("Codex");
        if (id == QStringLiteral("com.electron.lark")) name = QStringLiteral("飞书");
        result.insert(id, {id, name, icons.icon(folder)});
      } else if (depth > 0 && !folder.isSymLink()) {
        self(self, folder.filePath(), depth - 1);
      }
    }
  };
  for (const auto &path : {QStringLiteral("/Applications"), QStringLiteral("/System/Applications"),
                           QDir::homePath() + QStringLiteral("/Applications")}) {
    visit(visit, path, 2);
  }
#elif defined(Q_OS_WIN)
  QFileIconProvider icons;
  const QStringList candidates{
      qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/Programs/Microsoft VS Code/Code.exe"),
      qEnvironmentVariable("ProgramFiles") + QStringLiteral("/Microsoft VS Code/Code.exe"),
      qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/Feishu/Feishu.exe"),
      qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/Programs/Codex/Codex.exe")};
  for (const auto &path : candidates) {
    const QFileInfo file(path);
    if (file.isFile()) {
      const auto id = ApplicationPicker::executableIdentifier(file.absoluteFilePath());
      result.insert(id, {id, file.completeBaseName(), icons.icon(file)});
    }
  }
#elif defined(Q_OS_LINUX)
  for (const auto &directory : QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation)) {
    const auto entries = QDir(directory).entryInfoList({QStringLiteral("*.desktop")}, QDir::Files | QDir::Readable);
    for (const auto &entry : entries) {
      QSettings desktop(entry.absoluteFilePath(), QSettings::IniFormat);
      desktop.beginGroup(QStringLiteral("Desktop Entry"));
      if (desktop.value(QStringLiteral("Hidden"), false).toBool() ||
          desktop.value(QStringLiteral("NoDisplay"), false).toBool()) continue;
      const auto command = QProcess::splitCommand(desktop.value(QStringLiteral("Exec")).toString());
      if (command.isEmpty() || QFileInfo(command.first()).fileName() == QStringLiteral("env")) continue;
      const auto path = QStandardPaths::findExecutable(command.first());
      if (path.isEmpty()) continue;
      const auto id = ApplicationPicker::executableIdentifier(path);
      const auto name = desktop.value(QStringLiteral("Name"), QFileInfo(path).completeBaseName()).toString();
      result.insert(id, {id, name, QIcon::fromTheme(desktop.value(QStringLiteral("Icon")).toString())});
    }
  }
#endif
  return result;
}

const QMap<QString, ApplicationInfo> &applications()
{
  static const auto apps = installedApplications();
  return apps;
}

QList<ApplicationInfo> catalogForSelection(const QStringList &selected)
{
  auto choices = applications();
  for (const auto &id : selected)
    if (!choices.contains(id)) choices.insert(id, ApplicationPicker::application(id));
  return choices.values();
}
} // namespace

ApplicationInfo ApplicationPicker::application(const QString &id)
{
  if (applications().contains(id))
    return applications().value(id);
#ifdef Q_OS_MACOS
  if (id == QStringLiteral("com.openai.codex"))
    return {id, QStringLiteral("Codex"), QIcon::fromTheme(QStringLiteral("application-x-executable"))};
  if (id == QStringLiteral("com.electron.lark"))
    return {id, tr("飞书"), QIcon::fromTheme(QStringLiteral("application-x-executable"))};
#else
  const QFileInfo file(id);
  if (file.isAbsolute()) {
    QFileIconProvider icons;
    return {id, file.completeBaseName(), icons.icon(file)};
  }
#endif
  return {id, tr("未找到或属于其他系统的应用"), QIcon::fromTheme(QStringLiteral("application-x-executable"))};
}

QString ApplicationPicker::executableIdentifier(const QString &path)
{
  const QFileInfo file(path);
#ifdef Q_OS_WIN
  return QDir::cleanPath(QDir::fromNativeSeparators(file.absoluteFilePath())).toCaseFolded();
#else
  const auto canonical = file.canonicalFilePath();
  return QDir::cleanPath(canonical.isEmpty() ? file.absoluteFilePath() : canonical);
#endif
}

ApplicationPicker::ApplicationPicker(const QStringList &selected, QWidget *parent)
    : ApplicationPicker(selected, catalogForSelection(selected), parent)
{
}

ApplicationPicker::ApplicationPicker(const QStringList &selected, const QList<ApplicationInfo> &catalog, QWidget *parent)
    : QDialog(parent), m_selected(selected)
{
  setObjectName(QStringLiteral("applicationPicker"));
  setWindowTitle(tr("选择生效应用"));
  // Keep Dialog's keyboard-capable native type; WindowModal supplies the sheet.
  setWindowModality(Qt::WindowModal);
  resize(460, 510);
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(24, 24, 24, 20);
  layout->setSpacing(12);
  auto *description = new QLabel(tr("点按应用行即可选择，可多选；再次点按可取消本次选择。已添加的应用可返回映射页移除。"), this);
  description->setWordWrap(true);
  description->setProperty("secondaryText", true);
  layout->addWidget(description);
  auto *search = new QLineEdit(this);
  search->setObjectName(QStringLiteral("applicationSearch"));
  search->setAccessibleName(tr("搜索应用"));
  search->setPlaceholderText(tr("搜索应用名称"));
  search->setClearButtonEnabled(true);
  layout->addWidget(search);
#ifndef Q_OS_MACOS
  auto *browse = new QPushButton(tr("选择可执行文件…"), this);
  browse->setObjectName(QStringLiteral("browseApplicationExecutable"));
  browse->setAccessibleName(tr("选择本机应用程序文件"));
  layout->addWidget(browse);
  connect(browse, &QPushButton::clicked, this, [this, search] {
#ifdef Q_OS_WIN
    const auto path = QFileDialog::getOpenFileName(this, tr("选择应用程序"), {}, tr("应用程序 (*.exe)"));
#else
    const auto path = QFileDialog::getOpenFileName(this, tr("选择应用程序"), {}, tr("所有文件 (*)"));
#endif
    if (path.isEmpty()) return;
    const QFileInfo file(path);
    if (!file.isFile()
#ifdef Q_OS_WIN
        || file.suffix().compare(QStringLiteral("exe"), Qt::CaseInsensitive) != 0
#else
        || !file.isExecutable()
#endif
    ) {
      QMessageBox::warning(this, tr("无法选择应用"), tr("请选择一个本机可执行的应用程序文件。"));
      return;
    }
    const auto id = executableIdentifier(path);
    for (int i = 0; i < m_list->count(); ++i) {
      if (m_list->item(i)->data(Qt::UserRole).toString() == id) {
        if (!m_selected.contains(id)) m_list->item(i)->setSelected(true);
        search->clear();
        m_list->scrollToItem(m_list->item(i));
        return;
      }
    }
    QFileIconProvider icons;
    auto *item = new QListWidgetItem(icons.icon(file), file.completeBaseName(), m_list);
    item->setData(Qt::UserRole, id);
    item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
    item->setSizeHint(QSize(0, 40));
    item->setSelected(true);
    m_list->sortItems();
    search->clear();
    m_list->scrollToItem(item);
    if (auto *empty = findChild<QLabel *>(QStringLiteral("applicationEmptyNotice"))) empty->hide();
  });
#endif
  m_list = new QListWidget(this);
  m_list->setObjectName(QStringLiteral("applicationChoices"));
  m_list->setAccessibleName(tr("可选应用"));
  m_list->setSelectionMode(QAbstractItemView::MultiSelection);
  m_list->setIconSize(QSize(28, 28));
  m_list->setSpacing(3);
  QMap<QString, ApplicationInfo> choices;
  for (const auto &app : catalog)
    if (!app.id.isEmpty()) choices.insert(app.id, app);
  for (const auto &id : selected)
    if (!choices.contains(id)) choices.insert(id, {id, tr("未找到或属于其他系统的应用"), {}});
  for (const auto &app : choices) {
    const bool existing = selected.contains(app.id);
    auto *item = new QListWidgetItem(app.icon, existing ? tr("%1 · 已添加").arg(app.name) : app.name, m_list);
    item->setData(Qt::UserRole, app.id);
    item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
    if (existing) {
      item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
      item->setToolTip(tr("已在生效应用中。返回映射页可移除。"));
    }
    item->setSizeHint(QSize(0, 40));
  }
  m_list->sortItems();
  layout->addWidget(m_list, 1);
  auto *empty = new QLabel(
#ifdef Q_OS_MACOS
      tr("没有找到应用。应用安装到“应用程序”文件夹后，重新打开桌桥即可显示。"), this);
#else
      tr("没有找到应用。可以直接选择要使用的应用程序文件。"), this);
#endif
  empty->setObjectName(QStringLiteral("applicationEmptyNotice"));
  empty->setWordWrap(true);
  empty->setProperty("secondaryText", true);
  empty->setVisible(m_list->count() == 0);
  layout->addWidget(empty);
  m_selectionSummary = new QLabel(this);
  m_selectionSummary->setObjectName(QStringLiteral("applicationSelectionSummary"));
  m_selectionSummary->setProperty("secondaryText", true);
  layout->addWidget(m_selectionSummary);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok, this);
  buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
  m_confirm = buttons->button(QDialogButtonBox::Ok);
  m_confirm->setObjectName(QStringLiteral("confirmApplications"));
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(m_list, &QListWidget::itemSelectionChanged, this, &ApplicationPicker::updateSelection);
  connect(search, &QLineEdit::textChanged, this, [this, empty](const QString &text) {
    bool visible = false;
    for (int i = 0; i < m_list->count(); ++i) {
      const bool matches = m_list->item(i)->text().contains(text.trimmed(), Qt::CaseInsensitive);
      m_list->item(i)->setHidden(!matches);
      visible |= matches;
    }
    empty->setText(tr("没有找到匹配的应用，请试试其他名称。"));
    empty->setVisible(!visible);
  });
  updateSelection();
  applyPersonalStyle(this);
  search->setFocus();
}

QStringList ApplicationPicker::selectedApplications() const
{
  // Keep an unchanged selection byte-for-byte stable in the caller's draft.
  QStringList selected = m_selected;
  for (int i = 0; i < m_list->count(); ++i)
    if (m_list->item(i)->isSelected()) {
      const auto id = m_list->item(i)->data(Qt::UserRole).toString();
      if (!selected.contains(id))
        selected.append(id);
    }
  return selected;
}

void ApplicationPicker::updateSelection()
{
  const auto added = selectedApplications().size() - m_selected.size();
  m_confirm->setEnabled(added > 0);
  m_confirm->setText(added > 0 ? tr("添加 %1 个应用").arg(added) : tr("选择要添加的应用"));
  m_selectionSummary->setText(m_selected.isEmpty() ? tr("本次已选择 %1 个应用").arg(added)
      : tr("已添加 %1 个应用 · 本次选择 %2 个").arg(m_selected.size()).arg(added));
}

} // namespace deskflow::gui::personal
