/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2024 - 2026 Chris Rizzitello <sithord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2012 - 2024 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2008 Volker Lanz <vl@fidra.de>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "MainWindow.h"
#include "ui_MainWindow.h"

#include "Diagnostic.h"
#include "StyleUtils.h"

#include "dialogs/AboutDialog.h"
#include "dialogs/ClientConfigDialog.h"
#include "dialogs/FingerprintDialog.h"
#include "dialogs/HelpDialog.h"
#include "dialogs/ServerConfigDialog.h"
#include "dialogs/SettingsDialog.h"

#include "common/PlatformInfo.h"
#include "common/Settings.h"
#include "common/UrlConstants.h"
#include "common/VersionInfo.h"
#include "gui/Messages.h"
#include "gui/TlsUtility.h"
#include "gui/core/CoreProcess.h"
#include "gui/ipc/DaemonIpcClient.h"
#include "gui/widgets/LogDock.h"
#include "net/FingerprintDatabase.h"
#include "net/SecureUtils.h"
#include "widgets/StatusBar.h"
#include "personal/PersonalWindow.h"
#include "personal/AdvancedPage.h"
#include "personal/PreferenceChanges.h"
#include "personal/PreferencesApplyController.h"
#include "personal/ShortcutPage.h"
#include "personal/ShortcutApplyController.h"
#include "personal/PersonalStyle.h"
#include "personal/ScreenLayoutPersistence.h"

#include <QCheckBox>
#include <QApplication>
#include <QComboBox>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFileDialog>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkInterface>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScreen>
#include <QSslCertificate>
#include <QSslKey>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QTemporaryFile>
#include <QScrollBar>
#include <QVBoxLayout>

#include <memory>

#if defined(Q_OS_MACOS)
#include <ApplicationServices/ApplicationServices.h>
#endif

using namespace deskflow::gui;

MainWindow::MainWindow()
    : ui{std::make_unique<Ui::MainWindow>()},
      m_coreProcess(m_serverConfig),
      m_trayIcon{new QSystemTrayIcon(this)},
      m_guiDupeChecker{new QLocalServer(this)},
      m_daemonIpcClient{new ipc::DaemonIpcClient(this)},
      m_logDock{new LogDock(this)},
      m_statusBar{new StatusBar(this)},
      m_menuFile{new QMenu(this)},
      m_menuEdit{new QMenu(this)},
      m_menuView{new QMenu(this)},
      m_menuHelp{new QMenu(this)},
      m_actionAbout{new QAction(this)},
      m_actionMinimize{new QAction(this)},
      m_actionQuit{new QAction(this)},
      m_actionTrayQuit{new QAction(this)},
      m_actionRestore{new QAction(this)},
      m_actionSettings{new QAction(this)},
      m_actionStartCore{new QAction(this)},
      m_actionRestartCore{new QAction(this)},
      m_actionStopCore{new QAction(this)},
      m_actionShowHelp{new QAction(this)},
      m_networkMonitor{new NetworkMonitor(this)}
{
  ui->setupUi(this);

  setWindowIcon(appIcon());

  addDockWidget(Qt::BottomDockWidgetArea, m_logDock);

  // Setup Actions
  m_actionAbout->setMenuRole(QAction::AboutRole);
  m_actionAbout->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::HelpAbout));

  m_actionMinimize->setIcon(QIcon::fromTheme(QStringLiteral("window-minimize-pip")));
  m_actionRestore->setIcon(QIcon::fromTheme(QStringLiteral("window-restore-pip")));

  if (!deskflow::platform::isWindows()) {
    m_actionQuit->setShortcut(QKeySequence::Quit);
    m_actionTrayQuit->setShortcut(QKeySequence::Quit);
  }

  m_actionQuit->setIcon(QIcon::fromTheme("application-exit"));
  m_actionQuit->setMenuRole(QAction::QuitRole);

  m_actionTrayQuit->setIcon(QIcon::fromTheme("application-exit"));
  m_actionTrayQuit->setMenuRole(QAction::NoRole);

  m_actionSettings->setIcon(QIcon::fromTheme(QStringLiteral("configure")));
  m_actionSettings->setMenuRole(QAction::PreferencesRole);
  m_actionSettings->setShortcut(QKeySequence::Preferences);

  m_actionStartCore->setIcon(QIcon::fromTheme(QStringLiteral("system-run")));
  m_actionStartCore->setMenuRole(QAction::NoRole);

  m_actionRestartCore->setVisible(false);
  m_actionRestartCore->setIcon(QIcon::fromTheme(QStringLiteral("view-refresh")));
  m_actionRestartCore->setMenuRole(QAction::NoRole);

  m_actionStopCore->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::ProcessStop));
  m_actionStopCore->setMenuRole(QAction::NoRole);

  m_actionShowHelp->setIcon(QIcon::fromTheme(QStringLiteral("question")));
  m_actionShowHelp->setMenuRole(QAction::NoRole);
  m_actionShowHelp->setShortcut(QKeySequence::HelpContents);

  // Setup the Instance Checking
  // In case of a previous crash remove first
  QLocalServer::removeServer(m_guiSocketName);
  m_guiDupeChecker->listen(m_guiSocketName);

  createMenuBar();
  setupControls();
  updateText();
  connectSlots();
  setupPersonalUi();
  setupTrayIcon();
  updateComputerName();
  setHelpFilePath();

  qDebug().noquote() << "active settings path:" << Settings::settingsPath();

  // Force generation of a certificate on the host
  if (TlsUtility::isEnabled()) {
    if (Settings::value(Settings::Security::KeySize).toInt() < 2048) {
      QMessageBox::information(
          this, kAppName,
          tr("Your current TLS key is smaller than the minimum allowed size, A new key 2048-bit key will be generated.")
      );
      Settings::setValue(Settings::Security::KeySize, 2048);
    }
    if (!TlsUtility::isCertValid()) {
      generateCertificate();
    } else {
      m_fingerprint = {QCryptographicHash::Sha256, TlsUtility::certFingerprint()};
    }
  }

  applyConfig();
  m_statusBar->setSecurityIcon(TlsUtility::isEnabled());
  restoreWindow();

#ifdef Q_OS_MACOS
  // Route native quits (Cmd+Q / Apple menu Quit / Dock "Quit") to the usual close-to-tray decision instead.
  installQuitHandler([this] { return !maybeHideToTray(); });
#endif
}
MainWindow::~MainWindow()
{
  // Stop network monitoring
  if (m_networkMonitor) {
    m_networkMonitor->stopMonitoring();
  }

  m_guiDupeChecker->close();
  m_coreProcess.cleanup();
}

deskflow::gui::personal::ShortcutPage *MainWindow::personalShortcutPage() const
{
  return m_personalUi ? m_personalUi->shortcutsPage() : nullptr;
}

void MainWindow::setupPersonalUi()
{
  if (QString::fromLatin1(kRuntimeId) == QString::fromLatin1(kAppId)) return;
  m_legacyCentral = takeCentralWidget();
  m_legacyCentral->setParent(this);
  m_legacyCentral->hide();
  m_personalUi = new personal::PersonalWindow(this);
  m_layoutPersistence = std::make_unique<personal::ScreenLayoutPersistence>(m_serverConfig);
  setCentralWidget(m_personalUi);
  m_statusBar->hide();
  removeDockWidget(m_logDock);
  m_logDock->hide();
  m_menuView->removeAction(m_logDock->toggleViewAction());
  connect(m_personalUi, &personal::PersonalWindow::connectRequested, this, &MainWindow::startCore);
  connect(m_personalUi, &personal::PersonalWindow::disconnectRequested, this, &MainWindow::stopCore);
  connect(m_personalUi, &personal::PersonalWindow::connectionOptionsRequested, this, &MainWindow::openPersonalConnectionOptions);
  auto *advanced = m_personalUi->advancedPage();
  advanced->setLogDocument(m_logDock->logDocument());
  connect(advanced, &personal::AdvancedPage::fingerprintRequested, this, &MainWindow::showMyFingerprint);
  connect(advanced, &personal::AdvancedPage::regenerateCertificateRequested, this, &MainWindow::regeneratePersonalCertificate);
  connect(advanced, &personal::AdvancedPage::resetAllRequested, this, [this] {
    if (!guardPersonalConfigurationChange()) clearSettings();
  });
  connect(advanced, &personal::AdvancedPage::applyRequested, this, [this](const QVariantMap &changes) {
    applyPersonalPreferences(changes, PreferencesTarget::Advanced);
  });
  connect(m_personalUi, &personal::PersonalWindow::preferencesApplyRequested, this, [this](const QVariantMap &changes) {
    applyPersonalPreferences(changes, PreferencesTarget::General);
  });
  connect(personalShortcutPage(), &personal::ShortcutPage::keyboardCompatibilityRequested,
          this, &MainWindow::openKeyboardCompatibility);
  connect(m_personalUi, &personal::PersonalWindow::screenLayoutRequested, this, &MainWindow::requestPersonalScreenLayout);
  connect(m_personalUi, &personal::PersonalWindow::screenMoveRequested, this, &MainWindow::requestPersonalScreenMove);
  connect(&m_coreProcess, &CoreProcess::screenLayoutChanged, this, [this] {
    if (!m_layoutPending) m_layoutFeedback.clear();
    refreshPersonalUi();
  }, Qt::QueuedConnection);
  connect(&m_coreProcess, &CoreProcess::layoutRequestReceived, this, &MainWindow::persistPersonalScreenLayout,
          Qt::QueuedConnection);
  connect(&m_coreProcess, &CoreProcess::layoutMoveReceived, this, &MainWindow::persistPersonalScreenMove,
          Qt::QueuedConnection);
  connect(&m_coreProcess, &CoreProcess::layoutRequestFinished, this, [this](bool success, const QString &error) {
    m_layoutPending = false;
    m_layoutUncertain = false;
    m_layoutFeedback = success ? tr("屏幕布局已同步。") : error;
    refreshPersonalUi();
  }, Qt::QueuedConnection);
  connect(&m_coreProcess, &CoreProcess::layoutApplyFinished, this,
          [this](quint32 requestId, bool success, const QString &error) {
    if (!m_layoutPersistence->isPending() || requestId != m_layoutRequestId) return;
    QString restoreError;
    m_layoutPersistence->finish(success, &restoreError);
    m_layoutPending = false;
    m_layoutUncertain = false;
    m_layoutFeedback = success ? tr("屏幕布局已同步。") : error;
    if (!restoreError.isEmpty()) m_layoutFeedback += QStringLiteral(" ") + restoreError;
    refreshPersonalUi();
  }, Qt::QueuedConnection);
  connect(&m_coreProcess, &CoreProcess::layoutSynchronizationUncertain, this, [this](const QString &error) {
    m_layoutUncertain = true;
    m_layoutFeedback = error + tr(" 请点按“停止共享”以恢复原布局，然后重新开始共享。");
    refreshPersonalUi();
  }, Qt::QueuedConnection);
  connect(personalShortcutPage(), &personal::ShortcutPage::applyRequested, this, &MainWindow::personalShortcutApplyRequested);
  connect(Settings::instance(), &Settings::settingsWritableChanged, this, &MainWindow::refreshPersonalUi);
  connect(&m_coreProcess, &CoreProcess::sourceOperatingSystemChanged, this, [this] { refreshPersonalUi(); }, Qt::QueuedConnection);
  connect(&m_coreProcess, &CoreProcess::serverModifierMappingChanged, this, [this] { refreshPersonalUi(); }, Qt::QueuedConnection);
  setWindowTitle(tr("桌桥 DeskBridge"));
#ifdef Q_OS_MACOS
  setUnifiedTitleAndToolBarOnMac(true);
#endif
  setMinimumSize(900, 620);
  setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
  resize(1060, 720);
  updateText();
  refreshPersonalUi();
  auto *transaction = new personal::ShortcutApplyController(this);
  connect(transaction, &personal::ShortcutApplyController::applyingChanged, this, [this] { refreshPersonalUi(); });
  personal::PreferencesApplyController::Operations operations{
      [](const QStringList &keys) {
        QVariantMap previous;
        for (const auto &key : keys) previous.insert(key, Settings::proxy().value(key));
        return previous;
      },
      [this](const QVariantMap &changes, QString *error) { return writePersonalPreferences(changes, error); },
      [this](const QVariantMap &previous, QString *error) { return restorePersonalPreferences(previous, error); },
      [this] { return m_coreProcess.processState(); },
      [this] { return m_coreProcess.connectionState(); },
      [this] { applyConfig(); m_coreProcess.start(); },
      [this] { m_coreProcess.stop(); }};
  auto *preferences = new personal::PreferencesApplyController(std::move(operations), this);
  connect(&m_coreProcess, &CoreProcess::processStateChanged, preferences,
          &personal::PreferencesApplyController::notifyProcessState, Qt::QueuedConnection);
  connect(&m_coreProcess, &CoreProcess::connectionStateChanged, preferences,
          &personal::PreferencesApplyController::notifyConnectionState, Qt::QueuedConnection);
  connect(&m_coreProcess, &CoreProcess::error, preferences,
          &personal::PreferencesApplyController::notifyStartFailure, Qt::QueuedConnection);
  connect(preferences, &personal::PreferencesApplyController::applyingChanged, this, [this] { refreshPersonalUi(); });
  connect(preferences, &personal::PreferencesApplyController::statusChanged, this, [this](const QString &message) {
    if (m_preferencesTarget == PreferencesTarget::Advanced) m_personalUi->advancedPage()->setStatusMessage(message);
    else if (m_preferencesTarget == PreferencesTarget::Keyboard) personalShortcutPage()->setStatusMessage(message);
    else if (m_preferencesTarget == PreferencesTarget::Connection) m_personalUi->setConnectionFeedback(message);
    else m_personalUi->setPreferencesStatus(message);
  });
  connect(preferences, &personal::PreferencesApplyController::finished, this, [this](bool success, const QString &message) {
    QString feedback = message;
    if (m_certificateMaintenance && !m_certificateMaintenance->backupPath.isEmpty())
      feedback += tr(" 原证书备份：%1").arg(m_certificateMaintenance->backupPath);
    if (m_preferencesTarget == PreferencesTarget::Advanced) m_personalUi->advancedPage()->applied(success, feedback);
    else if (m_preferencesTarget == PreferencesTarget::Keyboard) personalShortcutPage()->setStatusMessage(message);
    else if (m_preferencesTarget == PreferencesTarget::Connection) m_personalUi->setConnectionFeedback(message);
    else m_personalUi->finishPreferencesApply(success, message);
    m_certificateMaintenance.reset();
    m_fingerprint = {QCryptographicHash::Sha256, TlsUtility::certFingerprint()};
    refreshPersonalUi();
    if (m_preferencesTarget == PreferencesTarget::Connection) {
      applyConfig();
    }
    if (success && m_preferencesTarget == PreferencesTarget::Connection) {
      if (m_resetSourceFallbackAfterPreferences) personalShortcutPage()->clearSourceOperatingSystemFallback();
      if (m_openLayoutAfterPreferences) QTimer::singleShot(0, this, [this] { showConfigureServer({}); });
    }
    m_openLayoutAfterPreferences = false;
    m_resetSourceFallbackAfterPreferences = false;
  });
}

