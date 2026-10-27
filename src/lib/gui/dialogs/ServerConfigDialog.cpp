/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 - 2026 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2008 Volker Lanz <vl@fidra.de>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ServerConfigDialog.h"
#include "ServerConfigView.h"

#include "common/Constants.h"
#include "common/NetworkProtocol.h"
#include "common/PlatformInfo.h"
#include "common/Settings.h"
#include "dialogs/ActionDialog.h"
#include "dialogs/ComputerSettingsDialog.h"
#include "dialogs/HotkeyDialog.h"
#include "gui/widgets/SettingsDialogButtonBox.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QKeySequence>
#include <QMessageBox>

using enum ComputerConfig::SwitchCorner;

namespace {
QString displayKeySequence(const KeySequence &sequence)
{
  QStringList keys;
  for (const int key : sequence.sequence()) {
    if (key < Qt::Key_Space) {
      if (key == Qt::LeftButton)
        keys.append(QStringLiteral("鼠标左键"));
      else if (key == Qt::RightButton)
        keys.append(QStringLiteral("鼠标右键"));
      else if (key == Qt::MiddleButton)
        keys.append(QStringLiteral("鼠标中键"));
      else
        keys.append(QStringLiteral("鼠标侧键"));
    } else if ((key & Qt::KeyboardModifierMask) && !(key & ~Qt::KeyboardModifierMask)) {
      if (key & Qt::ControlModifier)
        keys.append(QStringLiteral("Ctrl"));
      if (key & Qt::AltModifier)
        keys.append(QStringLiteral("Alt"));
      if (key & Qt::ShiftModifier)
        keys.append(QStringLiteral("Shift"));
      if (key & Qt::MetaModifier)
        keys.append(QStringLiteral("Meta"));
    } else {
      const QString name =
          QKeySequence(QKeyCombination(Qt::NoModifier, static_cast<Qt::Key>(key))).toString(QKeySequence::NativeText);
      if (!name.isEmpty())
        keys.append(name);
    }
  }
  return keys.isEmpty() ? QStringLiteral("未设置按键") : keys.join(QStringLiteral(" + "));
}

QString displayHotkey(const Hotkey &hotkey)
{
  return QStringLiteral("%1  ·  %2 个动作").arg(displayKeySequence(hotkey.keySequence())).arg(hotkey.actions().size());
}

QString displayAction(const Action &action)
{
  QString description;
  const QString keys = displayKeySequence(action.keySequence());
  const QString targets = action.keySequence().isMouseButton()
                              ? QStringLiteral("当前受控设备")
                              : (action.haveComputers() && !action.typeComputerNames().isEmpty()
                                     ? action.typeComputerNames().join(QStringLiteral("、"))
                                     : QStringLiteral("全部设备"));
  switch (static_cast<Action::Type>(action.type())) {
  case Action::Type::keyDown:
  case Action::Type::mouseDown:
    description = QStringLiteral("按住 %1 → %2").arg(keys, targets);
    break;
  case Action::Type::keyUp:
  case Action::Type::mouseUp:
    description = QStringLiteral("释放 %1 → %2").arg(keys, targets);
    break;
  case Action::Type::keystroke:
  case Action::Type::mousebutton:
    description = action.tapAtTrigger() ? QStringLiteral("点按 %1 → %2").arg(keys, targets)
                                        : QStringLiteral("随热键保持 %1 → %2").arg(keys, targets);
    break;
  case Action::Type::switchToComputer:
    description = QStringLiteral("切换到设备 %1").arg(action.switchComputerName());
    break;
  case Action::Type::switchInDirection: {
    const QStringList directions{
        QStringLiteral("左"), QStringLiteral("右"), QStringLiteral("上"), QStringLiteral("下")
    };
    description =
        QStringLiteral("切换到%1方相邻设备").arg(directions.value(action.switchDirection(), QStringLiteral("指定")));
    break;
  }
  case Action::Type::switchToNextComputer:
    description = QStringLiteral("切换到下一台设备");
    break;
  case Action::Type::lockCursorToComputer: {
    const QStringList modes{
        QStringLiteral("切换键鼠锁定状态"), QStringLiteral("将键鼠锁定在当前设备"), QStringLiteral("解除键鼠锁定")
    };
    description = modes.value(action.lockCursorMode(), modes.first());
    break;
  }
  case Action::Type::restartAllConnections:
    description = QStringLiteral("重启共享连接");
    break;
  default:
    description = QStringLiteral("未知动作，请编辑后检查");
    break;
  }
  const bool hold = (action.type() == static_cast<int>(Action::Type::keystroke) ||
                     action.type() == static_cast<int>(Action::Type::mousebutton)) &&
                    !action.tapAtTrigger();
  return QStringLiteral("%1  ·  %2")
      .arg(
          hold                       ? QStringLiteral("按下保持，松开释放")
          : action.activeOnRelease() ? QStringLiteral("松开热键时")
                                     : QStringLiteral("按下热键时"),
          description
      );
}
} // namespace

