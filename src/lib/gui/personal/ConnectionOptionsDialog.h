/* SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception */
#pragma once
#include "common/Settings.h"
#include <QDialog>
#include <QVariantMap>

class QComboBox;
class QLineEdit;
class QCheckBox;
class QLabel;
class QPushButton;

namespace deskflow::gui::personal {
class ConnectionOptionsDialog : public QDialog
{
  Q_OBJECT
public:
  explicit ConnectionOptionsDialog(const QStringList &otherComputerNames = {}, QWidget *parent = nullptr);
  QVariantMap changes() const;

private:
  void refresh();
  QComboBox *m_role;
  QLineEdit *m_host;
  QLineEdit *m_name;
  QCheckBox *m_retry;
  QLabel *m_explanation;
  QLabel *m_error;
  QWidget *m_hostRow;
  QWidget *m_retryCard;
  QPushButton *m_save;
  QStringList m_otherNames;
  QVariantMap m_baseline;
};
} // namespace deskflow::gui::personal