void MainWindow::refreshPersonalUi()
{
  if (!m_personalUi) return;
  const auto process = m_coreProcess.processState();
  const bool connected = m_coreProcess.connectionState() == ConnectionState::Connected;
  const bool running = process != ProcessState::Stopped;
  const bool server = m_coreProcess.mode() == CoreMode::Server;
  const auto address = Settings::value(Settings::Client::RemoteHost).toString();
  const auto mapping = deskflow::shortcuts::load();
  const auto detectedOS = m_coreProcess.sourceOperatingSystem();
  const auto sourceOS = deskflow::shortcuts::effectiveSourceOS(mapping, detectedOS);
  const auto *transaction = findChild<personal::ShortcutApplyController *>();
  const auto *preferences = findChild<personal::PreferencesApplyController *>();
  const bool applying = (transaction && transaction->isApplying()) || (preferences && preferences->isApplying());
  m_personalUi->setConfigurationBusy(applying || m_layoutPending, m_layoutUncertain);
  personalShortcutPage()->setApplying(applying || m_layoutPending);
  m_personalUi->advancedPage()->setConnectionSecurity(connected && m_secureSocket);
  m_personalUi->setDeviceContext(sourceOS, server);
  personalShortcutPage()->setSourceOperatingSystem(detectedOS);
  personalShortcutPage()->setServerModifierMapping(m_coreProcess.serverModifierMapping());
  m_personalUi->setConnectionStatus(m_coreProcess.connectionState(), process, TlsUtility::isEnabled(),
                                    m_secureSocket, canRunCore(), address,
                                    Settings::value(Settings::Core::ComputerName).toString());
  auto screenLayout = server && !running ? m_serverConfig.screenLayout() : m_coreProcess.screenLayout();
  if (server && running) {
    const auto configured = m_serverConfig.screenLayout();
    screenLayout.editable = screenLayout.editable && configured.editable;
    if (!configured.editable) screenLayout.error = configured.error;
  }
  if (!Settings::isWritable()) {
    screenLayout.editable = false;
    screenLayout.error = tr("设置文件为只读，无法调整屏幕布局。");
  }
  m_personalUi->setScreenLayout(screenLayout, m_layoutPending, m_layoutFeedback);
  const auto effective = deskflow::shortcuts::effectiveConfiguration(mapping, detectedOS);
  int activeRules = 0;
  for (const auto &rule : effective.rules) {
    if (m_coreProcess.serverModifierMapping() && mapping.habit != deskflow::shortcuts::Habit::Custom &&
        (rule.id == QStringLiteral("copy") || rule.id == QStringLiteral("paste"))) continue;
    bool overlaps = false;
    for (const auto &app : rule.apps) if (mapping.apps.contains(app)) { overlaps = true; break; }
    if (rule.enabled && overlaps) ++activeRules;
  }
  QString fullShortcutStatus;
  if (mapping.fullShortcuts) {
    if (m_coreProcess.serverModifierMapping())
      fullShortcutStatus = tr("完整适配已暂停：服务端正在全局替换修饰键");
    else if (!deskflow::shortcuts::shortcutModifierMapping(mapping, detectedOS))
      fullShortcutStatus = tr("完整适配保持原样：当前无需转换或来源系统尚未确定");
    else if (!mapping.allApplications && mapping.apps.isEmpty())
      fullShortcutStatus = tr("完整适配：请先选择应用或启用“用于全部应用”");
    else
      fullShortcutStatus = mapping.allApplications ? tr("完整快捷键适配 · 全部应用 · Ctrl+空格保留")
                                                  : tr("完整快捷键适配 · 所选应用 · Ctrl+空格保留");
  }
  m_personalUi->setMappingStatus(mapping.enabled && !server, activeRules, fullShortcutStatus);
  m_personalUi->reloadPreferences();
  if (m_personalTrayStatus) {
    const auto trayState = process == ProcessState::Stopping ? tr("○ 正在停止共享…")
        : server ? (running ? tr("● 正在共享本机键鼠") : tr("○ 未启动共享"))
        : (connected ? tr("● 已连接来源电脑") : (running ? tr("○ 正在连接来源电脑…") : tr("○ 未连接来源电脑")));
    m_personalTrayStatus->setText(trayState);
    m_personalTrayMapping->setText(server ? tr("快捷键由接收端适配") :
        (mapping.enabled ? (fullShortcutStatus.isEmpty() ? tr("快捷键映射：已开启 · %1 条规则").arg(activeRules)
                                                       : fullShortcutStatus) : tr("快捷键映射：已关闭")));
    m_personalTrayToggle->setText(server ? (running ? tr("停止共享") : tr("开始共享"))
                                       : (running ? tr("断开连接") : tr("连接来源电脑")));
    m_personalTrayToggle->setEnabled(!applying && !m_layoutPending && process != ProcessState::Stopping && (running || canRunCore()));
    m_trayIcon->setToolTip(server ? tr("桌桥 · 键鼠来源") : (connected ? tr("桌桥 · 已连接") : tr("桌桥 · 未连接")));
  }
}