ServerConfigDialog::ServerConfigDialog(QWidget *parent, ServerConfig &config)
    : QDialog(parent, Qt::WindowTitleHint | Qt::WindowSystemMenuHint),
      ui{std::make_unique<Ui::ServerConfigDialog>()},
      m_columns{Settings::value(Settings::Server::GridWidth).toInt()},
      m_rows{Settings::value(Settings::Server::GridHeight).toInt()},
      m_originalServerConfig(config),
      m_originalServerConfigIsExternal(config.useExternalConfig()),
      m_originalServerConfigUsesExternalFile(config.configFile()),
      m_serverConfig(config),
      m_computerSetupModel(m_serverConfig.computers(), m_columns, m_rows),
      m_buttonBox{new SettingsDialogButtonBox(this)}
{
  ui->setupUi(this);
  ui->tabWidget->setCurrentIndex(0);
  m_buttonBox->setProperty("personalFooter", true);
  layout()->addWidget(m_buttonBox);

  loadFromConfig();

  ui->lblRemoveComputer->setPixmap(deskflow::gui::personal::personalCloseIcon().pixmap(QSize(32, 32)));
  ui->lblNewComputer->setEnabled(!model().isFull());
  ui->lblNewComputer->setPixmap(
      deskflow::gui::personal::personalIcon(QStringLiteral("devices"), deskflow::gui::personal::personalAccentColor())
          .pixmap(QSize(36, 36))
  );
  ui->btnBrowseConfigFile->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::DocumentOpen));

  if (!deskflow::platform::isWindows())
    ui->cbWin32KeepForeground->setVisible(false);
  initConnections();
  updateControls();
}

ServerConfigDialog::~ServerConfigDialog() = default;

void ServerConfigDialog::message(const QString &message)
{
  m_message = message;
  if (!message.isEmpty()) {
    auto *notice = new QLabel(message, this);
    notice->setWordWrap(true);
    notice->setTextFormat(Qt::PlainText);
    notice->setProperty("secondaryText", true);
    notice->setContentsMargins(24, 0, 24, 14);
    static_cast<QVBoxLayout *>(layout())->insertWidget(1, notice);
  }
}

bool ServerConfigDialog::addClient(const QString &clientName)
{
  return addComputer(clientName, true);
}

void ServerConfigDialog::save()
{
  if (!Settings::isWritable())
    return;
  if (ui->groupExternalConfig->isChecked() && !QFileInfo(ui->lineConfigFile->text()).isFile()) {
    QMessageBox::warning(this, tr("配置文件不可用"), tr("请选择存在的配置文件后再保存。"));
    ui->navigation->setCurrentRow(3);
    ui->lineConfigFile->setFocus();
    return;
  }
  // now that the dialog has been accepted, copy the new server config to the
  // original one, which is a reference to the one in MainWindow.
  setOriginalServerConfig(serverConfig());
  if (Settings::value(Settings::Server::EnableClipboard).toBool() != m_enableClipboard) {
    Settings::setValue(Settings::Server::XdpRestoreToken, QString());
    Settings::setValue(Settings::Server::XdpClipboardRetried, false);
  }
  Settings::setValue(Settings::Server::Protocol, networkProtocolToOption(m_protocol));
  Settings::setValue(Settings::Server::EnableClipboard, m_enableClipboard);
  Settings::setValue(Settings::Server::ClipboardSize, m_clipboardSize);
  Settings::setValue(Settings::Server::EnableHeartbeat, m_enableHeartbeat);
  Settings::setValue(Settings::Server::Heartbeat, m_heartbeatRate);
  Settings::setValue(Settings::Server::EnableSwitchDelay, m_enableSwitchDelay);
  Settings::setValue(Settings::Server::SwitchDelay, m_switchDelay);
  Settings::setValue(Settings::Server::DefaultLockToComputerState, m_defaultLockToComputerState);
  Settings::setValue(Settings::Server::DisableLockToComputer, m_disableLockToComputer);
  Settings::setValue(Settings::Server::EnableSwitchDoubleTap, m_enableSwitchDoubleTap);
  Settings::setValue(Settings::Server::SwitchDoubleTap, m_switchDoubleTap);
  Settings::setValue(Settings::Server::RelativeMouseMoves, m_relativeMouseMoves);
  Settings::setValue(Settings::Server::Win32KeepForeground, m_win32keepForeground);
  Settings::setValue(Settings::Server::ExternalConfig, ui->groupExternalConfig->isChecked());
  Settings::setValue(Settings::Server::ExternalConfigFile, ui->lineConfigFile->text());

  QStringList computerNames;
  const auto computerList = m_computerSetupModel.m_computers;
  for (const auto &computer : computerList) {
    const auto &computerName = computer.name();
    if (computerName.isEmpty())
      continue;
    computerNames.append(QStringLiteral("computer_%1").arg(computerName));
    Settings::setValue(Settings::Computer::Aliases.arg(computerName), computer.aliases());
    Settings::setValue(Settings::Computer::Name.arg(computerName), computerName);
  }
  Settings::removeUnknownComputers(computerNames);
  QDialog::accept();
}

