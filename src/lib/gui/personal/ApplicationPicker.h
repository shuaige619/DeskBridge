/*
 * SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <QDialog>
#include <QIcon>
#include <QList>
#include <QStringList>

class QListWidget;
class QLabel;
class QPushButton;

namespace deskflow::gui::personal {

struct ApplicationInfo
{
  QString id;
  QString name;
  QIcon icon;
};

class ApplicationPicker : public QDialog
{
  Q_OBJECT
public:
  explicit ApplicationPicker(const QStringList &selected, QWidget *parent = nullptr);
  ApplicationPicker(const QStringList &selected, const QList<ApplicationInfo> &catalog, QWidget *parent = nullptr);
  QStringList selectedApplications() const;
  static ApplicationInfo application(const QString &id);
  static QString executableIdentifier(const QString &path);

private:
  void updateSelection();
  QListWidget *m_list = nullptr;
  QLabel *m_selectionSummary = nullptr;
  QPushButton *m_confirm = nullptr;
  QStringList m_selected;
};

} // namespace deskflow::gui::personal
