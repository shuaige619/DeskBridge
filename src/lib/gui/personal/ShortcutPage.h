/*
 * SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "common/ShortcutRules.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QGridLayout;
class QLabel;
class QPushButton;
class QVBoxLayout;

namespace deskflow::gui::personal {

class ShortcutPage : public QWidget
{
  Q_OBJECT
public:
  explicit ShortcutPage(QWidget *parent = nullptr);
  bool isDirty() const;
  void reload();
  shortcuts::Configuration configuration() const;
  void setApplying(bool applying);
  void setStatusMessage(const QString &message);
  void applied(bool success, const QString &message = {});
  void setSourceOperatingSystem(shortcuts::DeviceOS os);
  void setClientMode(bool client);
  void setServerModifierMapping(bool enabled);
  void clearSourceOperatingSystemFallback();

Q_SIGNALS:
  void applyRequested(const deskflow::shortcuts::Configuration &configuration);
  void dirtyChanged(bool dirty);
  void keyboardCompatibilityRequested();

private:
  void refresh();
  void rebuildApplications();
  void rebuildRules();
  void editRule(int index);
  void restoreDefaults();
  void draftChanged();
  void applyHabit(shortcuts::Habit habit);
  shortcuts::Configuration m_saved;
  shortcuts::Configuration m_draft;
  QCheckBox *m_master = nullptr;
  QCheckBox *m_fullShortcuts = nullptr;
  QCheckBox *m_allApplications = nullptr;
  QCheckBox *m_shiftTapInputMethod = nullptr;
  QLabel *m_shiftTapInputMethodHint = nullptr;
  QLabel *m_fullShortcutHint = nullptr;
  QLabel *m_fullTerminalWarning = nullptr;
  QLabel *m_offHint = nullptr;
  QLabel *m_terminalWarning = nullptr;
  QLabel *m_status = nullptr;
  QLabel *m_error = nullptr;
  QLabel *m_readOnly = nullptr;
  QLabel *m_habitDescription = nullptr;
  QLabel *m_habitState = nullptr;
  QLabel *m_roleNotice = nullptr;
  QLabel *m_modifierNotice = nullptr;
  QComboBox *m_sourceOS = nullptr;
  QList<QPushButton *> m_habitButtons;
  QPushButton *m_addRule = nullptr;
  QPushButton *m_addApp = nullptr;
  QPushButton *m_defaults = nullptr;
  QPushButton *m_revert = nullptr;
  QPushButton *m_apply = nullptr;
  QWidget *m_apps = nullptr;
  QGridLayout *m_appsLayout = nullptr;
  QWidget *m_rules = nullptr;
  QVBoxLayout *m_rulesLayout = nullptr;
  bool m_applying = false;
  bool m_invalidLoad = false;
  bool m_forceDirty = false;
  bool m_lastDirty = false;
  bool m_clientMode = true;
  bool m_serverModifierMapping = false;
  shortcuts::DeviceOS m_detectedSourceOS = shortcuts::DeviceOS::Unknown;
  QString m_loadError;
};
} // namespace deskflow::gui::personal
