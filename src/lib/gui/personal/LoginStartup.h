/*
 * SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <QObject>
#include <QString>
#include <functional>

namespace deskflow::gui::personal {

class LoginStartup : public QObject
{
  Q_OBJECT
public:
  struct Status
  {
    bool supported = false;
    bool enabled = false;
    bool approvalRequired = false;
    QString message;
    QString error;

    bool operator==(const Status &other) const;
  };

  struct ChangeResult
  {
    bool success = false;
    Status status;
    QString error;
  };

  struct Operations
  {
    std::function<Status()> read;
    std::function<ChangeResult(bool)> change;
    std::function<void()> openSettings;
  };

  explicit LoginStartup(QObject *parent = nullptr);
  explicit LoginStartup(Operations operations, QObject *parent = nullptr);
  Status status() const;
  bool isApplying() const;

public Q_SLOTS:
  void refresh();
  void setEnabled(bool enabled);
  void openSystemSettings();

Q_SIGNALS:
  void statusChanged();
  void applyingChanged(bool applying);
  void finished(bool success, const QString &message);

private:
  void updateStatus(Status status);

  Operations m_operations;
  Status m_status;
  bool m_applying = false;
};

} // namespace deskflow::gui::personal