void ServerConfigDialog::cancel()
{
  QDialog::reject();
}

void ServerConfigDialog::addHotkey()
{
  Hotkey hotkey;
  HotkeyDialog dlg(this, hotkey);
  if (dlg.exec() == QDialog::Accepted) {
    serverConfig().hotkeys().append(hotkey);
    ui->listHotkeys->addItem(displayHotkey(hotkey));
    setButtonBoxEnabledButtons();
  }
}

void ServerConfigDialog::editHotkey()
{
  int row = ui->listHotkeys->currentRow();
  if (row < 0 || row >= serverConfig().hotkeys().size()) {
    qDebug() << "Attempt to editing out of bounds hotkey row: " << row;
    return;
  }

  Hotkey &hotkey = serverConfig().hotkeys()[row];
  HotkeyDialog dlg(this, hotkey);
  if (dlg.exec() == QDialog::Accepted) {
    ui->listHotkeys->currentItem()->setText(displayHotkey(hotkey));
    setButtonBoxEnabledButtons();
  }
}

void ServerConfigDialog::removeHotkey()
{
  int row = ui->listHotkeys->currentRow();
  if (row < 0 || row >= serverConfig().hotkeys().size()) {
    qDebug() << "Attempt to remove out of bounds hotkey row: " << row;
    return;
  }

  serverConfig().hotkeys().removeAt(row);
  ui->listActions->clear();
  delete ui->listHotkeys->item(row);
  setButtonBoxEnabledButtons();
}

void ServerConfigDialog::listHotkeysSelectionChanged(const QItemSelection &selected, const QItemSelection &)
{
  bool itemsSelected = Settings::isWritable() && !selected.isEmpty();
  ui->btnEditHotkey->setEnabled(itemsSelected);
  ui->btnRemoveHotkey->setEnabled(itemsSelected);
  ui->btnNewAction->setEnabled(itemsSelected);

  ui->listActions->clear();
  if (itemsSelected && !serverConfig().hotkeys().isEmpty()) {
    const Hotkey &hotkey = serverConfig().hotkeys().at(selected.indexes().first().row());
    for (const Action &action : hotkey.actions())
      ui->listActions->addItem(displayAction(action));
  }
}

void ServerConfigDialog::addAction()
{
  int row = ui->listHotkeys->currentRow();
  if (row < 0 || row >= serverConfig().hotkeys().size()) {
    qDebug() << "Attempt to add action to out of bounds hotkey row: " << row;
    return;
  }

  Hotkey &hotkey = serverConfig().hotkeys()[row];
  Action action;
  ActionDialog dlg(this, serverConfig(), hotkey, action);
  if (dlg.exec() == QDialog::Accepted) {
    hotkey.addAction(action);
    ui->listActions->addItem(displayAction(action));
    ui->listHotkeys->currentItem()->setText(displayHotkey(hotkey));
    setButtonBoxEnabledButtons();
  }
}

void ServerConfigDialog::editAction()
{
  int hotkeyRow = ui->listHotkeys->currentRow();
  if (hotkeyRow < 0 || hotkeyRow >= serverConfig().hotkeys().size()) {
    qDebug() << "Attempt to edit action from out of bounds hotkey row: " << hotkeyRow;
    return;
  }
  Hotkey &hotkey = serverConfig().hotkeys()[hotkeyRow];

  int actionRow = ui->listActions->currentRow();
  if (actionRow < 0 || actionRow >= hotkey.actions().size()) {
    qDebug() << "Attempt to remove out of bounds action row: " << actionRow;
    return;
  }
  Action &action = hotkey.actionAt(actionRow);

  ActionDialog dlg(this, serverConfig(), hotkey, action);
  if (dlg.exec() == QDialog::Accepted) {
    ui->listActions->currentItem()->setText(displayAction(action));
    setButtonBoxEnabledButtons();
  }
}

void ServerConfigDialog::removeAction()
{
  int hotkeyRow = ui->listHotkeys->currentRow();
  if (hotkeyRow < 0 || hotkeyRow >= serverConfig().hotkeys().size()) {
    qDebug() << "Attempt to remove action from out of bounds hotkey row: " << hotkeyRow;
    return;
  }
  Hotkey &hotkey = serverConfig().hotkeys()[hotkeyRow];

  int actionRow = ui->listActions->currentRow();
  if (actionRow < 0 || actionRow >= hotkey.actions().size()) {
    qDebug() << "Attempt to remove out of bounds action row: " << actionRow;
    return;
  }

  hotkey.removeActionAt(actionRow);
  delete ui->listActions->currentItem();
  ui->listHotkeys->currentItem()->setText(displayHotkey(hotkey));
  setButtonBoxEnabledButtons();
}

