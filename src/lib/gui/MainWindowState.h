/* SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception */
#pragma once
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QRadioButton>

// Transitional bindings for the existing core controller. These widgets are
// never presented; all user interaction lives in PersonalWindow and its sheets.
// Keeping the bindings allows the controller's lifecycle code to be migrated
// independently of the visible interface.
struct MainWindowState
{
  QPushButton *btnConfigureClient;
  QPushButton *btnConfigureServer;
  QPushButton *btnEditName;
  QPushButton *btnRestartCore;
  QPushButton *btnSaveServerConfig;
  QPushButton *btnToggleCore;
  QWidget *clientOptions;
  QWidget *serverOptions;
  QLabel *lblComputerName;
  QLabel *lblIpAddresses;
  QLabel *lblNoMode;
  QLineEdit *lineEditName;
  QLineEdit *lineHostname;
  QRadioButton *rbModeClient;
  QRadioButton *rbModeServer;

  void setupUi(QMainWindow *window)
  {
    auto *state = new QWidget(window);
    state->setObjectName(QStringLiteral("coreControllerBindings"));
    window->setCentralWidget(state);
    btnConfigureClient = new QPushButton(state);
    btnConfigureServer = new QPushButton(state);
    btnEditName = new QPushButton(state);
    btnRestartCore = new QPushButton(state);
    btnSaveServerConfig = new QPushButton(state);
    btnToggleCore = new QPushButton(state);
    clientOptions = new QWidget(state);
    serverOptions = new QWidget(state);
    lblComputerName = new QLabel(state);
    lblIpAddresses = new QLabel(state);
    lblNoMode = new QLabel(state);
    lineEditName = new QLineEdit(state);
    lineEditName->setMaxLength(255);
    lineHostname = new QLineEdit(state);
    rbModeClient = new QRadioButton(state);
    rbModeServer = new QRadioButton(state);
  }
};
