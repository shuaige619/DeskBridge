/*
 * SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "AdvancedPage.h"

#include "PersonalStyle.h"
#include "ToggleSwitch.h"
#include "common/LogLevel.h"
#include "common/Settings.h"
#include "gui/StyleUtils.h"
#include "gui/core/NetworkMonitor.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSslCertificate>
#include <QTextDocument>
#include <QToolButton>
#include <QVBoxLayout>

namespace deskflow::gui::personal {
namespace {
QLabel *label(const QString &text, QWidget *parent, const char *property = nullptr)
{
  auto *result = new QLabel(text, parent);
  result->setTextFormat(Qt::PlainText);
  result->setWordWrap(true);
  if (property)
    result->setProperty(property, true);
  return result;
}

QVBoxLayout *card(QVBoxLayout *body, QWidget *parent)
{
  auto *frame = new QFrame(parent);
  frame->setProperty("personalCard", true);
  auto *layout = new QVBoxLayout(frame);
  layout->setContentsMargins(18, 16, 18, 16);
  layout->setSpacing(10);
  body->addWidget(frame);
  return layout;
}

QWidget *fold(QVBoxLayout *layout, const QString &title, const QString &name, QToolButton **button)
{
  auto *parent = layout->parentWidget();
  *button = new QToolButton(parent);
  (*button)->setObjectName(name);
  (*button)->setText(title);
  (*button)->setProperty("personalDisclosure", true);
  (*button)->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  (*button)->setIcon(personalDisclosureIcon(false));
  (*button)->setIconSize(QSize(16, 16));
  (*button)->setCheckable(true);
  (*button)->setAutoRaise(true);
  (*button)->setFocusPolicy(Qt::StrongFocus);
  (*button)->setCursor(Qt::PointingHandCursor);
  (*button)->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
  (*button)->setAccessibleName(title);
  layout->addWidget(*button);
  auto *content = new QWidget(parent);
  content->setVisible(false);
  layout->addWidget(content);
  QObject::connect(*button, &QToolButton::toggled, content, [content, button = *button](bool expanded) {
    content->setVisible(expanded);
    button->setIcon(personalDisclosureIcon(expanded));
  });
  return content;
}

QVBoxLayout *foldLayout(QWidget *content)
{
  auto *layout = new QVBoxLayout(content);
  layout->setContentsMargins(4, 2, 4, 4);
  layout->setSpacing(10);
  return layout;
}

QFormLayout *form(QWidget *parent)
{
  auto *layout = new QFormLayout(parent);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setHorizontalSpacing(16);
  layout->setVerticalSpacing(10);
  layout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
  layout->setRowWrapPolicy(QFormLayout::WrapLongRows);
  return layout;
}

QStringList ordinaryKeys()
{
  QStringList keys{
      Settings::Security::TlsEnabled, Settings::Security::CheckPeers,
      Settings::Core::Port, Settings::Core::Interface,
      Settings::Log::Level, Settings::Log::GuiDebug, Settings::Log::ToFile, Settings::Log::File,
      Settings::Core::EnableEnterCommand, Settings::Core::ComputerEnterCommand,
      Settings::Core::EnableExitCommand, Settings::Core::ComputerExitCommand};
#ifdef Q_OS_WIN
  // A portable installation cannot install or configure the system service.
  if (!Settings::isPortableMode())
    keys.append({Settings::Core::ProcessMode, Settings::Daemon::Elevate});
#endif
  return keys;
}

QVariant ordinaryValue(const QString &key, bool defaultValue = false)
{
  const auto value = defaultValue ? Settings::defaultValue(key) : Settings::value(key);
  if (key == Settings::Core::Port || key == Settings::Core::ProcessMode)
    return value.toInt();
  if (key == Settings::Security::TlsEnabled || key == Settings::Security::CheckPeers ||
      key == Settings::Log::GuiDebug || key == Settings::Log::ToFile ||
      key == Settings::Core::EnableEnterCommand || key == Settings::Core::EnableExitCommand ||
      key == Settings::Daemon::Elevate)
    return value.toBool();
  return value.toString();
}

void setChecked(QCheckBox *widget, bool checked)
{
  const QSignalBlocker blocker(widget);
  widget->setChecked(checked);
}

void setText(QLineEdit *widget, const QString &text)
{
  const QSignalBlocker blocker(widget);
  if (widget->text() != text)
    widget->setText(text);
}

void selectData(QComboBox *widget, const QVariant &value)
{
  const QSignalBlocker blocker(widget);
  auto index = widget->findData(value);
  if (index < 0) {
    // Preserve an existing custom value even when it is no longer a preset.
    widget->addItem(value.toString(), value);
    index = widget->count() - 1;
  }
  widget->setCurrentIndex(index);
}
} // namespace

AdvancedPage::AdvancedPage(QWidget *parent) : QWidget(parent)
{
  setObjectName(QStringLiteral("advancedPage"));
  setAccessibleName(tr("高级"));
  auto *outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(0);
  auto *heading = new QWidget(this);
  heading->setObjectName(QStringLiteral("advancedPageHeading"));
  auto *headingLayout = new QVBoxLayout(heading);
  headingLayout->setContentsMargins(30, 24, 30, 18);
  auto *title = label(tr("高级"), heading, "pageTitle");
  title->setObjectName(QStringLiteral("advancedPageInternalTitle"));
  headingLayout->addWidget(title);
  headingLayout->addWidget(label(tr("连接安全、运行日志与低频维护选项。"), heading, "pageSubtitle"));
  outer->addWidget(heading);

  auto *scroll = new QScrollArea(this);
  scroll->setObjectName(QStringLiteral("advancedScrollArea"));
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  auto *content = new QWidget(scroll);
  content->setProperty("personalPageBody", true);
  auto *body = new QVBoxLayout(content);
  body->setObjectName(QStringLiteral("advancedBodyLayout"));
  body->setContentsMargins(30, 6, 30, 24);
  body->setSpacing(14);
  m_readOnly = label(tr("设置文件为只读，可以查看状态与日志，无法应用更改或执行维护。"), content, "secondaryText");
  m_readOnly->setObjectName(QStringLiteral("advancedReadOnlyNotice"));
  body->addWidget(m_readOnly);

  auto *security = card(body, content);
  auto *securityParent = security->parentWidget();
  security->addWidget(label(tr("连接安全"), securityParent, "sectionTitle"));
  m_securityState = label({}, securityParent, "secondaryText");
  m_securityState->setObjectName(QStringLiteral("advancedConnectionSecurity"));
  security->addWidget(m_securityState);
  m_tlsEnabled = new ToggleSwitch(tr("使用 TLS 加密连接"), securityParent);
  m_tlsEnabled->setObjectName(QStringLiteral("advancedTlsEnabled"));
  security->addWidget(m_tlsEnabled);
  m_checkPeers = new ToggleSwitch(tr("校验接收端证书（本机作为来源时）"), securityParent);
  m_checkPeers->setObjectName(QStringLiteral("advancedCheckPeers"));
  security->addWidget(m_checkPeers);
  security->addWidget(label(tr("安全选项应用后生效。接收端会核对来源电脑证书；来源端可要求接收端通过证书校验。"), securityParent, "secondaryText"));
  m_fingerprint = new QPushButton(tr("查看本机证书指纹…"), securityParent);
  m_fingerprint->setObjectName(QStringLiteral("advancedFingerprint"));
  security->addWidget(m_fingerprint, 0, Qt::AlignLeft);

  auto *network = fold(security, tr("网络选项"), QStringLiteral("advancedNetworkFold"), &m_networkFold);
  auto *networkLayout = foldLayout(network);
  networkLayout->addWidget(label(tr("端口需与另一台电脑一致。本机绑定地址用于选择监听网卡，不是另一台电脑的地址。更改后共享会重新连接。"), network, "secondaryText"));
  auto *networkFields = new QWidget(network);
  auto *networkForm = form(networkFields);
  m_port = new QSpinBox(networkFields);
  m_port->setObjectName(QStringLiteral("advancedPort"));
  m_port->setAccessibleName(tr("共享连接端口"));
  m_port->setRange(1, 65535);
  m_port->setMaximumWidth(160);
  networkForm->addRow(tr("端口"), m_port);
  m_interface = new QComboBox(networkFields);
  m_interface->setObjectName(QStringLiteral("advancedInterface"));
  m_interface->setAccessibleName(tr("本机绑定地址"));
  m_interface->setEditable(true);
  m_interface->setInsertPolicy(QComboBox::NoInsert);
  m_interface->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  m_interface->setMinimumContentsLength(12);
  networkForm->addRow(tr("本机绑定地址"), m_interface);
  networkLayout->addWidget(networkFields);

  auto *certificate = fold(security, tr("证书维护"), QStringLiteral("advancedCertificateFold"), &m_certificateFold);
  auto *certificateLayout = foldLayout(certificate);
  m_certificateName = label({}, certificate, "secondaryText");
  m_certificateName->setObjectName(QStringLiteral("advancedCertificateName"));
  m_certificateName->setTextInteractionFlags(Qt::TextSelectableByMouse);
  certificateLayout->addWidget(m_certificateName);
  auto *certificateNaming = label(tr("新建或重建的证书名称为 %1。已有 Deskflow 证书可继续使用，无需仅为名称重建。")
      .arg(QString::fromUtf8(kAppName)), certificate, "secondaryText");
  certificateNaming->setObjectName(QStringLiteral("advancedCertificateNamingNotice"));
  certificateLayout->addWidget(certificateNaming);
  certificateLayout->addWidget(label(tr("路径和密钥长度仅用于独立重建，不随“应用更改”保存。"), certificate, "secondaryText"));
  auto *certificateFields = new QWidget(certificate);
  auto *certificateForm = form(certificateFields);
  auto *certificatePathRow = new QWidget(certificateFields);
  auto *certificatePathLayout = new QHBoxLayout(certificatePathRow);
  certificatePathLayout->setContentsMargins(0, 0, 0, 0);
  m_certificatePath = new QLineEdit(certificatePathRow);
  m_certificatePath->setObjectName(QStringLiteral("advancedCertificatePath"));
  m_certificatePath->setAccessibleName(tr("重建证书的保存路径"));
  m_certificatePath->setMinimumWidth(0);
  certificatePathLayout->addWidget(m_certificatePath, 1);
  m_browseCertificate = new QPushButton(tr("选择…"), certificatePathRow);
  m_browseCertificate->setObjectName(QStringLiteral("advancedBrowseCertificate"));
  certificatePathLayout->addWidget(m_browseCertificate);
  certificateForm->addRow(tr("证书路径"), certificatePathRow);
  m_keySize = new QComboBox(certificateFields);
  m_keySize->setObjectName(QStringLiteral("advancedKeySize"));
  m_keySize->setAccessibleName(tr("重建证书的密钥长度"));
  for (const auto bits : {2048, 4096})
    m_keySize->addItem(tr("%1 位").arg(bits), bits);
  certificateForm->addRow(tr("密钥长度"), m_keySize);
  certificateLayout->addWidget(certificateFields);
  m_certificateHint = label({}, certificate, "secondaryText");
  m_certificateHint->setObjectName(QStringLiteral("advancedCertificateHint"));
  certificateLayout->addWidget(m_certificateHint);
  m_regenerate = new QPushButton(tr("重建本机证书…"), certificate);
  m_regenerate->setObjectName(QStringLiteral("advancedRegenerateCertificate"));
  certificateLayout->addWidget(m_regenerate, 0, Qt::AlignLeft);

  auto *logs = card(body, content);
  auto *logsParent = logs->parentWidget();
  logs->addWidget(label(tr("日志"), logsParent, "sectionTitle"));
  logs->addWidget(label(tr("默认保留基本连接事件、警告和错误。详细调试默认关闭，排查问题时可临时开启。"), logsParent, "secondaryText"));
  auto *logActions = new QHBoxLayout;
  m_viewLog = new QPushButton(tr("查看日志…"), logsParent);
  m_viewLog->setObjectName(QStringLiteral("advancedViewLog"));
  logActions->addWidget(m_viewLog);
  m_copyLog = new QPushButton(tr("复制日志"), logsParent);
  m_copyLog->setObjectName(QStringLiteral("advancedCopyLog"));
  logActions->addWidget(m_copyLog);
  logActions->addStretch();
  logs->addLayout(logActions);
  auto *diagnostics = fold(logs, tr("诊断选项"), QStringLiteral("advancedDiagnosticFold"), &m_diagnosticFold);
  auto *diagnosticLayout = foldLayout(diagnostics);
  auto *levelFields = new QWidget(diagnostics);
  auto *levelForm = form(levelFields);
  m_logLevel = new QComboBox(levelFields);
  m_logLevel->setObjectName(QStringLiteral("advancedLogLevel"));
  m_logLevel->setAccessibleName(tr("核心日志级别"));
  const QStringList levelNames{tr("严重错误 · FATAL"), tr("错误 · ERROR"), tr("警告 · WARNING"),
                               tr("基本信息（默认）· INFO"), tr("调试 · DEBUG"), tr("详细调试 · VERBOSE")};
  const auto levelOptions = LogLevel::logLevelOptions();
  for (int index = 0; index < levelOptions.size(); ++index)
    m_logLevel->addItem(levelNames.at(index), levelOptions.at(index));
  levelForm->addRow(tr("核心日志级别"), m_logLevel);
  diagnosticLayout->addWidget(levelFields);
  m_guiDebug = new ToggleSwitch(tr("记录界面调试信息"), diagnostics);
  m_guiDebug->setObjectName(QStringLiteral("advancedGuiDebug"));
  diagnosticLayout->addWidget(m_guiDebug);
  m_logToFile = new ToggleSwitch(tr("将日志写入文件"), diagnostics);
  m_logToFile->setObjectName(QStringLiteral("advancedLogToFile"));
  diagnosticLayout->addWidget(m_logToFile);
  auto *logFileRow = new QWidget(diagnostics);
  auto *logFileLayout = new QHBoxLayout(logFileRow);
  logFileLayout->setContentsMargins(0, 0, 0, 0);
  m_logFile = new QLineEdit(logFileRow);
  m_logFile->setObjectName(QStringLiteral("advancedLogFile"));
  m_logFile->setAccessibleName(tr("日志文件路径"));
  m_logFile->setMinimumWidth(0);
  logFileLayout->addWidget(m_logFile, 1);
  m_browseLog = new QPushButton(tr("选择…"), logFileRow);
  m_browseLog->setObjectName(QStringLiteral("advancedBrowseLog"));
  logFileLayout->addWidget(m_browseLog);
  diagnosticLayout->addWidget(logFileRow);
  diagnosticLayout->addWidget(label(tr("调试信息可能包含设备名称、地址与命令内容。排查完成后，可将日志级别恢复为 INFO，并关闭界面调试与文件日志。"), diagnostics, "secondaryText"));

  auto *automation = card(body, content);
  auto *automationParent = automation->parentWidget();
  auto *automationContent = fold(automation, tr("自动化 · 未启用"), QStringLiteral("advancedAutomationFold"), &m_automationFold);
  auto *automationLayout = foldLayout(automationContent);
  automationLayout->addWidget(label(tr("当共享鼠标进入或离开本机屏幕时，以本机当前运行权限执行命令。仅填写你了解用途的本机命令。"), automationParent, "secondaryText"));
  m_enableEnter = new ToggleSwitch(tr("鼠标进入本机时运行命令"), automationContent);
  m_enableEnter->setObjectName(QStringLiteral("advancedEnableEnterCommand"));
  automationLayout->addWidget(m_enableEnter);
  m_enterCommand = new QLineEdit(automationContent);
  m_enterCommand->setObjectName(QStringLiteral("advancedEnterCommand"));
  m_enterCommand->setAccessibleName(tr("鼠标进入本机时运行的命令"));
  m_enterCommand->setPlaceholderText(tr("输入本机命令"));
  automationLayout->addWidget(m_enterCommand);
  m_enableExit = new ToggleSwitch(tr("鼠标离开本机时运行命令"), automationContent);
  m_enableExit->setObjectName(QStringLiteral("advancedEnableExitCommand"));
  automationLayout->addWidget(m_enableExit);
  m_exitCommand = new QLineEdit(automationContent);
  m_exitCommand->setObjectName(QStringLiteral("advancedExitCommand"));
  m_exitCommand->setAccessibleName(tr("鼠标离开本机时运行的命令"));
  m_exitCommand->setPlaceholderText(tr("输入本机命令"));
  automationLayout->addWidget(m_exitCommand);

#ifdef Q_OS_WIN
  auto *process = card(body, content);
  auto *processContent = fold(process, tr("运行方式"), QStringLiteral("advancedProcessFold"), &m_processFold);
  auto *processLayout = foldLayout(processContent);
  processLayout->addWidget(label(Settings::isPortableMode()
      ? tr("便携模式使用桌面进程，不安装后台系统服务。")
      : tr("后台系统服务可在登录界面工作，与“通用”中的登录时启动不同。更改运行方式后共享会重新连接。"), processContent, "secondaryText"));
  m_processMode = new QComboBox(processContent);
  m_processMode->setObjectName(QStringLiteral("advancedProcessMode"));
  m_processMode->setAccessibleName(tr("键鼠共享运行方式"));
  m_processMode->addItem(tr("桌面进程"), static_cast<int>(Settings::Desktop));
  m_processMode->addItem(tr("后台系统服务"), static_cast<int>(Settings::Service));
  processLayout->addWidget(m_processMode);
  m_elevate = new ToggleSwitch(tr("以系统权限运行共享服务"), processContent);
  m_elevate->setObjectName(QStringLiteral("advancedElevate"));
  processLayout->addWidget(m_elevate);
#endif

  auto *recovery = card(body, content);
  QToolButton *recoveryFold = nullptr;
  auto *recoveryContent = fold(recovery, tr("恢复与重置"), QStringLiteral("advancedRecoveryFold"), &recoveryFold);
  auto *recoveryLayout = foldLayout(recoveryContent);
  recoveryLayout->addWidget(label(tr("恢复本页默认值只修改高级页草稿，需应用后生效。证书身份、其他页面设置与设备信任记录会保留。"), recoveryContent, "secondaryText"));
  m_defaults = new QPushButton(tr("恢复本页默认值…"), recoveryContent);
  m_defaults->setObjectName(QStringLiteral("advancedRestoreDefaults"));
  recoveryLayout->addWidget(m_defaults, 0, Qt::AlignLeft);
  auto *separator = new QFrame(recoveryContent);
  separator->setFrameShape(QFrame::HLine);
  separator->setProperty("personalSeparator", true);
  recoveryLayout->addWidget(separator);
  recoveryLayout->addWidget(label(tr("清除全部设置会删除此应用保存的偏好、设备连接与快捷键配置，并重启应用。此操作无法撤销。"), recoveryContent, "secondaryText"));
  m_resetAll = new QPushButton(tr("清除全部设置并重启…"), recoveryContent);
  m_resetAll->setObjectName(QStringLiteral("advancedResetAll"));
  m_resetAll->setProperty("dangerButton", true);
  recoveryLayout->addWidget(m_resetAll, 0, Qt::AlignLeft);
  body->addStretch();
  scroll->setWidget(content);
  outer->addWidget(scroll, 1);

  auto *footer = new QWidget(this);
  footer->setProperty("personalFooter", true);
  auto *footerLayout = new QHBoxLayout(footer);
  footerLayout->setContentsMargins(28, 12, 28, 14);
  footerLayout->setSpacing(10);
  m_status = label({}, footer, "secondaryText");
  m_status->setObjectName(QStringLiteral("advancedStatus"));
  m_status->setMinimumWidth(0);
  footerLayout->addWidget(m_status, 1);
  m_revert = new QPushButton(tr("还原草稿"), footer);
  m_revert->setObjectName(QStringLiteral("advancedRevert"));
  footerLayout->addWidget(m_revert);
  m_apply = new QPushButton(tr("应用更改"), footer);
  m_apply->setObjectName(QStringLiteral("advancedApply"));
  m_apply->setProperty("primaryButton", true);
  footerLayout->addWidget(m_apply);
  outer->addWidget(footer);

  const auto connectCheck = [this](QCheckBox *widget, const QString &key) {
    connect(widget, &QCheckBox::toggled, this, [this, key](bool checked) { draftChanged(key, checked); });
  };
  const auto connectText = [this](QLineEdit *widget, const QString &key) {
    connect(widget, &QLineEdit::textChanged, this, [this, key](const QString &text) { draftChanged(key, text); });
  };
  connectCheck(m_tlsEnabled, Settings::Security::TlsEnabled);
  connectCheck(m_checkPeers, Settings::Security::CheckPeers);
  connectCheck(m_guiDebug, Settings::Log::GuiDebug);
  connectCheck(m_logToFile, Settings::Log::ToFile);
  connectCheck(m_enableEnter, Settings::Core::EnableEnterCommand);
  connectCheck(m_enableExit, Settings::Core::EnableExitCommand);
  connectText(m_logFile, Settings::Log::File);
  connectText(m_enterCommand, Settings::Core::ComputerEnterCommand);
  connectText(m_exitCommand, Settings::Core::ComputerExitCommand);
  connect(m_port, &QSpinBox::valueChanged, this, [this](int port) { draftChanged(Settings::Core::Port, port); });
  connect(m_interface, &QComboBox::currentTextChanged, this, [this](const QString &text) {
    const auto index = m_interface->findText(text);
    draftChanged(Settings::Core::Interface, index >= 0 ? m_interface->itemData(index).toString() : text);
  });
  connect(m_logLevel, &QComboBox::currentIndexChanged, this, [this] {
    draftChanged(Settings::Log::Level, m_logLevel->currentData());
  });
#ifdef Q_OS_WIN
  connect(m_processMode, &QComboBox::currentIndexChanged, this, [this] {
    draftChanged(Settings::Core::ProcessMode, m_processMode->currentData());
  });
  connectCheck(m_elevate, Settings::Daemon::Elevate);
#endif
  connect(m_certificatePath, &QLineEdit::textChanged, this, [this] {
    if (m_syncing) return;
    setStatusMessage(tr("证书参数仅为维护草稿；点按“重建本机证书”并确认后才会执行。"));
    refresh();
  });
  connect(m_keySize, &QComboBox::currentIndexChanged, this, [this] {
    if (m_syncing) return;
    setStatusMessage(tr("密钥长度尚未应用；只有确认重建证书才会改变本机身份。"));
    refresh();
  });
  connect(m_browseCertificate, &QPushButton::clicked, this, [this] {
    if (m_applying || !Settings::isWritable()) return;
    const auto path = QFileDialog::getSaveFileName(this, tr("选择重建证书的保存路径"), m_certificatePath->text(), tr("证书 (*.pem)"), nullptr, QFileDialog::DontConfirmOverwrite);
    if (!path.isEmpty()) m_certificatePath->setText(path);
  });
  connect(m_browseLog, &QPushButton::clicked, this, [this] {
    if (m_applying || !Settings::isWritable()) return;
    const auto path = QFileDialog::getSaveFileName(this, tr("选择日志文件"), m_logFile->text(), tr("日志 (*.log *.txt)"));
    if (!path.isEmpty()) m_logFile->setText(path);
  });
  connect(m_fingerprint, &QPushButton::clicked, this, &AdvancedPage::fingerprintRequested);
  connect(m_regenerate, &QPushButton::clicked, this, &AdvancedPage::regenerateCertificate);
  connect(m_viewLog, &QPushButton::clicked, this, &AdvancedPage::showLog);
  connect(m_copyLog, &QPushButton::clicked, this, &AdvancedPage::copyLog);
  connect(m_defaults, &QPushButton::clicked, this, &AdvancedPage::restoreDefaults);
  connect(m_resetAll, &QPushButton::clicked, this, &AdvancedPage::resetAll);
  connect(m_revert, &QPushButton::clicked, this, [this] {
    if (m_applying) return;
    m_draft = m_saved;
    m_syncing = true;
    m_certificatePath->setText(m_savedCertificatePath);
    selectData(m_keySize, m_savedKeySize);
    m_syncing = false;
    refresh();
    setStatusMessage(tr("已还原本页草稿，未更改已保存设置。"));
  });
  connect(m_apply, &QPushButton::clicked, this, [this] {
    if (m_applying || !Settings::isWritable()) return;
    const auto changed = changes();
    if (!changed.isEmpty()) Q_EMIT applyRequested(changed);
  });
  connect(Settings::instance(), &Settings::settingsWritableChanged, this, [this] { refresh(); });
  reload();
}

bool AdvancedPage::isDirty() const
{
  return m_saved != m_draft;
}

bool AdvancedPage::hasPendingEdits() const
{
  return isDirty() || certificateDraftChanged();
}

void AdvancedPage::refreshFromSettings()
{
  if (hasPendingEdits()) refresh();
  else reload();
}

QVariantMap AdvancedPage::changes() const
{
  QVariantMap result;
  for (auto it = m_draft.cbegin(); it != m_draft.cend(); ++it)
    if (m_saved.value(it.key()) != it.value()) result.insert(it.key(), it.value());
  return result;
}

void AdvancedPage::populateInterfaces()
{
  const QSignalBlocker blocker(m_interface);
  const auto current = m_draft.value(Settings::Core::Interface).toString();
  m_interface->clear();
  m_interface->addItem(tr("所有本机地址（默认）"), QString());
  for (const auto &address : NetworkMonitor::validAddresses())
    if (m_interface->findData(address) < 0) m_interface->addItem(address, address);
  if (!current.isEmpty() && m_interface->findData(current) < 0)
    m_interface->addItem(current, current);
  selectData(m_interface, current);
}

void AdvancedPage::reload()
{
  m_syncing = true;
  m_saved.clear();
  for (const auto &key : ordinaryKeys())
    m_saved.insert(key, ordinaryValue(key));
  m_draft = m_saved;
  m_savedCertificatePath = Settings::value(Settings::Security::Certificate).toString();
  m_savedKeySize = Settings::value(Settings::Security::KeySize).toInt();
  // Display only the public certificate's subject, never key material. Keep file
  // reads in reload(), rather than re-reading on every draft or connection update.
  const auto certificates = QSslCertificate::fromPath(m_savedCertificatePath);
  const auto certificateName = certificates.isEmpty() ? QString()
      : certificates.first().subjectInfo(QSslCertificate::CommonName).join(QStringLiteral(", "));
  m_certificateName->setText(certificates.isEmpty() ? tr("当前证书名称：未读取到证书")
      : tr("当前证书名称：%1").arg(certificateName.isEmpty() ? tr("未注明") : certificateName));
  m_certificatePath->setText(m_savedCertificatePath);
  selectData(m_keySize, m_savedKeySize);
  populateInterfaces();
  m_syncing = false;
  refresh();
  setStatusMessage(tr("查看与复制日志立即可用；更改设置需点按“应用更改”。"));
}

void AdvancedPage::setApplying(bool applying)
{
  m_applying = applying;
  if (applying) setStatusMessage(tr("正在应用，请稍候…"));
  refresh();
}

void AdvancedPage::setStatusMessage(const QString &message)
{
  m_status->setText(message);
}

void AdvancedPage::applied(bool success, const QString &message)
{
  m_applying = false;
  if (success) {
    // An ordinary settings save must not discard a separate maintenance draft.
    const bool preserveMaintenanceDraft = certificateDraftChanged();
    const auto path = m_certificatePath->text();
    const auto bits = m_keySize->currentData();
    reload();
    if (preserveMaintenanceDraft) {
      m_syncing = true;
      m_certificatePath->setText(path);
      selectData(m_keySize, bits);
      m_syncing = false;
      refresh();
    }
  } else {
    refresh();
  }
  setStatusMessage(message.isEmpty() ? (success ? tr("更改已应用。") : tr("应用失败，草稿已保留，可重试或还原。")) : message);
}

void AdvancedPage::setLogDocument(QTextDocument *document)
{
  m_logDocument = document;
  if (document)
    connect(document, &QObject::destroyed, this, [this] { refresh(); });
  refresh();
}

void AdvancedPage::setConnectionSecurity(bool secure)
{
  m_connectionKnown = true;
  m_connectionSecure = secure;
  refresh();
}

void AdvancedPage::draftChanged(const QString &key, const QVariant &value)
{
  if (m_syncing) return;
  if (m_applying || !Settings::isWritable() || !m_draft.contains(key)) {
    refresh();
    return;
  }
  m_draft.insert(key, value);
  setStatusMessage(isDirty() ? tr("有待应用的更改。连接参数生效时会短暂重新连接。") : tr("本页设置没有更改。"));
  refresh();
}

bool AdvancedPage::certificateDraftChanged() const
{
  return m_certificatePath->text() != m_savedCertificatePath || m_keySize->currentData().toInt() != m_savedKeySize;
}

void AdvancedPage::refresh()
{
  const bool writable = Settings::isWritable();
  const bool editable = writable && !m_applying;
  m_syncing = true;
  m_readOnly->setVisible(!writable);
  m_securityState->setText(!m_connectionKnown ? tr("尚未收到连接安全状态。")
      : (m_connectionSecure ? tr("当前连接已使用 TLS 加密。") : tr("当前未建立加密连接。")));
  setChecked(m_tlsEnabled, m_draft.value(Settings::Security::TlsEnabled).toBool());
  setChecked(m_checkPeers, m_draft.value(Settings::Security::CheckPeers).toBool());
  m_tlsEnabled->setEnabled(editable);
  m_checkPeers->setEnabled(editable && m_tlsEnabled->isChecked() &&
      Settings::value(Settings::Core::CoreMode).toInt() != static_cast<int>(Settings::Client));
  m_fingerprint->setEnabled(!m_applying);
  {
    const QSignalBlocker blocker(m_port);
    m_port->setValue(m_draft.value(Settings::Core::Port).toInt());
  }
  m_port->setEnabled(editable);
  {
    const QSignalBlocker blocker(m_interface);
    const auto address = m_draft.value(Settings::Core::Interface).toString();
    const auto index = m_interface->findData(address);
    if (index >= 0) {
      if (m_interface->currentIndex() != index || m_interface->currentText() != m_interface->itemText(index)) {
        m_interface->setCurrentIndex(index);
        m_interface->setEditText(m_interface->itemText(index));
      }
    } else if (m_interface->currentText() != address) {
      m_interface->setEditText(address);
    }
  }
  m_interface->setEnabled(editable);
  const bool customNetwork = m_draft.value(Settings::Core::Port).toInt() != Settings::defaultValue(Settings::Core::Port).toInt() ||
      !m_draft.value(Settings::Core::Interface).toString().isEmpty();
  m_networkFold->setText(customNetwork ? tr("网络选项 · 已自定义") : tr("网络选项 · 默认"));
  m_certificatePath->setEnabled(editable);
  m_keySize->setEnabled(editable);
  m_browseCertificate->setEnabled(editable);
  m_regenerate->setEnabled(editable && !m_certificatePath->text().trimmed().isEmpty() && m_keySize->currentData().toInt() > 0);
  m_certificateFold->setText(certificateDraftChanged() ? tr("证书维护 · 参数未执行") : tr("证书维护"));
  m_certificateHint->setText(certificateDraftChanged() ? tr("维护参数尚未执行。重建会改变指纹，另一台电脑需要重新信任本机。")
                                                       : tr("仅在证书损坏或需要更换本机身份时重建；指纹会改变，另一台电脑需要重新信任本机。"));
  const bool hasLog = !m_logDocument.isNull();
  m_viewLog->setEnabled(hasLog);
  m_copyLog->setEnabled(hasLog);
  selectData(m_logLevel, m_draft.value(Settings::Log::Level));
  setChecked(m_guiDebug, m_draft.value(Settings::Log::GuiDebug).toBool());
  setChecked(m_logToFile, m_draft.value(Settings::Log::ToFile).toBool());
  setText(m_logFile, m_draft.value(Settings::Log::File).toString());
  m_logLevel->setEnabled(editable);
  m_guiDebug->setEnabled(editable);
  m_logToFile->setEnabled(editable);
  m_logFile->setEnabled(editable && m_logToFile->isChecked());
  m_browseLog->setEnabled(editable && m_logToFile->isChecked());
  const auto logLevel = m_draft.value(Settings::Log::Level).toString();
  const bool detailed = static_cast<int>(LogLevel::fromOption(logLevel)) > static_cast<int>(LogLevel::Level::Info) || m_guiDebug->isChecked();
  QString diagnosticState;
  if (detailed) diagnosticState = tr("详细调试已开启");
  else if (logLevel.compare(QStringLiteral("INFO"), Qt::CaseInsensitive) != 0) diagnosticState = tr("日志级别 %1").arg(logLevel);
  else diagnosticState = tr("基本日志");
  if (m_logToFile->isChecked()) diagnosticState += tr("，写入文件");
  m_diagnosticFold->setText(tr("诊断选项 · %1").arg(diagnosticState));
  setChecked(m_enableEnter, m_draft.value(Settings::Core::EnableEnterCommand).toBool());
  setChecked(m_enableExit, m_draft.value(Settings::Core::EnableExitCommand).toBool());
  setText(m_enterCommand, m_draft.value(Settings::Core::ComputerEnterCommand).toString());
  setText(m_exitCommand, m_draft.value(Settings::Core::ComputerExitCommand).toString());
  m_enableEnter->setEnabled(editable);
  m_enableExit->setEnabled(editable);
  m_enterCommand->setEnabled(editable && m_enableEnter->isChecked());
  m_exitCommand->setEnabled(editable && m_enableExit->isChecked());
  QString automationState;
  if (m_enableEnter->isChecked() && m_enableExit->isChecked()) automationState = tr("进入与离开命令已启用");
  else if (m_enableEnter->isChecked()) automationState = tr("进入命令已启用");
  else if (m_enableExit->isChecked()) automationState = tr("离开命令已启用");
  else if (!m_enterCommand->text().isEmpty() || !m_exitCommand->text().isEmpty()) automationState = tr("已保存命令，当前停用");
  else automationState = tr("未启用");
  m_automationFold->setText(tr("自动化 · %1").arg(automationState));
#ifdef Q_OS_WIN
  const bool portable = Settings::isPortableMode();
  const int mode = portable ? static_cast<int>(Settings::Desktop) : m_draft.value(Settings::Core::ProcessMode).toInt();
  selectData(m_processMode, mode);
  setChecked(m_elevate, portable ? false : m_draft.value(Settings::Daemon::Elevate).toBool());
  m_processMode->setEnabled(editable && !portable);
  m_elevate->setEnabled(editable && !portable && mode == static_cast<int>(Settings::Service));
  m_processFold->setText(mode == static_cast<int>(Settings::Service) ? tr("运行方式 · 后台系统服务") : tr("运行方式 · 桌面进程"));
#endif
  m_defaults->setEnabled(editable);
  m_resetAll->setEnabled(editable);
  m_revert->setEnabled(!m_applying && (isDirty() || certificateDraftChanged()));
  m_apply->setEnabled(editable && isDirty());
  m_syncing = false;
  const auto dirty = isDirty();
  if (dirty != m_lastDirty) {
    m_lastDirty = dirty;
    Q_EMIT dirtyChanged(dirty);
  }
}

void AdvancedPage::restoreDefaults()
{
  if (m_applying || !Settings::isWritable()) return;
  QMessageBox confirmation(QMessageBox::Question, tr("恢复高级页默认值"),
      tr("将连接安全、网络、诊断与自动化选项恢复为默认草稿？\n\n证书身份和其他页面设置将保留。点按“应用更改”后才会保存。"),
      QMessageBox::NoButton, this);
  auto *restore = confirmation.addButton(tr("恢复本页草稿"), QMessageBox::AcceptRole);
  confirmation.addButton(QMessageBox::Cancel);
  confirmation.setDefaultButton(QMessageBox::Cancel);
  confirmation.exec();
  if (confirmation.clickedButton() != restore || m_applying || !Settings::isWritable()) return;
  for (const auto &key : ordinaryKeys())
    m_draft.insert(key, ordinaryValue(key, true));
  refresh();
  setStatusMessage(tr("已恢复高级页默认草稿，点按“应用更改”后生效。证书身份已保留。"));
}

void AdvancedPage::showLog()
{
  if (!m_logDocument) return;
  QDialog dialog(this);
  dialog.setObjectName(QStringLiteral("runtimeLogDialog"));
  dialog.setWindowTitle(tr("运行日志"));
  dialog.resize(760, 480);
  dialog.setMinimumSize(480, 300);
  auto *layout = new QVBoxLayout(&dialog);
  auto *log = new QPlainTextEdit(&dialog);
  log->setObjectName(QStringLiteral("runtimeLog"));
  log->setReadOnly(true);
  log->setLineWrapMode(QPlainTextEdit::NoWrap);
  log->setFont(fixedFont());
  log->setPlaceholderText(tr("连接后，运行日志将显示在这里。"));
  // Reuse the existing bounded document; neither viewing nor copying changes settings.
  log->setDocument(m_logDocument.data());
  log->moveCursor(QTextCursor::End);
  log->horizontalScrollBar()->setValue(0);
  layout->addWidget(log);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
  auto *copy = buttons->addButton(tr("复制日志"), QDialogButtonBox::ActionRole);
  copy->setAutoDefault(false);
  connect(copy, &QPushButton::clicked, this, &AdvancedPage::copyLog);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);
  applyPersonalStyle(&dialog);
  dialog.exec();
}

void AdvancedPage::copyLog()
{
  if (!m_logDocument) return;
  QApplication::clipboard()->setText(m_logDocument->toPlainText());
  setStatusMessage(tr("日志已复制。"));
}

void AdvancedPage::regenerateCertificate()
{
  if (m_applying || !Settings::isWritable()) return;
  const auto path = m_certificatePath->text().trimmed();
  const auto bits = m_keySize->currentData().toInt();
  if (path.isEmpty() || bits <= 0) return;
  if (isDirty()) {
    setStatusMessage(tr("请先应用或还原本页设置草稿，再单独重建证书。"));
    return;
  }
  QMessageBox confirmation(QMessageBox::Warning, tr("重建本机证书"),
      tr("将以 %1 为证书名称，使用 %2 位密钥重建本机证书，并覆盖以下路径的证书文件：\n%3\n\n指纹会改变，共享会短暂断开，另一台电脑需要重新信任本机。只有确认后才会保存维护参数并执行。")
          .arg(QString::fromUtf8(kAppName)).arg(bits).arg(path), QMessageBox::NoButton, this);
  auto *regenerate = confirmation.addButton(tr("重建证书"), QMessageBox::DestructiveRole);
  confirmation.addButton(QMessageBox::Cancel);
  confirmation.setDefaultButton(QMessageBox::Cancel);
  confirmation.setTextFormat(Qt::PlainText);
  confirmation.exec();
  if (confirmation.clickedButton() == regenerate && !m_applying && Settings::isWritable())
    Q_EMIT regenerateCertificateRequested(path, bits);
}

void AdvancedPage::resetAll()
{
  if (m_applying || !Settings::isWritable()) return;
  QMessageBox confirmation(QMessageBox::Warning, tr("清除全部设置并重启"),
      tr("将清除桌桥保存的全部偏好、设备连接和快捷键配置，并重启应用。当前未应用的草稿也会丢失。\n\n此操作无法撤销。"),
      QMessageBox::NoButton, this);
  auto *clear = confirmation.addButton(tr("清除并重启"), QMessageBox::DestructiveRole);
  confirmation.addButton(QMessageBox::Cancel);
  confirmation.setDefaultButton(QMessageBox::Cancel);
  confirmation.exec();
  if (confirmation.clickedButton() == clear && !m_applying && Settings::isWritable())
    Q_EMIT resetAllRequested();
}

} // namespace deskflow::gui::personal