void ServerConfigDialog::toggleClipboard(bool enabled)
{
  if (m_enableClipboard == enabled)
    return;

  m_enableClipboard = enabled;

  ui->sbClipboardSizeLimit->setEnabled(enabled && Settings::isWritable());
  if (enabled && !ui->sbClipboardSizeLimit->value()) {
    m_clipboardSize = Settings::defaultValue(Settings::Server::ClipboardSize).toUInt();
    ui->sbClipboardSizeLimit->setValue(m_clipboardSize ? m_clipboardSize : 1);
  }
  setButtonBoxEnabledButtons();
}

void ServerConfigDialog::setClipboardLimit(int limit)
{
  if (m_clipboardSize == limit)
    return;

  m_clipboardSize = limit;
  setButtonBoxEnabledButtons();
}

void ServerConfigDialog::toggleHeartbeat(bool enabled)
{
  m_enableHeartbeat = enabled;
  ui->sbHeartbeat->setEnabled(enabled && Settings::isWritable());
  setButtonBoxEnabledButtons();
}

void ServerConfigDialog::setHeartbeat(int rate)
{
  if (rate == m_heartbeatRate)
    return;
  m_heartbeatRate = rate;
  setButtonBoxEnabledButtons();
}

void ServerConfigDialog::toggleRelativeMouseMoves(bool enabled)
{
  if (m_relativeMouseMoves == enabled)
    return;
  m_relativeMouseMoves = enabled;
  setButtonBoxEnabledButtons();
}

void ServerConfigDialog::toggleProtocol()
{
  m_protocol = ui->rbProtocolBarrier->isChecked() ? NetworkProtocol::Barrier : NetworkProtocol::Synergy;
  setButtonBoxEnabledButtons();
}

void ServerConfigDialog::listActionsSelectionChanged(const QItemSelection &selected, const QItemSelection &)
{
  bool enabled = Settings::isWritable() && !selected.isEmpty();
  ui->btnEditAction->setEnabled(enabled);
  ui->btnRemoveAction->setEnabled(enabled);
}

void ServerConfigDialog::toggleSwitchDoubleTap(bool enable)
{
  m_enableSwitchDoubleTap = enable;
  ui->sbSwitchDoubleTap->setEnabled(enable && Settings::isWritable());
  setButtonBoxEnabledButtons();
}

void ServerConfigDialog::setSwitchDoubleTap(int within)
{
  if (m_switchDoubleTap == within)
    return;
  m_switchDoubleTap = within;
  setButtonBoxEnabledButtons();
}

void ServerConfigDialog::toggleSwitchDelay(bool enable)
{
  m_enableSwitchDelay = enable;
  ui->sbSwitchDelay->setEnabled(enable && Settings::isWritable());
  setButtonBoxEnabledButtons();
}

void ServerConfigDialog::setSwitchDelay(int delay)
{
  if (m_switchDelay == delay)
    return;
  m_switchDelay = delay;
  setButtonBoxEnabledButtons();
}

void ServerConfigDialog::toggleDefaultLockToComputerState(bool state)
{
  if (m_defaultLockToComputerState == state)
    return;
  m_defaultLockToComputerState = state;
  setButtonBoxEnabledButtons();
}

void ServerConfigDialog::toggleLockToComputer(bool disabled)
{
  if (m_disableLockToComputer == disabled)
    return;
  m_disableLockToComputer = disabled;
  setButtonBoxEnabledButtons();
}

void ServerConfigDialog::toggleWin32Foreground(bool enabled)
{
  if (m_win32keepForeground == enabled)
    return;
  m_win32keepForeground = enabled;
  setButtonBoxEnabledButtons();
}

void ServerConfigDialog::addClient()
{
  addComputer("", false);
}

void ServerConfigDialog::onComputerRemoved()
{
  updateComputerControls();
  setButtonBoxEnabledButtons();
}

void ServerConfigDialog::toggleExternalConfig(bool checked)
{
  ui->externalConfigNotice->setVisible(checked);
  updateControls();
}

bool ServerConfigDialog::browseConfigFile()
{
  //: %1 is replaced with the application names
  //: (*.conf) and (*.*) should not be translated
  const auto deskflowConfigFilter = tr("%1 配置文件 (*.conf);;所有文件 (*.*)");

  QString fileName = QFileDialog::getOpenFileName(this, tr("选择共享配置文件"), "", deskflowConfigFilter.arg(kAppName));

  if (!fileName.isEmpty()) {
    ui->lineConfigFile->setText(fileName);
    setServerConfig();
    return true;
  }

  return false;
}