void MainWindow::openPersonalConnectionOptions()
{
  if (guardPersonalConfigurationChange()) return;
  QDialog dialog(this);
  dialog.setWindowModality(Qt::WindowModal);
  dialog.setWindowTitle(tr("连接选项"));
  dialog.setMinimumWidth(420);
  auto *layout = new QVBoxLayout(&dialog);
  layout->setContentsMargins(24, 22, 24, 20);
  layout->setSpacing(16);
  auto *intro = new QLabel(tr("选择此电脑的角色。来源端提供物理键鼠，接收端负责快捷键适配。"), &dialog);
  intro->setWordWrap(true);
  layout->addWidget(intro);
  auto *form = new QFormLayout;
  auto *role = new QComboBox(&dialog);
  role->setObjectName(QStringLiteral("personalComputerRole"));
  role->addItem(tr("接收另一台电脑的键鼠（客户端）"), static_cast<int>(CoreMode::Client));
  role->addItem(tr("共享本机物理键鼠（服务端）"), static_cast<int>(CoreMode::Server));
  role->setCurrentIndex(qMax(0, role->findData(static_cast<int>(m_coreProcess.mode()))));
  role->setEnabled(Settings::isWritable());
  form->addRow(tr("本机角色"), role);
  form->addRow(tr("本机系统"), new QLabel(personal::displayOperatingSystem(deskflow::shortcuts::localOS()), &dialog));
  auto *host = new QLineEdit(Settings::value(Settings::Client::RemoteHost).toString(), &dialog);
  host->setPlaceholderText(tr("键鼠来源电脑的 IP 地址或主机名"));
  host->setEnabled(Settings::isWritable());
  form->addRow(tr("来源电脑地址"), host);
  auto *computer = new QLabel(Settings::value(Settings::Core::ComputerName).toString(), &dialog);
  computer->setTextInteractionFlags(Qt::TextSelectableByMouse);
  form->addRow(tr("本机屏幕名称"), computer);
  layout->addLayout(form);
  auto *retry = new QCheckBox(tr("连接中断时逐步增加重试间隔"), &dialog);
  retry->setObjectName(QStringLiteral("personalDynamicRetry"));
  retry->setChecked(Settings::value(Settings::Client::DynamicConnectionRetry).toBool());
  layout->addWidget(retry);
  auto *serverLayout = new QPushButton(tr("保存并编辑屏幕布局…"), &dialog);
  serverLayout->setObjectName(QStringLiteral("personalServerLayout"));
  layout->addWidget(serverLayout, 0, Qt::AlignLeft);
  auto *schemes = new QPushButton(tr("来源系统与一键快捷键方案…"), &dialog);
  layout->addWidget(schemes, 0, Qt::AlignLeft);
  auto *note = new QLabel(tr("角色不会自动接管另一台电脑的物理键鼠。更改地址或角色会短暂重启共享。"), &dialog);
  note->setWordWrap(true);
  layout->addWidget(note);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
  auto *save = buttons->button(QDialogButtonBox::Save);
  save->setText(tr("保存"));
  auto updateRoleControls = [role, host, retry, serverLayout, save] {
    const bool server = static_cast<CoreMode>(role->currentData().toInt()) == CoreMode::Server;
    host->setEnabled(Settings::isWritable() && !server);
    retry->setEnabled(Settings::isWritable() && !server);
    serverLayout->setEnabled(Settings::isWritable() && server);
    save->setEnabled(Settings::isWritable() && (server || !host->text().trimmed().isEmpty()));
  };
  connect(host, &QLineEdit::textChanged, &dialog, updateRoleControls);
  connect(role, &QComboBox::currentIndexChanged, &dialog, updateRoleControls);
  updateRoleControls();
  bool openLayout = false;
  bool openSchemes = false;
  connect(serverLayout, &QPushButton::clicked, &dialog, [&] { openLayout = true; dialog.accept(); });
  connect(schemes, &QPushButton::clicked, &dialog, [&] { openSchemes = true; dialog.reject(); });
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);
  if (dialog.exec() != QDialog::Accepted) {
    if (openSchemes) m_personalUi->showPage(personal::PersonalWindow::Shortcuts);
    return;
  }
  const QString address = host->text().trimmed();
  const auto mode = static_cast<CoreMode>(role->currentData().toInt());
  const bool modeChanged = m_coreProcess.mode() != mode;
  const bool addressChanged = mode == CoreMode::Client && address != Settings::value(Settings::Client::RemoteHost).toString();
  QVariantMap changes;
  if (modeChanged) changes.insert(Settings::Core::CoreMode, QVariant::fromValue(mode));
  if (retry->isChecked() != Settings::value(Settings::Client::DynamicConnectionRetry).toBool())
    changes.insert(Settings::Client::DynamicConnectionRetry, retry->isChecked());
  if (addressChanged) {
    QString error;
    auto next = deskflow::shortcuts::load(&error);
    if (!error.isEmpty()) {
      QMessageBox::warning(this, tr("无法更换来源电脑"), tr("请先修复快捷键配置：%1").arg(error));
      return;
    }
    if (next.sourceOS != deskflow::shortcuts::DeviceOS::Unknown) {
      next.sourceOS = deskflow::shortcuts::DeviceOS::Unknown;
      changes.insert(Settings::Client::ShortcutConfig, deskflow::shortcuts::toJson(next));
    }
    changes.insert(Settings::Client::RemoteHost, address);
  }
  m_openLayoutAfterPreferences = openLayout;
  m_resetSourceFallbackAfterPreferences = addressChanged;
  applyPersonalPreferences(changes, PreferencesTarget::Connection);
}

bool MainWindow::guardPersonalConfigurationChange()
{
  if (!m_personalUi) return false;
  if (auto *preferences = findChild<personal::PreferencesApplyController *>(); preferences && preferences->isApplying()) {
    showAndActivate();
    m_personalUi->showPage(m_preferencesTarget == PreferencesTarget::General ? personal::PersonalWindow::General
                          : m_preferencesTarget == PreferencesTarget::Keyboard ? personal::PersonalWindow::Shortcuts
                          : m_preferencesTarget == PreferencesTarget::Connection ? personal::PersonalWindow::Connection
                                                                               : personal::PersonalWindow::Advanced);
    return true;
  }
  if (m_layoutPending) {
    showAndActivate();
    m_personalUi->showPage(personal::PersonalWindow::Connection);
    return true;
  }
  const auto *transaction = findChild<personal::ShortcutApplyController *>();
  if (!transaction || !transaction->isApplying()) return false;
  showAndActivate();
  m_personalUi->showPage(personal::PersonalWindow::Shortcuts);
  personalShortcutPage()->setStatusMessage(tr("正在应用更改，请完成后再调整连接或设置。"));
  return true;
}

void MainWindow::requestPersonalScreenLayout(deskflow::layout::Direction direction)
{
  if (guardPersonalConfigurationChange()) return;
  m_layoutFeedback.clear();
  m_layoutUncertain = false;
  if (m_coreProcess.mode() == CoreMode::Server && m_coreProcess.processState() == ProcessState::Stopped) {
    QString error;
    if (m_layoutPersistence->prepare(m_serverConfig.screenLayout().direction, direction, &error)) {
      m_layoutPersistence->finish(true);
      m_layoutFeedback = tr("屏幕布局已保存，下次开始共享时生效。");
    } else {
      m_layoutFeedback = error;
    }
  } else {
    m_layoutPending = true;
    if (!m_coreProcess.requestLayout(direction)) {
      m_layoutPending = false;
      m_layoutFeedback = tr("暂时无法修改来源电脑的布局，请确认双方均运行配套 DeskBridge 并已连接。");
    }
  }
  refreshPersonalUi();
}

void MainWindow::requestPersonalScreenMove(const deskflow::layout::Move &move)
{
  if (guardPersonalConfigurationChange()) return;
  m_layoutFeedback.clear();
  m_layoutUncertain = false;
  if (m_coreProcess.mode() == CoreMode::Server && m_coreProcess.processState() == ProcessState::Stopped) {
    QString error;
    if (m_layoutPersistence->prepareMove(move, &error)) {
      m_layoutPersistence->finish(true);
      m_layoutFeedback = tr("屏幕布局已保存，下次开始共享时生效。");
    } else {
      m_layoutFeedback = error;
    }
  } else {
    m_layoutPending = true;
    if (!m_coreProcess.requestLayoutMove(move)) {
      m_layoutPending = false;
      m_layoutFeedback = tr("暂时无法修改来源电脑的布局，请确认双方均运行配套 DeskBridge 并已连接。");
    }
  }
  refreshPersonalUi();
}

void MainWindow::persistPersonalScreenMove(quint32 requestId, const deskflow::layout::Move &move)
{
  const auto *shortcuts = findChild<personal::ShortcutApplyController *>();
  const auto *preferences = findChild<personal::PreferencesApplyController *>();
  QString error;
  if (m_coreProcess.mode() != CoreMode::Server || m_serverConfigDialogVisible || QApplication::activeModalWidget() ||
      (shortcuts && shortcuts->isApplying()) || (preferences && preferences->isApplying()) || m_layoutPersistence->isPending()) {
    error = tr("来源电脑正在修改设置，请稍后重新调整布局。");
  } else if (m_layoutPersistence->prepareMove(move, &error)) {
    m_layoutRequestId = requestId;
    m_layoutPending = true;
  }
  m_coreProcess.completeLayoutRequest(requestId, error.isEmpty(), error);
  refreshPersonalUi();
}

void MainWindow::persistPersonalScreenLayout(quint32 requestId, deskflow::layout::Direction expected,
                                           deskflow::layout::Direction desired)
{
  const auto *shortcuts = findChild<personal::ShortcutApplyController *>();
  const auto *preferences = findChild<personal::PreferencesApplyController *>();
  QString error;
  if (m_coreProcess.mode() != CoreMode::Server || m_serverConfigDialogVisible || QApplication::activeModalWidget() ||
      (shortcuts && shortcuts->isApplying()) || (preferences && preferences->isApplying()) || m_layoutPersistence->isPending()) {
    error = tr("来源电脑正在修改设置，请稍后重新调整布局。");
  } else if (m_layoutPersistence->prepare(expected, desired, &error)) {
    m_layoutRequestId = requestId;
    m_layoutPending = true;
  }
  m_coreProcess.completeLayoutRequest(requestId, error.isEmpty(), error);
  refreshPersonalUi();
}

void MainWindow::restoreWindow()
{
  if (m_personalUi) {
    auto saved = Settings::value(Settings::Gui::WindowGeometry).toRect();
    const auto available = QGuiApplication::primaryScreen()->availableGeometry();
    setMinimumSize(900, 620);
    setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    if (!saved.isValid() || saved.width() < 900 || saved.height() < 620) {
      resize(QSize(1060, 720).boundedTo(available.size()));
      move(available.center() - rect().center());
    } else {
      saved.setSize(saved.size().boundedTo(available.size()));
      setGeometry(saved);
      if (!available.contains(frameGeometry())) move(available.center() - rect().center());
    }
    return;
  }
  auto windowGeometry = Settings::value(Settings::Gui::WindowGeometry).toRect();
  const auto totalGeometry = QGuiApplication::primaryScreen()->availableGeometry();
  if (!windowGeometry.isValid()) {
    adjustSize();
    windowGeometry = geometry();
  } else {
    setGeometry(windowGeometry);
  }
  m_expandedSize = geometry().size();

  if (!totalGeometry.contains(windowGeometry)) {
    QRect screenGeometry = QGuiApplication::primaryScreen()->geometry();
    move(screenGeometry.center() - rect().center());
  }

  if (!Settings::value(Settings::Gui::LogExpanded).toBool())
    setFixedSize(size());
}

void MainWindow::setupControls()
{
  secureSocket(false);

  ui->btnConfigureServer->setIcon(QIcon::fromTheme(QStringLiteral("configure")));
  ui->btnConfigureClient->setIcon(QIcon::fromTheme(QStringLiteral("configure")));

  if (Settings::value(Settings::Core::LastVersion).toString() != kVersion) {
    Settings::setValue(Settings::Core::LastVersion, kVersion);
  }

  if (!Settings::value(Settings::Gui::LogExpanded).toBool()) {
    m_logDock->hide();
  }

  ui->serverOptions->setVisible(false);
  ui->clientOptions->setVisible(false);

  const auto coreMode = Settings::value(Settings::Core::CoreMode).value<Settings::CoreMode>();
  ui->rbModeClient->setChecked(coreMode == Settings::CoreMode::Client);
  ui->rbModeServer->setChecked(coreMode == Settings::CoreMode::Server);

  ui->lineEditName->setValidator(new QRegularExpressionValidator(m_nameRegEx, this));
  ui->lineEditName->setVisible(false);
  ui->lineEditName->installEventFilter(this);

  if (deskflow::platform::isMac()) {
    ui->rbModeServer->setAttribute(Qt::WA_MacShowFocusRect, false);
    ui->rbModeClient->setAttribute(Qt::WA_MacShowFocusRect, false);
    ui->btnSaveServerConfig->setFixedWidth(ui->btnSaveServerConfig->height());
  } else {
    ui->btnSaveServerConfig->setIconSize(QSize(22, 22));
  }
  setStatusBar(m_statusBar);
}

