/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QDialog>
#include <QPushButton>
#include <QTextBrowser>
#include <QUrl>
#include <QVector>

class QListWidget;

class HelpDialog : public QDialog
{
  Q_OBJECT

public:
  explicit HelpDialog(QWidget *parent = nullptr, const QUrl &source = {});
  ~HelpDialog() override = default;

protected:
  void changeEvent(QEvent *e) override;

private:
  void updateText();
  void historyChanged();
  void linkClicked(const QUrl &url);
  void showPage(int page, bool remember = true);
  QString pageContent(int page) const;
  QPushButton *m_btnBack = nullptr;
  QPushButton *m_btnHome = nullptr;
  QTextBrowser *m_browser = nullptr;
  QPushButton *m_btnClose = nullptr;
  QListWidget *m_topics = nullptr;
  QVector<int> m_history;
  int m_historyPosition = -1;
};