void ServerConfigDialog::loadFromConfig()
{
  m_protocol = Settings::networkProtocol();
  m_enableHeartbeat = Settings::value(Settings::Server::EnableHeartbeat).toBool();
  m_heartbeatRate = Settings::value(Settings::Server::Heartbeat).toInt();
  m_relativeMouseMoves = Settings::value(Settings::Server::RelativeMouseMoves).toBool();
  m_win32keepForeground = Settings::value(Settings::Server::Win32KeepForeground).toBool();
  m_enableSwitchDelay = Settings::value(Settings::Server::EnableSwitchDelay).toBool();
  m_switchDelay = Settings::value(Settings::Server::SwitchDelay).toInt();
  m_enableSwitchDoubleTap = Settings::value(Settings::Server::EnableSwitchDoubleTap).toBool();
  m_switchDoubleTap = Settings::value(Settings::Server::SwitchDoubleTap).toInt();
  m_defaultLockToComputerState = Settings::value(Settings::Server::DefaultLockToComputerState).toBool();
  m_disableLockToComputer = Settings::value(Settings::Server::DisableLockToComputer).toBool();
  m_enableClipboard = Settings::value(Settings::Server::EnableClipboard).toBool();
  m_clipboardSize = Settings::value(Settings::Server::ClipboardSize).toUInt();

  ui->lineConfigFile->setText(m_originalServerConfigUsesExternalFile);
  ui->groupExternalConfig->setChecked(m_originalServerConfigIsExternal);

  refreshControls();

  ui->listHotkeys->clear();
  ui->listActions->clear();
  for (const Hotkey &hotkey : std::as_const(serverConfig().hotkeys()))
    ui->listHotkeys->addItem(displayHotkey(hotkey));

  ui->computerSetupView->setModel(&m_computerSetupModel);

  auto &computers = serverConfig().computers();
  auto server = std::ranges::find_if(computers, [this](const Computer &computer) {
    return (computer.name() == serverConfig().getServerName());
  });

  if (server == computers.end()) {
    Computer serverComputer(serverConfig().getServerName());
    serverComputer.markAsServer();
    model().computer(m_columns / 2, m_rows / 2) = serverComputer;
  } else {
    server->markAsServer();
  }
  updateControls();
}

void ServerConfigDialog::resetFromSettings()
{
  m_serverConfig = m_originalServerConfig;
  loadFromConfig();
  ui->computerSetupView->reset();
  ui->computerSetupView->update();
}

void ServerConfigDialog::refreshControls()
{
  ui->rbProtocolSynergy->setChecked(m_protocol == NetworkProtocol::Synergy);
  ui->rbProtocolBarrier->setChecked(m_protocol == NetworkProtocol::Barrier);
  ui->cbHeartbeat->setChecked(m_enableHeartbeat);
  ui->sbHeartbeat->setEnabled(ui->cbHeartbeat->isChecked());
  ui->sbHeartbeat->setValue(m_heartbeatRate);
  ui->cbRelativeMouseMoves->setChecked(m_relativeMouseMoves);
  ui->cbWin32KeepForeground->setChecked(m_win32keepForeground);
  ui->cbSwitchDelay->setChecked(m_enableSwitchDelay);
  ui->sbSwitchDelay->setEnabled(ui->cbSwitchDelay->isChecked());
  ui->sbSwitchDelay->setValue(m_switchDelay);
  ui->cbSwitchDoubleTap->setChecked(m_enableSwitchDoubleTap);
  ui->sbSwitchDoubleTap->setEnabled(ui->cbSwitchDoubleTap->isChecked());
  ui->sbSwitchDoubleTap->setValue(m_switchDoubleTap);
  ui->widgetExternalConfigControls->setEnabled(ui->groupExternalConfig->isChecked());
  toggleExternalConfig(ui->groupExternalConfig->isChecked());
  ui->cbDefaultLockToComputerState->setChecked(m_defaultLockToComputerState);
  ui->cbDisableLockToComputer->setChecked(m_disableLockToComputer);
  ui->cbEnableClipboard->setChecked(m_enableClipboard);
  ui->sbClipboardSizeLimit->setEnabled(m_enableClipboard);
  ui->sbClipboardSizeLimit->setValue(m_clipboardSize);
}