//////////////////////////////////////////////////////////////////////////////
// Begin slots
//////////////////////////////////////////////////////////////////////////////

// remember: using queued connection allows the render loop to run before
// executing the slot. the default is to instantly call the slot when the
// signal is emitted from the thread that owns the receiver's object.
void MainWindow::connectSlots()
{
  connect(Settings::instance(), &Settings::serverSettingsChanged, this, &MainWindow::serverConfigSaving);
  connect(Settings::instance(), &Settings::settingsChanged, this, &MainWindow::settingsChanged);

  connect(&m_coreProcess, &CoreProcess::error, this, &MainWindow::coreProcessError);
  connect(&m_coreProcess, &CoreProcess::logLine, this, &MainWindow::handleLogLine);
  connect(
      &m_coreProcess, &CoreProcess::processStateChanged, this, &MainWindow::coreProcessStateChanged,
      Qt::QueuedConnection
  );
  connect(&m_coreProcess, &CoreProcess::connectionStateChanged, this, &MainWindow::coreConnectionStateChanged);
  connect(&m_coreProcess, &CoreProcess::secureSocket, this, &MainWindow::secureSocket);
  connect(
      &m_coreProcess, &CoreProcess::daemonIpcClientConnectionFailed, this, &MainWindow::daemonIpcClientConnectionFailed
  );
  connect(&m_coreProcess, &CoreProcess::securityLevelChanged, m_statusBar, &StatusBar::setSecurityLevel);

  connect(m_actionAbout, &QAction::triggered, this, &MainWindow::openAboutDialog);
  connect(m_actionMinimize, &QAction::triggered, this, &MainWindow::hide);

  connect(m_actionQuit, &QAction::triggered, this, &MainWindow::close);
  connect(m_actionTrayQuit, &QAction::triggered, this, &MainWindow::close);
  connect(m_actionRestore, &QAction::triggered, this, &MainWindow::showAndActivate);
  connect(m_actionSettings, &QAction::triggered, this, &MainWindow::openSettings);
  connect(m_actionStartCore, &QAction::triggered, this, &MainWindow::startCore);
  connect(m_actionRestartCore, &QAction::triggered, this, &MainWindow::resetCore);
  connect(m_actionStopCore, &QAction::triggered, this, &MainWindow::stopCore);
  connect(m_actionShowHelp, &QAction::triggered, this, &MainWindow::showHelpViewer);

  // Mac os tray will only show a menu
  if (!deskflow::platform::isMac())
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &MainWindow::trayIconActivated);

  connect(&m_coreProcess, &CoreProcess::connectedClientsChanged, this, &MainWindow::serverClientsChanged);
  connect(&m_coreProcess, &CoreProcess::unrecognisedClient, this, &MainWindow::handleUnrecognisedClient);
  connect(&m_coreProcess, &CoreProcess::connectionRefused, this, &MainWindow::handleConnectionRefused);
  connect(&m_coreProcess, &CoreProcess::retryIn, this, &MainWindow::updateTimeoutDelay);
  connect(&m_coreProcess, &CoreProcess::peerFingerprint, this, &MainWindow::handlePeerFingerprint);
  connect(&m_coreProcess, &CoreProcess::missingKeyboardLayouts, this, &MainWindow::handleMissingKeyboardLayouts);

  if (Settings::value(Settings::Gui::AutoStartCore).toBool()) {
    connect(ui->btnToggleCore, &QPushButton::clicked, m_actionStopCore, &QAction::trigger, Qt::UniqueConnection);
  } else {
    connect(ui->btnToggleCore, &QPushButton::clicked, m_actionStartCore, &QAction::trigger, Qt::UniqueConnection);
  }

  connect(ui->btnRestartCore, &QPushButton::clicked, this, &MainWindow::resetCore);

  connect(ui->lineHostname, &QLineEdit::returnPressed, ui->btnRestartCore, &QPushButton::click);
  connect(ui->lineHostname, &QLineEdit::textChanged, this, &MainWindow::remoteHostChanged);

  connect(ui->btnSaveServerConfig, &QPushButton::clicked, this, &MainWindow::saveServerConfig);
  connect(ui->btnConfigureServer, &QPushButton::clicked, this, [this] { showConfigureServer(""); });
  connect(ui->btnConfigureClient, &QPushButton::clicked, this, [this] { showConfigureClient(); });
  connect(ui->lblComputerName, &QLabel::linkActivated, this, &MainWindow::openSettings);

  connect(ui->rbModeServer, &QRadioButton::toggled, this, &MainWindow::coreModeToggled);
  connect(ui->rbModeClient, &QRadioButton::toggled, this, &MainWindow::coreModeToggled);

  connect(m_logDock->toggleViewAction(), &QAction::toggled, this, &MainWindow::toggleLogVisible);

  connect(m_statusBar, &StatusBar::requestShowMyFingerprints, this, &MainWindow::showMyFingerprint);
  connect(m_statusBar, &StatusBar::requestUpdateVersion, this, &MainWindow::openGetNewVersionUrl);
  connect(&m_versionChecker, &VersionChecker::updateFound, m_statusBar, &StatusBar::updateFound);

  connect(m_guiDupeChecker, &QLocalServer::newConnection, this, &MainWindow::showAndActivate);

  connect(ui->btnEditName, &QPushButton::clicked, this, &MainWindow::showHostNameEditor);

  connect(ui->lineEditName, &QLineEdit::editingFinished, this, &MainWindow::setHostName);

  connect(m_networkMonitor, &NetworkMonitor::ipAddressesChanged, this, &MainWindow::updateIpLabel);
}

void MainWindow::toggleLogVisible(bool visible)
{
  if (m_personalUi) return;
  // When the main window is hidden e.g. close to tray / minimized), this also triggers the log visibility toggle,
  // but we don't want to hide the log in this case since we would need to un-hide it when the window is shown again.
  if (!isVisible() || isMinimized()) {
    qDebug() << "not toggling log, window not visible";
    return;
  }

  setFixedSize(16777215, 16777215);
  Settings::setValue(Settings::Gui::LogExpanded, visible);
  if (visible) {
    if (m_logDock->isFloating()) {
      adjustSize();
      setFixedSize(size());
    } else {
      QTimer::singleShot(15, this, [&] { resize(m_expandedSize); });
    }
  } else {
    if (!m_logDock->isFloating()) {
      m_expandedSize = geometry().size();
    }
    m_logDock->hide();
    if (!m_logDock->isFloating()) {
      adjustSize();
    }
    setFixedSize(size());
  }
  Settings::setValue(Settings::Gui::WindowGeometry, geometry());
}

void MainWindow::settingsChanged(const QString &key)
{
  refreshPersonalUi();
  if (key == Settings::Log::Level) {
    m_coreProcess.applyLogLevel();
    return;
  }

  if (key == Settings::Core::ComputerName)
    updateComputerName();

  if ((key == Settings::Security::Certificate) || (key == Settings::Security::KeySize) ||
      (key == Settings::Security::TlsEnabled) || (key == Settings::Security::CheckPeers)) {
    if (TlsUtility::isEnabled()) {
      if (!m_personalUi && !TlsUtility::isCertValid()) {
        qWarning() << tr("invalid certificate, generating a new one");
        TlsUtility::generateCertificate();
      }
      m_fingerprint = {QCryptographicHash::Sha256, TlsUtility::certFingerprint()};
      updateFingerprintButton();
    }
    updateSecurityIcon(m_statusBar->securityIconVisible());
    return;
  }
}

void MainWindow::serverConfigSaving()
{
  m_serverConfig.commit();
}

void MainWindow::trayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
  if (reason != QSystemTrayIcon::Trigger)
    return;
  isVisible() ? hide() : showAndActivate();
}

void MainWindow::coreProcessError(CoreProcess::Error error)
{
  if (auto *preferences = findChild<personal::PreferencesApplyController *>(); preferences && preferences->isApplying())
    return;
  if (m_personalUi) {
    if (auto *transaction = findChild<personal::ShortcutApplyController *>(); transaction && transaction->isApplying())
      return;
  }
  if (error == CoreProcess::Error::AddressMissing) {
    QMessageBox::warning(
        this, tr("Address missing"), tr("Please enter the hostname or IP address of the other computer.")
    );
  } else if (error == CoreProcess::Error::StartFailed) {
    show();
    auto message = tr("The Core executable could not be started.\n"
                      "Please check if you have sufficient permissions to run %1.")
                       .arg(kCoreBinName);

    if (Settings::value(Settings::Core::CoreMode) == Settings::CoreMode::Server) {
      const auto mode =
          Settings::value(Settings::Server::ExternalConfigFile).toBool() ? tr("read") : tr("read and write");
      message.append(tr("\nAdditionally, check you are able to %1 the server config file: %2")
                         .arg(mode, Settings::serverConfigFile()));
    }
    QMessageBox::warning(this, kAppName, message);
  }
}

void MainWindow::startCore()
{
  if (guardPersonalConfigurationChange()) return;
  // Save current IP state when server starts
  if (m_coreProcess.mode() == CoreMode::Server && Settings::value(Settings::Core::Interface).toString().isEmpty()) {
    m_serverStartIPs = NetworkMonitor::validAddresses();
    m_serverStartSuggestedIP = m_serverStartIPs.isEmpty() ? "" : m_serverStartIPs.first();
  }

  m_actionStartCore->setVisible(false);
  m_actionRestartCore->setVisible(true);
  m_coreProcess.start();
}

void MainWindow::stopCore()
{
  if (m_layoutUncertain) {
    m_coreProcess.stop();
    return;
  }
  if (guardPersonalConfigurationChange()) return;
  qDebug() << "stopping core process";
  m_coreProcess.stop();
  m_actionStartCore->setVisible(true);
  m_actionRestartCore->setVisible(false);
}

void MainWindow::clearSettings()
{
  qDebug() << "clearing settings";

  m_networkMonitor->stopMonitoring();

  disconnect(&m_coreProcess, nullptr, this, nullptr);
  disconnect(&m_versionChecker, nullptr, this, nullptr);
  disconnect(m_guiDupeChecker, nullptr, this, nullptr);
  disconnect(m_trayIcon, nullptr, this, nullptr);
  disconnect(m_logDock->toggleViewAction(), nullptr, this, nullptr);

  m_coreProcess.stop();
  m_coreProcess.clearSettings();

  m_saveOnExit = false;
  diagnostic::clearSettings(true);
}

bool MainWindow::saveServerConfig()
{
  QString fileName = QFileDialog::getSaveFileName(this, tr("Save server configuration as..."));

  if (!fileName.isEmpty() && !m_serverConfig.save(fileName)) {
    QMessageBox::warning(this, tr("Save failed"), tr("Could not save server configuration to file."));
    return true;
  }

  return false;
}

void MainWindow::openAboutDialog()
{
  AboutDialog about(this);
  about.exec();
}

void MainWindow::openGetNewVersionUrl() const
{
  QDesktopServices::openUrl(QUrl(kUrlDownload));
}

void MainWindow::openSettings()
{
  if (m_personalUi) {
    showAndActivate();
    m_personalUi->showPage(personal::PersonalWindow::General);
    return;
  }
  openAdvancedSettings();
}

