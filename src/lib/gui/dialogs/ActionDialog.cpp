/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2008 Volker Lanz <vl@fidra.de>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ActionDialog.h"
#include "EditorViews.h"

#include "common/Action.h"
#include "common/Hotkey.h"
#include "common/KeySequence.h"
#include "config/ServerConfig.h"

ActionDialog::ActionDialog(QWidget *parent, const ServerConfig &config, Hotkey &hotkey, Action &action)
    : QDialog(parent, Qt::WindowTitleHint | Qt::WindowSystemMenuHint),
      ui{std::make_unique<Ui::ActionDialog>()},
      m_hotkey(hotkey),
      m_action(action)
{
  ui->setupUi(this);
  connect(ui->keySequenceWidget, &KeySequenceWidget::keySequenceChanged, this, &ActionDialog::keySequenceChanged);
  connect(
      ui->comboActionType, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ActionDialog::actionTypeChanged
  );
  connect(ui->listComputers, &QListWidget::itemChanged, this, &ActionDialog::itemToggled);
  connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &ActionDialog::accept);
  connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &ActionDialog::reject);

  ui->keySequenceWidget->setKeySequence(m_action.keySequence());

  ui->comboSwitchInDirection->setCurrentIndex(m_action.switchDirection());
  ui->comboLockCursorToComputer->setCurrentIndex(m_action.lockCursorMode());

  const int savedType = m_action.type();
  const int mouseDown = static_cast<int>(Action::Type::mouseDown);
  const int mouseButton = static_cast<int>(Action::Type::mousebutton);
  const int visibleType = savedType >= mouseDown && savedType <= mouseButton ? savedType - mouseDown : savedType;
  const bool existingHold =
      visibleType == ActionTypes::ToggleKey && !m_action.tapAtTrigger() && !m_action.keySequence().toString().isEmpty();
  ui->comboActionType->setCurrentIndex(existingHold ? ActionTypes::HoldWithHotkey : visibleType);
  ui->comboTriggerOn->setCurrentIndex(m_action.activeOnRelease());

  for (const Computer &computer : config.computers()) {
    if (computer.isNull())
      continue;
    auto *newListItem = new QListWidgetItem(computer.name());
    newListItem->setCheckState(Qt::Checked);
    if ((m_action.typeComputerNames().indexOf(computer.name()) == -1) &&
        (m_action.haveComputers() && !m_action.typeComputerNames().isEmpty()))
      newListItem->setCheckState(Qt::Unchecked);

    ui->listComputers->addItem(newListItem);

    ui->comboSwitchToComputer->addItem(computer.name(), computer.name());
    if (computer.name() == m_action.switchComputerName())
      ui->comboSwitchToComputer->setCurrentIndex(ui->comboSwitchToComputer->count() - 1);
  }

  ui->keySequenceWidget->setVisible(false);
  ui->groupComputers->setVisible(false);
  ui->listComputers->setEnabled(!ui->keySequenceWidget->keySequence().isMouseButton());
  ui->comboSwitchToComputer->setVisible(false);
  ui->comboSwitchInDirection->setVisible(false);
  ui->comboLockCursorToComputer->setVisible(false);

  actionTypeChanged(ui->comboActionType->currentIndex());
}

void ActionDialog::accept()
{
  if (!canSave())
    return;

  m_action.setKeySequence(ui->keySequenceWidget->keySequence());
  const int selectedType = ui->comboActionType->currentIndex();
  const int outputType = selectedType == ActionTypes::HoldWithHotkey ? ActionTypes::ToggleKey : selectedType;
  m_action.setType(
      isKeyAction(selectedType) && ui->keySequenceWidget->keySequence().isMouseButton()
          ? static_cast<int>(Action::Type::mouseDown) + outputType
          : outputType
  );
  m_action.setTapAtTrigger(selectedType == ActionTypes::ToggleKey);

  m_action.clearComputers();

  int computerCount = ui->listComputers->count();

  for (int i = 0; i < ui->listComputers->count(); i++) {
    const auto &item = ui->listComputers->item(i);
    m_action.addComputer(item->text());
    if (item->checkState() == Qt::Unchecked) {
      computerCount--;
      m_action.removeComputer(item->text());
    }
  }

  if (computerCount == ui->listComputers->count())
    m_action.clearComputers();

  m_action.setHaveComputers(computerCount);

  m_action.setSwitchComputerName(ui->comboSwitchToComputer->currentData().toString());
  m_action.setSwitchDirection(ui->comboSwitchInDirection->currentIndex());
  m_action.setLockCursorMode(ui->comboLockCursorToComputer->currentIndex());
  m_action.setActiveOnRelease(selectedType != ActionTypes::HoldWithHotkey && ui->comboTriggerOn->currentIndex());
  m_action.setRestartServer(ui->comboActionType->currentIndex() == ActionTypes::RestartServer);

  QDialog::accept();
}