void ServerConfigDialog::initConnections() const
{
  connect(m_buttonBox, &SettingsDialogButtonBox::accepted, this, &ServerConfigDialog::save);
  connect(m_buttonBox, &SettingsDialogButtonBox::rejected, this, &ServerConfigDialog::cancel);
  connect(m_buttonBox, &SettingsDialogButtonBox::reset, this, &ServerConfigDialog::resetFromSettings);
  connect(m_buttonBox, &SettingsDialogButtonBox::restoreDefault, this, &ServerConfigDialog::restoreFromDefaults);
  connect(ui->tabWidget, &QStackedWidget::currentChanged, this, &ServerConfigDialog::setButtonBoxEnabledButtons);
  connect(
      ui->btnAddComputer, &QPushButton::clicked, this,
      static_cast<void (ServerConfigDialog::*)()>(&ServerConfigDialog::addClient)
  );
  connect(ui->btnEditComputer, &QPushButton::clicked, this, &ServerConfigDialog::editComputer);
  connect(ui->btnRemoveComputer, &QPushButton::clicked, this, &ServerConfigDialog::removeComputer);
  connect(
      ui->computerSetupView->selectionModel(), &QItemSelectionModel::selectionChanged, this,
      &ServerConfigDialog::updateComputerControls
  );
  connect(ui->lblRemoveComputer, &RemoveComputerWidget::computerRemoved, this, &ServerConfigDialog::onComputerRemoved);
  connect(ui->btnNewHotkey, &QPushButton::clicked, this, &ServerConfigDialog::addHotkey);
  connect(ui->btnEditHotkey, &QPushButton::clicked, this, &ServerConfigDialog::editHotkey);
  connect(ui->btnRemoveHotkey, &QPushButton::clicked, this, &ServerConfigDialog::removeHotkey);
  connect(ui->listHotkeys, &QListView::doubleClicked, this, &ServerConfigDialog::editHotkey);
  connect(
      ui->listHotkeys->selectionModel(), &QItemSelectionModel::selectionChanged, this,
      &ServerConfigDialog::listHotkeysSelectionChanged
  );

  connect(ui->btnNewAction, &QPushButton::clicked, this, &ServerConfigDialog::addAction);
  connect(ui->btnEditAction, &QPushButton::clicked, this, &ServerConfigDialog::editAction);
  connect(ui->btnRemoveAction, &QPushButton::clicked, this, &ServerConfigDialog::removeAction);
  connect(ui->listActions, &QListView::doubleClicked, this, &ServerConfigDialog::editAction);
  connect(
      ui->listActions->selectionModel(), &QItemSelectionModel::selectionChanged, this,
      &ServerConfigDialog::listActionsSelectionChanged
  );

  connect(ui->rbProtocolBarrier, &QRadioButton::toggled, this, &ServerConfigDialog::toggleProtocol);
  connect(ui->cbHeartbeat, &QCheckBox::toggled, this, &ServerConfigDialog::toggleHeartbeat);
  connect(ui->sbHeartbeat, QOverload<int>::of(&QSpinBox::valueChanged), this, &ServerConfigDialog::setHeartbeat);
  connect(ui->cbWin32KeepForeground, &QCheckBox::toggled, this, &ServerConfigDialog::toggleWin32Foreground);
  connect(ui->cbSwitchDelay, &QCheckBox::toggled, this, &ServerConfigDialog::toggleSwitchDelay);
  connect(ui->sbSwitchDelay, QOverload<int>::of(&QSpinBox::valueChanged), this, &ServerConfigDialog::setSwitchDelay);
  connect(ui->cbSwitchDoubleTap, &QCheckBox::toggled, this, &ServerConfigDialog::toggleSwitchDoubleTap);
  connect(
      ui->sbSwitchDoubleTap, QOverload<int>::of(&QSpinBox::valueChanged), this, &ServerConfigDialog::setSwitchDoubleTap
  );

  connect(ui->cbRelativeMouseMoves, &QCheckBox::toggled, this, &ServerConfigDialog::toggleRelativeMouseMoves);
  connect(ui->cbEnableClipboard, &QCheckBox::toggled, this, &ServerConfigDialog::toggleClipboard);
  connect(ui->btnBrowseConfigFile, &QPushButton::clicked, this, &ServerConfigDialog::browseConfigFile);
  connect(ui->btnExportConfig, &QPushButton::clicked, this, &ServerConfigDialog::exportConfig);
  connect(ui->groupExternalConfig, &QCheckBox::toggled, this, &ServerConfigDialog::toggleExternalConfig);
  connect(ui->lineConfigFile, &QLineEdit::textChanged, this, &ServerConfigDialog::setServerConfig);

  connect(
      ui->sbClipboardSizeLimit, QOverload<int>::of(&QSpinBox::valueChanged), this,
      &ServerConfigDialog::setClipboardLimit
  );
  connect(
      ui->cbDefaultLockToComputerState, &QCheckBox::toggled, this, &ServerConfigDialog::toggleDefaultLockToComputerState
  );
  connect(ui->cbDisableLockToComputer, &QCheckBox::toggled, this, &ServerConfigDialog::toggleLockToComputer);
  connect(
      &m_computerSetupModel, &ComputerSetupModel::computersChanged, this,
      &ServerConfigDialog::setButtonBoxEnabledButtons
  );
  connect(Settings::instance(), &Settings::settingsWritableChanged, this, &ServerConfigDialog::updateControls);
}