void MainWindow::openAdvancedSettings()
{
  if (m_personalUi) {
    showAndActivate();
    m_personalUi->showPage(personal::PersonalWindow::Advanced);
    return;
  }
  if (guardPersonalConfigurationChange()) return;
  auto dialog = SettingsDialog(this, m_serverConfig, m_logDock->logDocument());

  connect(&dialog, &SettingsDialog::fingerprintRequested, &dialog, [this, &dialog] {
    FingerprintDialog fingerprintDialog(&dialog, m_fingerprint);
    fingerprintDialog.exec();
  });
  connect(&dialog, &SettingsDialog::requestRemoveAllSettings, this, &MainWindow::clearSettings, Qt::UniqueConnection);
  if (dialog.exec() == QDialog::Accepted) {
    Settings::save();
    disconnect(&dialog, &SettingsDialog::requestRemoveAllSettings, nullptr, nullptr);

    applyConfig();

    if (m_coreProcess.isStarted()) {
      m_coreProcess.restart();
    }
    refreshPersonalUi();
  }
}

void MainWindow::applyPersonalPreferences(const QVariantMap &changes, PreferencesTarget target)
{
  if (guardPersonalConfigurationChange()) return;
  m_preferencesTarget = target;
  auto report = [this, target](const QString &message) {
    if (target == PreferencesTarget::Advanced) m_personalUi->advancedPage()->setStatusMessage(message);
    else if (target == PreferencesTarget::Keyboard) personalShortcutPage()->setStatusMessage(message);
    else if (target == PreferencesTarget::Connection) m_personalUi->setConnectionFeedback(message);
    else m_personalUi->setPreferencesStatus(message);
  };
  QString error;
  auto ordinary = changes;
  if (target == PreferencesTarget::Connection) {
    ordinary.remove(Settings::Client::RemoteHost);
    ordinary.remove(Settings::Core::CoreMode);
    ordinary.remove(Settings::Client::ShortcutConfig);
    const auto mode = changes.value(Settings::Core::CoreMode, Settings::value(Settings::Core::CoreMode)).value<CoreMode>();
    if ((mode != CoreMode::Client && mode != CoreMode::Server) ||
        (mode == CoreMode::Client && changes.value(Settings::Client::RemoteHost, Settings::value(Settings::Client::RemoteHost)).toString().trimmed().isEmpty()) ||
        (changes.contains(Settings::Client::ShortcutConfig) && !deskflow::shortcuts::fromJson(changes.value(Settings::Client::ShortcutConfig).toString(), &error))) {
      report(error.isEmpty() ? tr("连接角色或来源地址无效。") : error);
      return;
    }
  }
  if (!Settings::isWritable() || !personal::validatePreferenceChanges(ordinary, &error)) {
    report(error.isEmpty() ? tr("设置文件为只读，无法应用更改。") : error);
    return;
  }
  if (changes.value(Settings::Security::TlsEnabled, TlsUtility::isEnabled()).toBool() &&
      changes.contains(Settings::Security::TlsEnabled) && !TlsUtility::isCertValid()) {
    report(tr("本机证书无效，请先在“证书维护”中明确生成证书，再启用加密。"));
    return;
  }
  const bool forceReconnect = target == PreferencesTarget::Connection &&
      (changes.contains(Settings::Client::RemoteHost) || changes.contains(Settings::Core::CoreMode));
  findChild<personal::PreferencesApplyController *>()->apply(changes, forceReconnect);
}

void MainWindow::openKeyboardCompatibility()
{
  if (guardPersonalConfigurationChange()) return;
  QDialog dialog(this);
  dialog.setWindowModality(Qt::WindowModal);
  dialog.setWindowTitle(tr("远程键盘布局兼容"));
  dialog.setMinimumWidth(420);
  auto *layout = new QVBoxLayout(&dialog);
  auto *sync = new QCheckBox(tr("同步来源电脑的键盘布局"), &dialog);
  sync->setObjectName(QStringLiteral("keyboardLayoutSync"));
  sync->setChecked(Settings::value(Settings::Client::LanguageSync).toBool());
  sync->setEnabled(Settings::isWritable());
  layout->addWidget(sync);
  auto *note = new QLabel(tr("用于兼容来源端的键盘布局标识，与界面语言、轻按 Shift 切换中英文相互独立。部分旧版来源端或本机未安装的布局可能无法同步。更改后将短暂重新连接。"), &dialog);
  note->setWordWrap(true);
  layout->addWidget(note);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
  buttons->button(QDialogButtonBox::Save)->setEnabled(Settings::isWritable());
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);
  if (dialog.exec() == QDialog::Accepted)
    applyPersonalPreferences({{Settings::Client::LanguageSync, sync->isChecked()}}, PreferencesTarget::Keyboard);
}

void MainWindow::regeneratePersonalCertificate(const QString &path, int bits)
{
  if (guardPersonalConfigurationChange()) return;
  auto *page = m_personalUi->advancedPage();
  if (!Settings::isWritable() || page->isDirty()) {
    page->setStatusMessage(tr("请先应用或还原高级设置草稿，再维护证书。"));
    return;
  }
  const QFileInfo target(path);
  if (!target.isAbsolute() || target.isSymLink() || target.isDir() || (bits != 2048 && bits != 4096)) {
    page->setStatusMessage(tr("证书路径必须是本机的绝对文件路径，密钥长度为 2048 或 4096。"));
    return;
  }
  CertificateMaintenance maintenance;
  maintenance.path = path;
  maintenance.bits = bits;
  m_certificateMaintenance = maintenance;
  m_preferencesTarget = PreferencesTarget::Advanced;
  findChild<personal::PreferencesApplyController *>()->apply(
      {{Settings::Security::Certificate, path}, {Settings::Security::KeySize, bits}}, true, false);
}

bool MainWindow::writePersonalPreferences(const QVariantMap &changes, QString *error)
{
  if (!m_certificateMaintenance) {
    if (m_preferencesTarget == PreferencesTarget::Connection) return Settings::trySetValues(changes, error);
    return personal::commitPreferenceChanges(changes, error);
  }
  auto &maintenance = *m_certificateMaintenance;
  auto fail = [error](const QString &message) { if (error) *error = message; return false; };
  QFileInfo info(maintenance.path);
  if (info.isSymLink() || info.isDir()) return fail(tr("证书目标已更改，操作已取消。"));
  maintenance.existed = info.exists();
  if (maintenance.existed) {
    QFile previous(maintenance.path);
    if (!previous.open(QIODevice::ReadOnly)) return fail(tr("无法备份原证书，原证书已保留。"));
    maintenance.previous = previous.readAll();
    QTemporaryFile backup(QFileInfo(maintenance.path).absolutePath() + QStringLiteral("/deskbridge-certificate-backup-XXXXXX.pem"));
    if (!backup.open() || !backup.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
        backup.write(maintenance.previous) != maintenance.previous.size() || !backup.flush())
      return fail(tr("无法保留原证书备份，操作已取消。"));
    maintenance.backupPath = backup.fileName();
    backup.setAutoRemove(false);
  }
  if (!QDir().mkpath(info.absolutePath())) return fail(tr("无法创建证书所在目录。"));
  QTemporaryFile generated(info.absolutePath() + QStringLiteral("/.deskbridge-certificate-XXXXXX"));
  if (!generated.open()) return fail(tr("无法创建临时证书文件。"));
  const auto temporaryPath = generated.fileName();
  generated.close();
  try {
    deskflow::generatePemSelfSignedCert(temporaryPath, maintenance.bits);
  } catch (const std::exception &) {
    return fail(tr("无法生成新证书，原证书已保留。"));
  }
  if (!generated.open() || (maintenance.generated = generated.readAll()).isEmpty())
    return fail(tr("无法读取新证书，原证书已保留。"));
  const auto certificates = QSslCertificate::fromData(maintenance.generated);
  const QSslKey privateKey(maintenance.generated, QSsl::Rsa);
  if (certificates.isEmpty() || certificates.first().publicKey().length() != maintenance.bits || privateKey.isNull() ||
      privateKey.length() != maintenance.bits)
    return fail(tr("新证书校验失败，原证书已保留。"));
  QSaveFile certificate(maintenance.path);
  if (!certificate.open(QIODevice::WriteOnly)) return fail(tr("无法写入证书位置，原证书已保留。"));
  if (!certificate.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner))
    return fail(tr("无法设置证书文件权限，原证书已保留。"));
  if (certificate.write(maintenance.generated) != maintenance.generated.size() || !certificate.commit())
    return fail(tr("无法保存证书，原证书已保留。"));
  maintenance.written = true;
  if (!Settings::trySetValues(changes, error)) {
    QString restoreError;
    if (!restoreCertificateFile(&restoreError) && error) *error += QStringLiteral(" ") + restoreError;
    return false;
  }
  return true;
}

bool MainWindow::restoreCertificateFile(QString *error)
{
  if (!m_certificateMaintenance || !m_certificateMaintenance->written) return true;
  auto &maintenance = *m_certificateMaintenance;
  QFile current(maintenance.path);
  if (!current.open(QIODevice::ReadOnly) || current.readAll() != maintenance.generated) {
    if (error) *error = tr("证书文件已被其他程序修改，无法自动覆盖恢复。原证书备份：%1").arg(maintenance.backupPath);
    return false;
  }
  current.close();
  if (maintenance.existed) {
    QSaveFile previous(maintenance.path);
    if (!previous.open(QIODevice::WriteOnly) ||
        !previous.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
        previous.write(maintenance.previous) != maintenance.previous.size() || !previous.commit()) {
      if (error) *error = tr("无法恢复原证书文件。");
      return false;
    }
  } else if (!QFile::remove(maintenance.path)) {
    if (error) *error = tr("无法移除本次新建的证书文件。");
    return false;
  }
  maintenance.written = false;
  return true;
}

bool MainWindow::restorePersonalPreferences(const QVariantMap &previous, QString *error)
{
  if (!restoreCertificateFile(error)) return false;
  return Settings::trySetValues(previous, error);
}

void MainWindow::resetCore()
{
  if (guardPersonalConfigurationChange()) return;
  m_coreProcess.restart();
}

void MainWindow::showMyFingerprint()
{
  FingerprintDialog fingerprintDialog(this, m_fingerprint);
  fingerprintDialog.exec();
}

void MainWindow::coreModeToggled(bool checked)
{
  // this method is called when rbClient or rbServer toggles
  // with both being in the same group one must be turned on if the other is turned off
  // only react to toggle on to avoid calling everything twice when the user switches modes
  if (!checked)
    return;

  Settings::CoreMode mode = Settings::CoreMode::None;

  if (ui->rbModeServer->isChecked())
    mode = Settings::CoreMode::Server;
  if (ui->rbModeClient->isChecked())
    mode = Settings::CoreMode::Client;

  qDebug() << QStringLiteral("change mode to: %1").arg(QVariant::fromValue(mode).toString());

  if (m_coreProcess.isStarted() && m_coreProcess.mode() != mode)
    m_coreProcess.stop();
  m_coreProcess.setMode(mode);

  Settings::setValue(Settings::Core::CoreMode, mode);
  Settings::save();

  updateModeControls();
}