void ActionDialog::updateSize()
{
  // The scrollable editor keeps its footprint when switching between action
  // types. Only the relevant controls change, without jumping or clipping.
  layout()->activate();
}

void ActionDialog::keySequenceChanged()
{
  ui->listComputers->setEnabled(!ui->keySequenceWidget->keySequence().isMouseButton());
  ui->groupComputers->setVisible(
      isKeyAction(ui->comboActionType->currentIndex()) && !ui->keySequenceWidget->keySequence().isMouseButton()
  );
  ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(canSave());
}

void ActionDialog::itemToggled() const
{
  ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(canSave());
}

void ActionDialog::actionTypeChanged(int index)
{
  const bool hold = index == ActionTypes::HoldWithHotkey;
  ui->comboTriggerOn->setEnabled(!hold);
  if (hold)
    ui->comboTriggerOn->setCurrentIndex(0);
  ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(canSave());
  ui->keySequenceWidget->setVisible(isKeyAction(index));
  ui->groupComputers->setVisible(isKeyAction(index) && !ui->keySequenceWidget->keySequence().isMouseButton());
  ui->listComputers->setEnabled(!ui->keySequenceWidget->keySequence().isMouseButton());
  ui->comboSwitchToComputer->setVisible(index == ActionTypes::SwitchTo);
  ui->comboSwitchInDirection->setVisible(index == ActionTypes::SwitchInDirection);
  ui->comboLockCursorToComputer->setVisible(index == ActionTypes::ModifyCursorLock);
  ui->mouseTargetHelp->setVisible(isKeyAction(index));
  const QStringList descriptions = {
      tr("发送按下事件，保持按键按住。可搭配“释放按键”动作完成松开。"),
      tr("发送释放事件，松开指定按键或鼠标按钮。"),
      tr("在选定的触发时机依次发送按下、释放两个事件，完成一次快捷键或点击。"),
      tr("将指针和键盘控制切换到选中的设备。"),
      tr("按照设备布局，将控制切换到指定方向的相邻设备。"),
      tr("按照设备顺序，将控制切换到下一台设备。"),
      tr("锁定后，指针停留在当前设备。解除锁定后可再次跨屏移动。"),
      tr("重新启动共享服务。设备会暂时断开，随后尝试重新连接。"),
      tr("按下热键时保持输出按键或鼠标按钮，松开热键时释放。适合持续按键和鼠标拖拽，并兼容已有规则。")
  };
  ui->actionDescription->setText(descriptions.value(index));
  ui->parameterLabel->setVisible(
      isKeyAction(index) || index == ActionTypes::SwitchTo || index == ActionTypes::SwitchInDirection ||
      index == ActionTypes::ModifyCursorLock
  );
  ui->parameterLabel->setText(
      isKeyAction(index)                        ? tr("输出按键")
      : index == ActionTypes::SwitchTo          ? tr("目标设备")
      : index == ActionTypes::SwitchInDirection ? tr("切换方向")
                                                : tr("锁定方式")
  );
  updateSize();
}

bool ActionDialog::isKeyAction(int index) const
{
  return (
      (index == ActionTypes::PressKey) || (index == ActionTypes::ReleaseKey) || (index == ActionTypes::ToggleKey) ||
      (index == ActionTypes::HoldWithHotkey)
  );
}

bool ActionDialog::canSave() const
{
  if (ui->comboActionType->currentIndex() < 0)
    return false;
  if (isKeyAction(ui->comboActionType->currentIndex())) {
    if (!ui->keySequenceWidget->valid() || ui->keySequenceWidget->keySequence().toString().isEmpty())
      return false;
    if (ui->keySequenceWidget->keySequence().isMouseButton())
      return true;
    const QList<QListWidgetItem *> items = ui->listComputers->findItems("*", Qt::MatchWildcard);
    int totalChecked = 0;
    for (const auto &item : items) {
      if (item->checkState() == Qt::Checked)
        totalChecked++;
    }
    return totalChecked > 0;
  }
  if (ui->comboActionType->currentIndex() == ActionTypes::SwitchTo)
    return ui->comboSwitchToComputer->currentIndex() >= 0;
  return true;
}

ActionDialog::~ActionDialog() = default;