void ServerConfigDialog::updateControls() const
{
  const bool writable = Settings::isWritable();
  const bool external = ui->groupExternalConfig->isChecked();
  ui->tabComputers->setEnabled(writable && !external);
  ui->tabHotkeys->setEnabled(writable && !external);
  ui->computerSetupView->setDragEnabled(writable && !external);
  ui->computerSetupView->setAcceptDrops(writable && !external);
  ui->widgetExternalConfigControls->setEnabled(writable && external);
  ui->cbDefaultLockToComputerState->setEnabled(writable);
  ui->cbDisableLockToComputer->setEnabled(writable);
  ui->cbEnableClipboard->setEnabled(writable);
  ui->sbClipboardSizeLimit->setEnabled(writable && ui->cbEnableClipboard->isChecked());
  ui->rbProtocolBarrier->setEnabled(writable);
  ui->rbProtocolSynergy->setEnabled(writable);
  ui->cbHeartbeat->setEnabled(writable);
  ui->sbHeartbeat->setEnabled(writable && ui->cbHeartbeat->isChecked());
  ui->cbRelativeMouseMoves->setEnabled(writable);
  ui->cbSwitchDelay->setEnabled(writable);
  ui->cbWin32KeepForeground->setEnabled(writable);
  ui->cbSwitchDoubleTap->setEnabled(writable);
  ui->sbSwitchDoubleTap->setEnabled(writable && ui->cbSwitchDoubleTap->isChecked());
  ui->sbSwitchDelay->setEnabled(writable && ui->cbSwitchDelay->isChecked());
  ui->groupExternalConfig->setEnabled(writable);
  ui->btnNewHotkey->setEnabled(writable && !external);
  const bool hotkeySelected = writable && !external && ui->listHotkeys->currentRow() >= 0;
  ui->btnEditHotkey->setEnabled(hotkeySelected);
  ui->btnRemoveHotkey->setEnabled(hotkeySelected);
  ui->btnNewAction->setEnabled(hotkeySelected);
  const bool actionSelected = hotkeySelected && ui->listActions->currentRow() >= 0;
  ui->btnEditAction->setEnabled(actionSelected);
  ui->btnRemoveAction->setEnabled(actionSelected);
  updateComputerControls();
  setButtonBoxEnabledButtons();
}

void ServerConfigDialog::restoreFromDefaults()
{
  m_protocol = networkProtocolFromString(Settings::defaultValue(Settings::Server::Protocol).toString());
  m_enableHeartbeat = Settings::defaultValue(Settings::Server::EnableHeartbeat).toBool();
  m_heartbeatRate = Settings::defaultValue(Settings::Server::Heartbeat).toInt();
  m_relativeMouseMoves = Settings::defaultValue(Settings::Server::RelativeMouseMoves).toBool();
  m_win32keepForeground = Settings::defaultValue(Settings::Server::Win32KeepForeground).toBool();
  m_enableSwitchDelay = Settings::defaultValue(Settings::Server::EnableSwitchDelay).toBool();
  m_switchDelay = Settings::defaultValue(Settings::Server::SwitchDelay).toInt();
  m_enableSwitchDoubleTap = Settings::defaultValue(Settings::Server::EnableSwitchDoubleTap).toBool();
  m_switchDoubleTap = Settings::defaultValue(Settings::Server::SwitchDoubleTap).toInt();
  m_defaultLockToComputerState = Settings::defaultValue(Settings::Server::DefaultLockToComputerState).toBool();
  m_disableLockToComputer = Settings::defaultValue(Settings::Server::DisableLockToComputer).toBool();
  m_enableClipboard = Settings::defaultValue(Settings::Server::EnableClipboard).toBool();
  m_clipboardSize = Settings::defaultValue(Settings::Server::ClipboardSize).toUInt();

  ui->groupExternalConfig->setChecked(Settings::defaultValue(Settings::Server::ExternalConfig).toBool());
  ui->lineConfigFile->setText(Settings::defaultValue(Settings::Server::ExternalConfigFile).toString());

  refreshControls();
  updateControls();
}

void ServerConfigDialog::setServerConfig()
{
  setButtonBoxEnabledButtons();
}

bool ServerConfigDialog::addComputer(const QString &clientName, bool doSilent)
{
  if (!Settings::isWritable() || ui->groupExternalConfig->isChecked() || model().isFull())
    return false;
  bool isAccepted = false;
  Computer newComputer(clientName);

  if (ComputerSettingsDialog dlg(this, &newComputer, &model().m_computers);
      doSilent || dlg.exec() == QDialog::Accepted) {
    model().addComputer(newComputer);
    isAccepted = true;
  }

  updateComputerControls();
  return isAccepted;
}

bool ServerConfigDialog::isGeneralConfigModified() const
{
  return m_originalServerConfigIsExternal != ui->groupExternalConfig->isChecked() ||
         m_originalServerConfigUsesExternalFile != ui->lineConfigFile->text() ||
         m_protocol != Settings::networkProtocol() ||
         m_enableClipboard != Settings::value(Settings::Server::EnableClipboard).toBool() ||
         m_clipboardSize != Settings::value(Settings::Server::ClipboardSize).toUInt() ||
         m_enableHeartbeat != Settings::value(Settings::Server::EnableHeartbeat).toBool() ||
         m_heartbeatRate != Settings::value(Settings::Server::Heartbeat).toInt() ||
         m_enableSwitchDelay != Settings::value(Settings::Server::EnableSwitchDelay).toBool() ||
         m_switchDelay != Settings::value(Settings::Server::SwitchDelay).toInt() ||
         m_enableSwitchDoubleTap != Settings::value(Settings::Server::EnableSwitchDoubleTap).toBool() ||
         m_switchDoubleTap != Settings::value(Settings::Server::SwitchDoubleTap).toInt() ||
         m_relativeMouseMoves != Settings::value(Settings::Server::RelativeMouseMoves).toBool() ||
         m_win32keepForeground != Settings::value(Settings::Server::Win32KeepForeground).toBool() ||
         m_disableLockToComputer != Settings::value(Settings::Server::DisableLockToComputer).toBool() ||
         m_defaultLockToComputerState != Settings::value(Settings::Server::DefaultLockToComputerState).toBool();
}