void MainWindow::updateModeControls()
{
  const auto mode = m_coreProcess.mode();
  const bool isServer = mode == Settings::CoreMode::Server;
  const bool isClient = mode == Settings::CoreMode::Client;

  ui->serverOptions->setVisible(isServer);
  ui->clientOptions->setVisible(isClient);
  ui->lblNoMode->setVisible(!isServer && !isClient);
  toggleCanRunCore(canRunCore());

  ui->lblIpAddresses->setVisible(
      (isClient && !Settings::value(Settings::Core::Interface).toString().isEmpty()) || isServer
  );

  if (ui->lblIpAddresses->isVisible())
    updateNetworkInfo();

  if (isServer) {
    m_networkMonitor->startMonitoring();
  } else {
    m_networkMonitor->stopMonitoring();
  }

  if (isServer || isClient)
    updateModeControlLabels();
}

void MainWindow::updateModeControlLabels()
{
  const bool isServer = m_coreProcess.mode() == CoreMode::Server;
  const bool isStarted = m_coreProcess.isStarted();

  QString startText;
  QString stopText;
  QIcon startIcon;
  QIcon stopIcon;

  if (isServer) {
    startText = tr("Start");
    stopText = tr("Stop");
    startIcon = QIcon::fromTheme(QStringLiteral("system-run"));
    stopIcon = QIcon::fromTheme(QIcon::ThemeIcon::ProcessStop);
  } else {
    startText = tr("Connect");
    stopText = tr("Disconnect");
    startIcon = QIcon::fromTheme(QStringLiteral("network-connect"));
    stopIcon = QIcon::fromTheme(QStringLiteral("network-disconnect"));
  }

  m_actionStartCore->setText(startText);
  m_actionStartCore->setIcon(startIcon);
  m_actionStopCore->setText(stopText);
  m_actionStopCore->setIcon(stopIcon);

  if (isStarted) {
    ui->btnToggleCore->setText(stopText);
    ui->btnToggleCore->setIcon(stopIcon);
  } else {
    ui->btnToggleCore->setText(startText);
    ui->btnToggleCore->setIcon(startIcon);
  }
}

void MainWindow::updateSecurityIcon(bool visible)
{
  m_statusBar->setSecurityIconVisible(visible);
  if (!visible)
    return;
  m_statusBar->setSecurityIcon(TlsUtility::isEnabled());
}

void MainWindow::updateNetworkInfo()
{
  const auto mode = m_coreProcess.mode();
  if (mode == CoreMode::None) {
    return;
  }

  if (mode == CoreMode::Server)
    updateIpLabel(NetworkMonitor::validAddresses());
  else
    updateIpLabel({Settings::value(Settings::Core::Interface).toString()});
}

void MainWindow::serverConnectionConfigureClient(const QString &clientName)
{
  m_serverConfigDialogVisible = true;
  ServerConfigDialog dialog(this, m_serverConfig);
  if (dialog.addClient(clientName) && dialog.exec() == QDialog::Accepted) {
    m_coreProcess.restart();
  }
  m_serverConfigDialogVisible = false;
}

//////////////////////////////////////////////////////////////////////////////
// End slots
//////////////////////////////////////////////////////////////////////////////

void MainWindow::open()
{
  if (!Settings::value(Settings::Gui::Autohide).toBool())
    showAndActivate();
  else if (deskflow::platform::isMac())
    // macOS to call hide after this function ends
    QTimer::singleShot(1, this, &MainWindow::hide);
  else
    hide();

  // if a critical error was shown just before the main window (i.e. on app
  // load), it will be hidden behind the main window. therefore we need to raise
  // it up in front of the main window.
  // HACK: because the `onShown` event happens just as the window is shown, the
  // message box has a chance of being raised under the main window. to solve
  // this we delay the error dialog raise by a split second. this seems a bit
  // hacky and fragile, so maybe there's a better approach.
  const auto kCriticalDialogDelay = 100;
  QTimer::singleShot(kCriticalDialogDelay, this, &messages::raiseCriticalDialog);

  if (!m_personalUi && !Settings::value(Settings::Gui::AutoUpdateCheck).isValid()) {
    Settings::setValue(Settings::Gui::AutoUpdateCheck, messages::showUpdateCheckOption(this));
  }

  if (!m_personalUi && Settings::value(Settings::Gui::AutoUpdateCheck).toBool()) {
    m_versionChecker.checkLatest();
  } else {
    qDebug() << "skipping check for new version, disabled";
  }

  if (Settings::value(Settings::Gui::AutoStartCore).toBool()) {
    if (ui->rbModeClient->isChecked() && ui->lineHostname->text().isEmpty())
      return;
    startCore();
  }
}

void MainWindow::createMenuBar()
{
  m_menuFile->addAction(m_actionStartCore);
  m_menuFile->addAction(m_actionRestartCore);
  m_menuFile->addAction(m_actionStopCore);
  m_menuFile->addSeparator();
  m_menuFile->addAction(m_actionQuit);

  m_menuEdit->addAction(m_actionSettings);

  m_menuView->addAction(m_logDock->toggleViewAction());

  m_menuHelp->addAction(m_actionAbout);
  m_menuHelp->addAction(m_actionShowHelp);

  auto menuBar = new QMenuBar(this);
  menuBar->addMenu(m_menuFile);
  menuBar->addMenu(m_menuEdit);
  menuBar->addMenu(m_menuView);
  menuBar->addMenu(m_menuHelp);

  setMenuBar(menuBar);
}

void MainWindow::setupTrayIcon()
{
  auto trayMenu = new QMenu(this);
  if (m_personalUi) {
    m_personalTrayStatus = trayMenu->addAction(tr("桌桥 · 未连接"));
    m_personalTrayStatus->setEnabled(false);
    trayMenu->addSeparator();
    m_personalTrayMapping = trayMenu->addAction(tr("快捷键映射"));
    m_personalTrayMapping->setEnabled(false);
    m_personalTrayToggle = trayMenu->addAction(tr("连接来源电脑"));
    connect(m_personalTrayToggle, &QAction::triggered, this, [this] {
      if (m_coreProcess.processState() == ProcessState::Stopped) startCore(); else stopCore();
    });
    trayMenu->addSeparator();
    auto *open = trayMenu->addAction(tr("打开桌桥…"));
    connect(open, &QAction::triggered, this, &MainWindow::showAndActivate);
    trayMenu->addAction(m_actionTrayQuit);
    connect(trayMenu, &QMenu::aboutToShow, this, &MainWindow::refreshPersonalUi);
    m_trayIcon->setContextMenu(trayMenu);
    setTrayIcon();
    refreshPersonalUi();
    m_trayIcon->show();
    return;
  }
  trayMenu->addActions(
      {m_actionStartCore, m_actionRestartCore, m_actionStopCore, m_actionMinimize, m_actionRestore, m_actionTrayQuit}
  );
  trayMenu->insertSeparator(m_actionMinimize);
  trayMenu->insertSeparator(m_actionTrayQuit);
  m_trayIcon->setContextMenu(trayMenu);

  setTrayIcon();
  m_trayIcon->show();
}

void MainWindow::applyConfig()
{
  if (m_personalUi) {
    const auto mode = Settings::value(Settings::Core::CoreMode).value<CoreMode>();
    const QSignalBlocker clientBlocker(ui->rbModeClient);
    const QSignalBlocker serverBlocker(ui->rbModeServer);
    ui->rbModeClient->setChecked(mode == CoreMode::Client);
    ui->rbModeServer->setChecked(mode == CoreMode::Server);
    m_coreProcess.setMode(mode);
  }
  if (Settings::value(Settings::Gui::ShowVersionInTitle).toBool()) {
    setWindowTitle(QStringLiteral("%1 - %2").arg(kAppName, kDisplayVersion));
  } else {
    setWindowTitle(kAppName);
  }

  const auto host = Settings::value(Settings::Client::RemoteHost).toString();
  if (m_personalUi) {
    const QSignalBlocker blocker(ui->lineHostname);
    ui->lineHostname->setText(host);
    m_coreProcess.setAddress(host);
  } else if (!host.isEmpty()) {
    ui->lineHostname->setText(host);
  }

  updateFingerprintButton();
  setTrayIcon();

  if (const auto ip = Settings::value(Settings::Core::Interface).toString(); !ip.isEmpty()) {
    m_serverStartIPs = {ip};
    m_serverStartSuggestedIP = ip;
  }

  if (m_personalUi) updateModeControls();
  else coreModeToggled(true);
  refreshPersonalUi();
}

void MainWindow::saveSettings() const
{
  if (ui->rbModeClient->isChecked()) {
    Settings::setValue(Settings::Core::CoreMode, Settings::CoreMode::Client);
  } else if (ui->rbModeServer->isChecked()) {
    Settings::setValue(Settings::Core::CoreMode, Settings::CoreMode::Server);
  }
  if (!ui->lineHostname->text().isEmpty())
    Settings::setValue(Settings::Client::RemoteHost, ui->lineHostname->text());
  Settings::save();
}

void MainWindow::setTrayIcon()
{
#if DESKBRIDGE_PERSONAL_BUILD && defined(Q_OS_MACOS)
  auto personalIcon = QIcon(QStringLiteral(":/icons/deskbridge-tray.svg"));
  personalIcon.setIsMask(true);
  m_trayIcon->setIcon(personalIcon);
  return;
#endif
#if DESKBRIDGE_PERSONAL_BUILD && defined(Q_OS_LINUX)
  m_trayIcon->setIcon(deskflow::gui::linuxPersonalTrayIcon(Settings::value(Settings::Gui::SymbolicTrayIcon).toBool()));
  return;
#endif

  static const auto fallbackPath = QStringLiteral(":/icons/%1-%2/apps/64/%3.svg");

  QString themeIcon = kAppIconName;
  if (!Settings::value(Settings::Gui::SymbolicTrayIcon).toBool()) {
    if (deskflow::platform::isMac())
      m_trayIcon->setIcon(
          QIcon::fromTheme(themeIcon, QIcon(fallbackPath.arg(kIconThemeId, iconMode(), themeIcon)))
      );
    else
      m_trayIcon->setIcon(QIcon(fallbackPath.arg(kIconThemeId, QStringLiteral("dark"), themeIcon)));
    return;
  }

  themeIcon.append(QStringLiteral("-symbolic"));

  if (deskflow::platform::isWindows()) {
    QSettings settings(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
        QSettings::NativeFormat
    );
    const QString theme = settings.value(QStringLiteral("SystemUsesLightTheme"), 1).toBool() ? QStringLiteral("light")
                                                                                             : QStringLiteral("dark");
    m_trayIcon->setIcon(QIcon(fallbackPath.arg(kIconThemeId, theme, themeIcon)));
    return;
  }

  auto icon = QIcon::fromTheme(themeIcon, QIcon(fallbackPath.arg(kIconThemeId, iconMode(), themeIcon)));
  icon.setIsMask(true);
  m_trayIcon->setIcon(icon);
}

void MainWindow::handleLogLine(const QString &line)
{
  m_logDock->appendLine(line);
}

