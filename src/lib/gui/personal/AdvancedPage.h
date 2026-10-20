/*
 * SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <QPointer>
#include <QVariantMap>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTextDocument;
class QToolButton;

namespace deskflow::gui::personal {

// A draft editor only. The owner validates and persists requested changes.
class AdvancedPage : public QWidget
{
  Q_OBJECT
public:
  explicit AdvancedPage(QWidget *parent = nullptr);
  bool isDirty() const;
  bool hasPendingEdits() const;
  void reload();
  void refreshFromSettings();
  void setApplying(bool applying);
  void setStatusMessage(const QString &message);
  void applied(bool success, const QString &message = {});
  void setLogDocument(QTextDocument *document);
  void setConnectionSecurity(bool secure);

Q_SIGNALS:
  void applyRequested(const QVariantMap &changes);
  void fingerprintRequested();
  void regenerateCertificateRequested(const QString &path, int bits);
  void resetAllRequested();
  void dirtyChanged(bool dirty);

private:
  void refresh();
  void draftChanged(const QString &key, const QVariant &value);
  QVariantMap changes() const;
  void populateInterfaces();
  void restoreDefaults();
  void showLog();
  void copyLog();
  void regenerateCertificate();
  void resetAll();
  bool certificateDraftChanged() const;

  QVariantMap m_saved;
  QVariantMap m_draft;
  QPointer<QTextDocument> m_logDocument;
  QLabel *m_readOnly = nullptr;
  QLabel *m_status = nullptr;
  QLabel *m_securityState = nullptr;
  QLabel *m_certificateName = nullptr;
  QLabel *m_certificateHint = nullptr;
  QCheckBox *m_tlsEnabled = nullptr;
  QCheckBox *m_checkPeers = nullptr;
  QPushButton *m_fingerprint = nullptr;
  QToolButton *m_networkFold = nullptr;
  QToolButton *m_certificateFold = nullptr;
  QSpinBox *m_port = nullptr;
  QComboBox *m_interface = nullptr;
  QLineEdit *m_certificatePath = nullptr;
  QComboBox *m_keySize = nullptr;
  QPushButton *m_browseCertificate = nullptr;
  QPushButton *m_regenerate = nullptr;
  QPushButton *m_viewLog = nullptr;
  QPushButton *m_copyLog = nullptr;
  QToolButton *m_diagnosticFold = nullptr;
  QComboBox *m_logLevel = nullptr;
  QCheckBox *m_guiDebug = nullptr;
  QCheckBox *m_logToFile = nullptr;
  QLineEdit *m_logFile = nullptr;
  QPushButton *m_browseLog = nullptr;
  QToolButton *m_automationFold = nullptr;
  QCheckBox *m_enableEnter = nullptr;
  QCheckBox *m_enableExit = nullptr;
  QLineEdit *m_enterCommand = nullptr;
  QLineEdit *m_exitCommand = nullptr;
#ifdef Q_OS_WIN
  QToolButton *m_processFold = nullptr;
  QComboBox *m_processMode = nullptr;
  QCheckBox *m_elevate = nullptr;
#endif
  QPushButton *m_defaults = nullptr;
  QPushButton *m_resetAll = nullptr;
  QPushButton *m_revert = nullptr;
  QPushButton *m_apply = nullptr;
  QString m_savedCertificatePath;
  int m_savedKeySize = 2048;
  bool m_applying = false;
  bool m_syncing = false;
  bool m_lastDirty = false;
  bool m_connectionSecure = false;
  bool m_connectionKnown = false;
};

} // namespace deskflow::gui::personal
