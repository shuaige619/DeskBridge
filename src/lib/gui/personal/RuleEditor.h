/*
 * SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "common/ShortcutRules.h"

#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QVBoxLayout;

namespace deskflow::gui::personal {
class ChordRecorder;

class RuleEditor : public QDialog
{
  Q_OBJECT
public:
  RuleEditor(const shortcuts::Configuration &configuration, const shortcuts::Rule &rule, QWidget *parent = nullptr,
             shortcuts::DeviceOS detectedSourceOS = shortcuts::DeviceOS::Unknown);
  shortcuts::Rule rule() const;
  QStringList globalApplications() const;
  bool deletionRequested() const;

private:
  void rebuildApplications();
  void updatePreview();
  QString validationError() const;
  void accept() override;
  shortcuts::Configuration m_configuration;
  shortcuts::Rule m_original;
  QStringList m_apps;
  QLineEdit *m_name = nullptr;
  ChordRecorder *m_input = nullptr;
  ChordRecorder *m_output = nullptr;
  QWidget *m_appChoices = nullptr;
  QVBoxLayout *m_appLayout = nullptr;
  QList<QCheckBox *> m_appChecks;
  QLabel *m_error = nullptr;
  QLabel *m_preview = nullptr;
  QLabel *m_terminalWarning = nullptr;
  QPushButton *m_save = nullptr;
  bool m_delete = false;
  shortcuts::DeviceOS m_sourceOS = shortcuts::DeviceOS::Unknown;
};
} // namespace deskflow::gui::personal