void MainWindow::handleUnrecognisedClient(const QString &clientName)
{
  if (m_ignoredClients.contains(clientName)) {
    qDebug("ignoring %s:", qPrintable(clientName));
    return;
  }

  if (m_newClientPromptShowing || m_serverConfigDialogVisible)
    return;

  if (Settings::value(Settings::Server::ExternalConfig).toBool())
    return;

  if (m_serverConfig.isFull() || m_serverConfig.computerExists(clientName))
    return;

  m_newClientPromptShowing = true;

  showAndActivate();

  if (deskflow::gui::messages::showNewClientPrompt(this, clientName)) {
    serverConnectionConfigureClient(clientName);
  } else {
    m_ignoredClients.insert(clientName);
  }

  m_newClientPromptShowing = false;
}

void MainWindow::handleConnectionRefused(deskflow::core::ConnectionRefusal reason)
{
  if (reason != deskflow::core::ConnectionRefusal::AlreadyConnected)
    return;

  if (!isVisible() || m_clientErrorVisible)
    return;

  m_clientErrorVisible = true;
  showAndActivate();

  const auto address = Settings::value(Settings::Client::RemoteHost).toString();
  QMessageBox::warning(
      this, tr("%1 Connection Error").arg(kAppName),
      tr("<p>Failed to connect to the server '%1'.</p>"
         "<p>A Client with your name is already connected to the server.</p>"
         "Please ensure that you're using a unique name and that only a "
         "single instance of the client process is running.</p>")
          .arg(address)
  );

  m_clientErrorVisible = false;
}

void MainWindow::handleMissingKeyboardLayouts(const QString &layouts)
{
  if (Settings::value(Settings::Gui::IgnoreMissingKeyboardLayouts).toBool())
    return;

  QMessageBox msgBox(this);
  msgBox.setIcon(QMessageBox::Warning);
  msgBox.setWindowTitle(tr("Missing Keyboard Layouts"));
  msgBox.setText(tr("<p>Keyboard layout support requires matching layouts on all computers. "
                    "The following layouts from the other computer are not installed on this computer:</p>"
                    "<p><b>%1</b></p>"
                    "<p>Please install them to enable support for these layouts.</p>")
                     .arg(layouts));

  auto *checkBox = new QCheckBox(tr("Don't show this again"), &msgBox);
  msgBox.setCheckBox(checkBox);
  msgBox.exec();

  if (checkBox->isChecked()) {
    Settings::setValue(Settings::Gui::IgnoreMissingKeyboardLayouts, true);
  }
}

void MainWindow::handlePeerFingerprint(const QString &fingerprint)
{
  const auto sha256Text = QString(fingerprint).remove(':');
  const Fingerprint sha256 = {QCryptographicHash::Sha256, QByteArray::fromHex(sha256Text.toLatin1())};

  const bool isClient = m_coreProcess.mode() == CoreMode::Client;
  if ((isClient && m_checkedServers.contains(sha256Text)) || (!isClient && m_checkedClients.contains(sha256Text))) {
    qDebug("ignoring fingerprint, already handled");
    return;
  }

  FingerprintDatabase db;
  db.read(trustedFingerprintDatabase());

  if (db.isTrusted(sha256)) {
    qDebug("fingerprint is trusted");
    return;
  }

  if (isClient) {
    m_checkedServers.append(sha256Text);
    m_coreProcess.stop();
  } else {
    m_checkedClients.append(sha256Text);
  }

  auto mode = isClient ? FingerprintDialogMode::Client : FingerprintDialogMode::Server;
  FingerprintDialog fingerprintDialog(this, m_fingerprint, mode, sha256);

  if (fingerprintDialog.exec() == QDialog::Accepted) {
    db.addTrusted(sha256);
    if (!db.write(trustedFingerprintDatabase())) {
      qCritical().noquote() << "unable to write fingerprint to db:" << trustedFingerprintDatabase();
      m_coreProcess.stop();
      return;
    }
    if (isClient) {
      m_checkedServers.removeAll(sha256Text);
      m_coreProcess.start();
    } else {
      m_checkedClients.removeAll(sha256Text);
    }
  }
}

bool MainWindow::maybeHideToTray()
{
  if (!Settings::value(Settings::Gui::CloseToTray).toBool()) {
    return false;
  }

  if (Settings::value(Settings::Gui::CloseReminder).toBool()) {
    messages::showCloseReminder(this);
    Settings::setValue(Settings::Gui::CloseReminder, false);
  }
  Settings::setValue(Settings::Gui::WindowGeometry, geometry());
  qDebug() << "hiding to tray";
  hide();
  return true;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
  if (event->spontaneous() && maybeHideToTray()) {
    event->ignore();
    return;
  }

  if (m_personalUi) {
    if (m_layoutPending || (m_layoutPersistence && m_layoutPersistence->isPending())) {
      event->ignore();
      showAndActivate();
      m_personalUi->showPage(personal::PersonalWindow::Connection);
      return;
    }
    if (auto *preferences = findChild<personal::PreferencesApplyController *>(); preferences && preferences->isApplying()) {
      event->ignore();
      guardPersonalConfigurationChange();
      return;
    }
    auto *page = personalShortcutPage();
    if (auto *transaction = findChild<personal::ShortcutApplyController *>(); transaction && transaction->isApplying()) {
      event->ignore();
      showAndActivate();
      m_personalUi->showPage(personal::PersonalWindow::Shortcuts);
      page->setStatusMessage(tr("正在应用更改，请完成后再退出"));
      return;
    }
    const bool shortcutsDirty = page && page->isDirty();
    const bool preferencesDirty = m_personalUi->hasPendingPreferences();
    const bool advancedDirty = m_personalUi->advancedPage()->hasPendingEdits();
    if (shortcutsDirty || preferencesDirty || advancedDirty) {
      const QString message = tr("退出会丢失未应用的设置或快捷键草稿。已应用的配置将保留。");
      QMessageBox confirmation(QMessageBox::Warning, tr("未应用的更改"),
                               message, QMessageBox::NoButton, this);
      auto *keepEditing = confirmation.addButton(tr("继续编辑"), QMessageBox::RejectRole);
      auto *discard = confirmation.addButton(tr("放弃更改并退出"), QMessageBox::DestructiveRole);
      confirmation.setDefaultButton(keepEditing);
      confirmation.setEscapeButton(keepEditing);
      confirmation.exec();
      if (confirmation.clickedButton() != discard) {
        event->ignore();
        showAndActivate();
        m_personalUi->showPage(shortcutsDirty ? personal::PersonalWindow::Shortcuts
                              : advancedDirty ? personal::PersonalWindow::Advanced : personal::PersonalWindow::General);
        return;
      }
    }
  }

  if (m_saveOnExit) {
    Settings::setValue(Settings::Gui::WindowGeometry, geometry());
    if (!m_personalUi) Settings::setValue(Settings::Gui::AutoStartCore, m_coreProcess.isStarted());
  }
  qDebug() << "quitting application";

  // any connected dock view acitons will be triggered
  // disconnect them before accepting the event
  disconnect(m_logDock->toggleViewAction(), &QAction::toggled, nullptr, nullptr);

  event->accept();
  QApplication::quit();
}

void MainWindow::showFirstConnectedMessage()
{
  if (Settings::value(Settings::Gui::ShownFirstConnectedMessage).toBool())
    return;
  Settings::setValue(Settings::Gui::ShownFirstConnectedMessage, true);
  messages::showFirstConnectedMessage(this);
}

void MainWindow::updateStatus()
{
  using enum ProcessState;
  const auto connection = m_coreProcess.connectionState();
  const auto process = m_coreProcess.processState();
  const bool isServer = (m_coreProcess.mode() == CoreMode::Server);
  if (process == Stopped || process == Started) {
    updateNetworkInfo();
    ui->btnEditName->setVisible(process == Stopped);
  }
  m_statusBar->setStatus(connection, process, isServer);
  refreshPersonalUi();
}

void MainWindow::coreProcessStateChanged(ProcessState state)
{
  using enum ProcessState;
  updateStatus();
  if (state == Started) {
    qDebug() << "recording that core has started";
    if (!m_personalUi) Settings::setValue(Settings::Gui::AutoStartCore, true);
    if (m_coreProcess.mode() == CoreMode::Server &&
        !Settings::value(Settings::Gui::ShownServerFirstStartMessage).toBool()) {
      qDebug() << "starting core server for first time";
      messages::showFirstServerStartMessage(this);
      Settings::setValue(Settings::Gui::ShownServerFirstStartMessage, true);
    }
  }

  if (state == Started || state == Starting || state == RetryPending) {
    disconnect(ui->btnToggleCore, &QPushButton::clicked, m_actionStartCore, &QAction::trigger);
    connect(ui->btnToggleCore, &QPushButton::clicked, m_actionStopCore, &QAction::trigger, Qt::UniqueConnection);

    ui->btnRestartCore->setEnabled(true);
    m_actionStartCore->setVisible(false);
    m_actionRestartCore->setVisible(true);
    m_actionStopCore->setEnabled(true);

    if (state == Starting) {
      saveSettings();
    }

  } else {
    disconnect(ui->btnToggleCore, &QPushButton::clicked, m_actionStopCore, &QAction::trigger);
    connect(ui->btnToggleCore, &QPushButton::clicked, m_actionStartCore, &QAction::trigger, Qt::UniqueConnection);

    ui->btnRestartCore->setEnabled(false);
    m_actionStartCore->setVisible(true);
    m_actionRestartCore->setVisible(false);
    m_actionStopCore->setEnabled(false);
  }
  updateModeControlLabels();
}

void MainWindow::coreConnectionStateChanged(ConnectionState state)
{
  qDebug() << "core connection state changed:" << static_cast<int>(state);

  updateStatus();

  // always assume connection is not secure when connection changes
  // to anything except connected. the only way the padlock shows is
  // when the correct TLS version string is detected.
  if (state != ConnectionState::Connected) {
    secureSocket(false);
  } else if (isVisible()) {
    showFirstConnectedMessage();
  }
}

void MainWindow::updateFingerprintButton()
{
  m_statusBar->setBtnFingerprintVisible(TlsUtility::isEnabled() && !m_fingerprint.data.isEmpty());
}

void MainWindow::hide()
{
#ifdef Q_OS_MACOS
  macOSNativeHide();
#else
  QMainWindow::hide();
#endif
  m_actionRestore->setVisible(true);
  m_actionMinimize->setVisible(false);
}

void MainWindow::changeEvent(QEvent *e)
{
  QMainWindow::changeEvent(e);
  if (e->type() == QEvent::PaletteChange) {
    updateIconTheme();
    setWindowIcon(appIcon());
    setTrayIcon();
  } else if (e->type() == QEvent::LanguageChange) {
    ui->retranslateUi(this);
    updateModeControlLabels();
    updateNetworkInfo();
    updateStatus();
    serverClientsChanged({});
    updateText();
  }
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
  if (obj != ui->lineEditName || event->type() != QEvent::KeyPress)
    return false;
  if (const auto keyEvent = static_cast<QKeyEvent *>(event); keyEvent->key() != Qt::Key_Escape)
    return false;
  ui->lineEditName->hide();
  ui->lblComputerName->show();
  ui->btnEditName->show();
  ui->lineEditName->setText(Settings::value(Settings::Core::ComputerName).toString());
  toggleCanRunCore(canRunCore());
  return true;
}