bool ServerConfigDialog::isGeneralConfigDefault() const
{
  return ui->groupExternalConfig->isChecked() == Settings::defaultValue(Settings::Server::ExternalConfig).toBool() &&
         ui->lineConfigFile->text() == Settings::defaultValue(Settings::Server::ExternalConfigFile).toString() &&
         m_protocol == networkProtocolFromString(Settings::defaultValue(Settings::Server::Protocol).toString()) &&
         m_enableClipboard == Settings::defaultValue(Settings::Server::EnableClipboard).toBool() &&
         m_clipboardSize == Settings::defaultValue(Settings::Server::ClipboardSize).toUInt() &&
         m_enableHeartbeat == Settings::defaultValue(Settings::Server::EnableHeartbeat).toBool() &&
         m_heartbeatRate == Settings::defaultValue(Settings::Server::Heartbeat).toInt() &&
         m_enableSwitchDelay == Settings::defaultValue(Settings::Server::EnableSwitchDelay).toBool() &&
         m_switchDelay == Settings::defaultValue(Settings::Server::SwitchDelay).toInt() &&
         m_enableSwitchDoubleTap == Settings::defaultValue(Settings::Server::EnableSwitchDoubleTap).toBool() &&
         m_switchDoubleTap == Settings::defaultValue(Settings::Server::SwitchDoubleTap).toInt() &&
         m_relativeMouseMoves == Settings::defaultValue(Settings::Server::RelativeMouseMoves).toBool() &&
         m_win32keepForeground == Settings::defaultValue(Settings::Server::Win32KeepForeground).toBool() &&
         m_disableLockToComputer == Settings::defaultValue(Settings::Server::DisableLockToComputer).toBool() &&
         m_defaultLockToComputerState == Settings::defaultValue(Settings::Server::DefaultLockToComputerState).toBool();
}

void ServerConfigDialog::setButtonBoxEnabledButtons() const
{
  const bool writable = Settings::isWritable();
  const bool modified = isGeneralConfigModified() || !(m_originalServerConfig == m_serverConfig);
  const bool validFile = !ui->groupExternalConfig->isChecked() || QFileInfo(ui->lineConfigFile->text()).isFile();
  m_buttonBox->enableSave(writable && modified && validFile);
  m_buttonBox->enableReset(writable && modified);
  m_buttonBox->enableRestoreDefaults(writable && !isGeneralConfigDefault() && ui->tabWidget->currentIndex() >= 2);
  updateComputerControls();
}

void ServerConfigDialog::updateComputerControls() const
{
  const bool editable = Settings::isWritable() && !ui->groupExternalConfig->isChecked();
  const auto index = ui->computerSetupView->currentIndex();
  const bool selected = editable && index.isValid() && !m_computerSetupModel.computer(index).isNull();
  ui->btnAddComputer->setEnabled(editable && !m_computerSetupModel.isFull());
  ui->lblNewComputer->setEnabled(editable && !m_computerSetupModel.isFull());
  ui->lblRemoveComputer->setEnabled(editable);
  ui->btnEditComputer->setEnabled(selected);
  ui->btnRemoveComputer->setEnabled(selected && !m_computerSetupModel.computer(index).isServer());
}

void ServerConfigDialog::editComputer()
{
  const auto index = ui->computerSetupView->currentIndex();
  if (!Settings::isWritable() || ui->groupExternalConfig->isChecked() || !index.isValid() ||
      model().computer(index).isNull())
    return;
  ComputerSettingsDialog dialog(this, &model().computer(index), &model().m_computers);
  if (dialog.exec() == QDialog::Accepted) {
    ui->computerSetupView->viewport()->update();
    setButtonBoxEnabledButtons();
  }
}

void ServerConfigDialog::removeComputer()
{
  const auto index = ui->computerSetupView->currentIndex();
  if (!Settings::isWritable() || ui->groupExternalConfig->isChecked() || !index.isValid() ||
      model().computer(index).isNull() || model().computer(index).isServer())
    return;
  model().computer(index) = Computer();
  ui->computerSetupView->reset();
  ui->computerSetupView->viewport()->update();
  onComputerRemoved();
}

void ServerConfigDialog::exportConfig()
{
  const QString fileName = QFileDialog::getSaveFileName(
      this, tr("导出设备布局与控制热键草稿"), QStringLiteral("deskbridge-server.conf"),
      tr("共享配置文件 (*.conf);;所有文件 (*.*)")
  );
  if (fileName.isEmpty())
    return;
  if (!serverConfig().save(fileName)) {
    QMessageBox::warning(this, tr("无法导出配置"), tr("无法写入所选文件，请检查文件夹权限后重试。"));
  }
}