void MainWindow::updateText()
{
  m_menuFile->setTitle(tr("&File"));
  m_menuEdit->setTitle(tr("&Edit"));
  m_menuView->setTitle(tr("&View"));
  m_menuHelp->setTitle(tr("&Help"));

  m_actionMinimize->setText(tr("&Minimize to tray"));
  m_actionQuit->setText(tr("&Quit"));
  m_actionTrayQuit->setText(tr("&Quit"));
  //: %1 will be the replaced with the appname
  m_actionRestore->setText(tr("&Open %1").arg(kAppName));
  m_actionSettings->setText(tr("&Preferences"));
  m_actionStartCore->setText(tr("&Start"));
  m_actionRestartCore->setText(tr("Rest&art"));
  m_actionStopCore->setText(tr("S&top"));
  //: %1 will be the replaced with the appname
  m_actionAbout->setText(tr("About %1...").arg(kAppName));

  m_actionShowHelp->setText(tr("View &Help"));

  //: start / restart core shortcut
  m_actionStartCore->setShortcut(QKeySequence(tr("Ctrl+S")));
  m_actionRestartCore->setShortcut(QKeySequence(tr("Ctrl+S")));

  //: stop core shortcut
  m_actionStopCore->setShortcut(QKeySequence(tr("Ctrl+T")));

  if (deskflow::platform::isWindows()) {
    //: Quit shortcut
    m_actionQuit->setShortcut(QKeySequence(tr("Ctrl+Q")));
    m_actionTrayQuit->setShortcut(QKeySequence(tr("Ctrl+Q")));
  }
  if (m_personalUi) {
    m_menuFile->setTitle(tr("连接"));
    m_menuEdit->setTitle(tr("编辑"));
    m_menuView->setTitle(tr("窗口"));
    m_menuHelp->setTitle(tr("帮助"));
    m_actionSettings->setText(tr("设置…"));
    m_actionTrayQuit->setText(tr("退出桌桥"));
    m_actionQuit->setText(tr("退出桌桥"));
  }
}

void MainWindow::showConfigureServer(const QString &message)
{
  if (guardPersonalConfigurationChange()) return;
  m_serverConfigDialogVisible = true;
  ServerConfigDialog dialog(this, serverConfig());
  dialog.message(message);
  if ((dialog.exec() == QDialog::Accepted) && m_coreProcess.isStarted()) {
    m_coreProcess.restart();
  }
  m_serverConfigDialogVisible = false;
  refreshPersonalUi();
}

void MainWindow::showConfigureClient()
{
  if (m_personalUi) {
    showAndActivate();
    m_personalUi->showPage(personal::PersonalWindow::General);
    return;
  }
  ClientConfigDialog dialog(this);
  if ((dialog.exec() == QDialog::Accepted) && m_coreProcess.isStarted()) {
    m_coreProcess.restart();
  }
}

void MainWindow::secureSocket(bool secureSocket)
{
  m_secureSocket = secureSocket;
  updateSecurityIcon(m_statusBar->securityIconVisible());
  refreshPersonalUi();
}

void MainWindow::updateComputerName()
{
  const auto computerName = Settings::value(Settings::Core::ComputerName).toString();
  ui->lblComputerName->setText(computerName);
  ui->lineEditName->setText(computerName);
  m_serverConfig.updateServerName();
}

void MainWindow::showAndActivate()
{
  const auto wasVisible = isVisible();
#ifdef Q_OS_MACOS
  forceAppActive();
#endif
  showNormal();
  raise();
  activateWindow();
  m_actionRestore->setVisible(false);
  m_actionMinimize->setVisible(true);
  if (!wasVisible)
    restoreWindow();
}

void MainWindow::showHostNameEditor()
{
  ui->lineEditName->show();
  ui->lblComputerName->hide();
  ui->btnEditName->hide();
  toggleCanRunCore(false);
  ui->lineEditName->setFocus();
}

void MainWindow::setHostName()
{
  ui->lineEditName->hide();
  ui->lblComputerName->show();
  ui->btnEditName->show();
  toggleCanRunCore(canRunCore());

  QString text = ui->lineEditName->text();
  const auto computerName = Settings::value(Settings::Core::ComputerName).toString();

  if (text == computerName)
    return;

  const bool isServer = ui->rbModeServer->isChecked();
  bool existingComputer = false;
  if (isServer)
    existingComputer = serverConfig().computerExists(text);

  if (!ui->lineEditName->hasAcceptableInput() || text.isEmpty() || existingComputer) {
    blockSignals(true);
    ui->lineEditName->setText(computerName);
    blockSignals(false);

    const auto title = tr("Invalid Computer Name");
    QString body;
    if (existingComputer) {
      body = tr("Computer name already exists");
    } else {
      body =
          tr("The name you have chosen is invalid.\n\n"
             "Valid names:\n"
             "• Use letters and numbers\n"
             "• May also use _ or -\n"
             "• Are between 1 and 255 characters");
    }
    QMessageBox::information(this, title, body);
    return;
  }

  ui->lblComputerName->setText(ui->lineEditName->text());
  Settings::setValue(Settings::Core::ComputerName, ui->lineEditName->text());
  if (isServer)
    serverConfig().updateServerName();
  applyConfig();
}

QString MainWindow::trustedFingerprintDatabase() const
{
  const bool isClient = m_coreProcess.mode() == CoreMode::Client;
  return isClient ? Settings::tlsTrustedServersDb() : Settings::tlsTrustedClientsDb();
}

bool MainWindow::generateCertificate()
{
  const auto certificate = Settings::value(Settings::Security::Certificate).toString();
  if (!QFile::exists(certificate) && !TlsUtility::generateCertificate()) {
    return false;
  }

  m_fingerprint = {QCryptographicHash::Sha256, TlsUtility::certFingerprint()};
  updateFingerprintButton();
  return true;
}

void MainWindow::serverClientsChanged(const QStringList &clients)
{
  if (m_coreProcess.mode() != CoreMode::Server || !m_coreProcess.isStarted())
    return;
  m_statusBar->setServerClients(clients);
}

void MainWindow::daemonIpcClientConnectionFailed()
{
  if (deskflow::gui::messages::showDaemonOffline(this)) {
    m_coreProcess.retryDaemon();
  }
}

void MainWindow::toggleCanRunCore(bool enableButtons)
{
  const bool isStarted = m_coreProcess.isStarted();
  ui->btnToggleCore->setEnabled(enableButtons);
  ui->btnRestartCore->setEnabled(enableButtons && isStarted);
  m_actionStartCore->setEnabled(enableButtons);
  m_actionStopCore->setEnabled(enableButtons && isStarted);
}

void MainWindow::remoteHostChanged(const QString &newRemoteHost)
{
  m_coreProcess.setAddress(newRemoteHost);
  toggleCanRunCore(!newRemoteHost.isEmpty() && ui->rbModeClient->isChecked());
  if (newRemoteHost.isEmpty()) {
    Settings::setValue(Settings::Client::RemoteHost);
  } else {
    Settings::setValue(Settings::Client::RemoteHost, newRemoteHost);
  }
}

void MainWindow::updateIpLabel(const QStringList &addresses)
{
  const auto mode = m_coreProcess.mode();
  if (mode == CoreMode::None) {
    return;
  }

  static const auto colorText = QStringLiteral(R"(<span style="color:%1;">%2</span>)");
  const bool serverStarted = m_coreProcess.isStarted();
  const bool fixedIP = !Settings::value(Settings::Core::Interface).toString().isEmpty();

  if (!fixedIP && addresses.isEmpty() && !serverStarted || (serverStarted && m_serverStartSuggestedIP.isEmpty())) {
    ui->lblIpAddresses->setText(colorText.arg(palette().linkVisited().color().name(), tr("No IP Detected")));
    ui->lblIpAddresses->setToolTip(tr("Unable to detect an IP address. Check your network connection is active."));
    return;
  }

  QString labelText = fixedIP ? tr("Using IP: ") : tr("Suggested IP: ");
  QString toolTipText = tr("<p>If connecting via the hostname fails, try %1</p>");

  const bool filterIpList = (serverStarted || fixedIP);
  const QRegularExpression ipListFilter(filterIpList ? QStringLiteral("(%1)").arg(m_serverStartIPs.join("|")) : "");
  const QStringList ipList = addresses.filter(ipListFilter);

  bool IPValid = true;
  if (filterIpList && (m_serverStartSuggestedIP != m_currentIpAddress) || !ipList.contains(m_serverStartSuggestedIP)) {
    IPValid = !ipList.isEmpty();
  }

  if (IPValid) {
    m_currentIpAddress = ipList.first();
    labelText.append(m_currentIpAddress);
  } else {
    labelText.append(colorText.arg(palette().linkVisited().color().name(), m_serverStartSuggestedIP));
    toolTipText.append(tr("\nA bound IP is now invalid, you may need to restart the server."));
  }

  if (ipList.count() < 2 || fixedIP) {
    toolTipText = toolTipText.arg(tr("the suggested IP."));
  } else {
    toolTipText = toolTipText.arg(tr("one of the following IPs:<br/>%1").arg(ipList.join("<br/>")));
  }

  ui->lblIpAddresses->setText(labelText);
  ui->lblIpAddresses->setToolTip(mode == CoreMode::Server ? toolTipText : QString());
}

void MainWindow::updateTimeoutDelay(int newDelay)
{
  m_statusBar->setConnectionInterval(newDelay);
}

void MainWindow::setHelpFilePath()
{
  const QString appPath = QCoreApplication::applicationDirPath();
  auto buildPath = QString("%1/../docs/HelpMain.md").arg(appPath);
  auto installPath = QString("%1/../share/doc/%2/HelpMain.md").arg(appPath, kAppId);
  if (deskflow::platform::isMac()) {
    installPath = QString("%1/../Resources/docs/HelpMain.md").arg(appPath);
    buildPath = QString("%1/../../../../docs/HelpMain.md").arg(appPath);
  } else if (deskflow::platform::isWindows()) {
    installPath = QString("%1/docs/HelpMain.md").arg(appPath);
  }

  buildPath = QDir::cleanPath(buildPath);
  installPath = QDir::cleanPath(installPath);

  if (QFile::exists(installPath))
    m_helpPath = QUrl::fromLocalFile(installPath);
  else if (QFile::exists(buildPath))
    m_helpPath = QUrl::fromLocalFile(buildPath);
  else
    m_helpPath = kUrlWiki;
}

void MainWindow::showHelpViewer() const
{
  if (m_helpPath.isLocalFile()) {
    HelpDialog dialogHelp(this->centralWidget(), m_helpPath);
    dialogHelp.exec();
  } else {
    QDesktopServices::openUrl(m_helpPath);
  }
}

bool MainWindow::canRunCore() const
{
  const auto mode = m_coreProcess.mode();
  const bool isServer = mode == Settings::CoreMode::Server;
  const bool isClient = mode == Settings::CoreMode::Client;
  return ((isServer || isClient) && (isClient && !ui->lineHostname->text().isEmpty()) || isServer);
}
