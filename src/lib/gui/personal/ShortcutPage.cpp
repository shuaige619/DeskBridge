/*
 * SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "ShortcutPage.h"
#include "ApplicationPicker.h"
#include "RuleEditor.h"
#include "ToggleSwitch.h"
#include "PersonalStyle.h"

#include "common/Settings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

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

QFrame *card(QWidget *parent)
{
  auto *result = new QFrame(parent);
  result->setProperty("personalCard", true);
  return result;
}

void clearLayout(QLayout *layout)
{
  while (auto *item = layout->takeAt(0)) {
    if (item->layout()) clearLayout(item->layout());
    if (auto *widget = item->widget()) {
      widget->hide();
      widget->setParent(nullptr);
      widget->deleteLater();
    }
    delete item;
  }
}
} // namespace

ShortcutPage::ShortcutPage(QWidget *parent) : QWidget(parent)
{
  setObjectName(QStringLiteral("shortcutPage"));
  setAccessibleName(tr("快捷键映射"));
  auto *outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(0);
  auto *heading = new QWidget(this);
  auto *headingLayout = new QHBoxLayout(heading);
  headingLayout->setContentsMargins(30, 24, 30, 18);
  auto *title = label(tr("快捷键映射"), heading, "pageTitle");
  title->setObjectName(QStringLiteral("shortcutPageInternalTitle"));
  headingLayout->addWidget(title);
  headingLayout->addStretch();
  m_addRule = new QPushButton(tr("＋ 添加规则"), heading);
  m_addRule->setObjectName(QStringLiteral("addShortcutRule"));
  m_addRule->setAccessibleName(tr("添加映射规则"));
  m_addRule->setProperty("primaryButton", true);
  headingLayout->addWidget(m_addRule);
  outer->addWidget(heading);

  auto *scroll = new QScrollArea(this);
  scroll->setObjectName(QStringLiteral("shortcutScrollArea"));
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  auto *content = new QWidget(scroll);
  content->setProperty("personalPageBody", true);
  auto *body = new QVBoxLayout(content);
  body->setContentsMargins(30, 6, 30, 24);
  body->setSpacing(16);
  m_readOnly = label(tr("设置文件为只读，当前可以查看规则，但无法应用更改。"), content, "secondaryText");
  m_readOnly->setObjectName(QStringLiteral("shortcutReadOnlyNotice"));
  body->addWidget(m_readOnly);
  m_error = label({}, content, "dangerText");
  m_error->setObjectName(QStringLiteral("shortcutConfigurationError"));
  body->addWidget(m_error);
  m_roleNotice = label(tr("此电脑作为键鼠来源，只共享物理输入；快捷键适配由接收端负责，本机物理按键不会转换。"), content, "secondaryText");
  m_roleNotice->setObjectName(QStringLiteral("shortcutServerRoleNotice"));
  body->addWidget(m_roleNotice);
  m_modifierNotice = label(tr("服务端已有全局修饰键替换。完整适配、轻按 Shift 切换及预设复制/粘贴已暂停；请先在服务端恢复原样，或继续使用自定义规则。"), content, "dangerText");
  m_modifierNotice->setObjectName(QStringLiteral("serverModifierMappingNotice"));
  body->addWidget(m_modifierNotice);

  auto *habitCard = card(content);
  auto *habitLayout = new QVBoxLayout(habitCard);
  habitLayout->setContentsMargins(20, 16, 20, 16);
  auto *habitHeading = new QHBoxLayout;
  habitHeading->addWidget(label(tr("一键快捷键方案"), habitCard, "sectionTitle"));
  habitHeading->addStretch();
  m_habitState = label({}, habitCard, "secondaryText");
  m_habitState->setObjectName(QStringLiteral("shortcutHabitState"));
  habitHeading->addWidget(m_habitState);
  habitLayout->addLayout(habitHeading);
  auto *habitButtons = new QHBoxLayout;
  const QStringList habitNames{tr("自动"), tr("Mac 习惯"), tr("Windows / Linux 习惯")};
  const QStringList habitObjects{QStringLiteral("habitAutomatic"), QStringLiteral("habitMac"), QStringLiteral("habitPC")};
  const QList<shortcuts::Habit> habits{shortcuts::Habit::Automatic, shortcuts::Habit::Mac, shortcuts::Habit::PC};
  for (int i = 0; i < habits.size(); ++i) {
    auto *button = new QPushButton(habitNames[i], habitCard);
    button->setObjectName(habitObjects[i]);
    button->setCheckable(true);
    button->setProperty("habit", static_cast<int>(habits[i]));
    button->setAccessibleName(tr("应用%1快捷键方案").arg(habitNames[i]));
    habitButtons->addWidget(button);
    m_habitButtons.append(button);
    // macOS accessibility changes checkable buttons through setChecked(),
    // without emitting clicked(). Keep the draft and visual selection together.
    connect(button, &QPushButton::toggled, this, [this, habit = habits[i]](bool checked) {
      if (checked) applyHabit(habit);
      // A selected scheme cannot be deselected; rejected or disabled changes
      // must also restore the selection from the actual draft.
      refresh();
    });
  }
  habitLayout->addLayout(habitButtons);
  m_habitDescription = label({}, habitCard, "secondaryText");
  m_habitDescription->setObjectName(QStringLiteral("shortcutHabitDescription"));
  habitLayout->addWidget(m_habitDescription);
  habitLayout->addWidget(label(tr("点按方案会立即应用，也会一并提交本页尚未应用的草稿；保留应用范围和其他自定义规则。"), habitCard, "secondaryText"));
  auto *sourceRow = new QHBoxLayout;
  sourceRow->addWidget(label(tr("旧版服务端的来源系统"), habitCard));
  m_sourceOS = new QComboBox(habitCard);
  m_sourceOS->setObjectName(QStringLiteral("sourceOperatingSystem"));
  m_sourceOS->setAccessibleName(tr("键鼠来源系统备用选择"));
  m_sourceOS->addItem(tr("尚未选择"), static_cast<int>(shortcuts::DeviceOS::Unknown));
  for (const auto os : {shortcuts::DeviceOS::Windows, shortcuts::DeviceOS::MacOS, shortcuts::DeviceOS::Linux})
    m_sourceOS->addItem(displayOperatingSystem(os), static_cast<int>(os));
  sourceRow->addWidget(m_sourceOS, 1);
  habitLayout->addLayout(sourceRow);
  body->addWidget(habitCard);

  auto *masterCard = card(content);
  auto *masterLayout = new QVBoxLayout(masterCard);
  masterLayout->setContentsMargins(20, 18, 20, 18);
  masterLayout->setSpacing(10);
  m_master = new ToggleSwitch(tr("启用快捷键映射"), masterCard);
  m_master->setObjectName(QStringLiteral("shortcutMasterSwitch"));
  m_master->setAccessibleName(tr("启用快捷键映射，不影响键鼠共享"));
  masterLayout->addWidget(m_master);
  masterLayout->addWidget(label(tr("仅处理另一台电脑传入的按键，本机物理键盘保持原样。"), masterCard, "secondaryText"));
  auto *scope = label(tr("远程输入 → %1").arg(displayOperatingSystem(shortcuts::localOS())), masterCard, "keycap");
  scope->setObjectName(QStringLiteral("shortcutScope"));
  scope->setWordWrap(false);
  scope->setMinimumHeight(scope->fontMetrics().height() + 16);
  scope->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
  masterLayout->addWidget(scope, 0, Qt::AlignLeft);
  m_offHint = label(tr("快捷键映射已关闭。键鼠共享仍正常工作，按键按原样传递。"), masterCard, "secondaryText");
  m_offHint->setObjectName(QStringLiteral("shortcutDisabledNotice"));
  masterLayout->addWidget(m_offHint);
  body->addWidget(masterCard);

  auto *inputMethodCard = card(content);
  inputMethodCard->setObjectName(QStringLiteral("shiftTapInputMethodCard"));
  auto *inputMethodLayout = new QVBoxLayout(inputMethodCard);
  inputMethodLayout->setContentsMargins(20, 18, 20, 18);
  inputMethodLayout->setSpacing(10);
  inputMethodLayout->addWidget(label(tr("输入法切换"), inputMethodCard, "sectionTitle"));
  m_shiftTapInputMethod = new ToggleSwitch(tr("轻按 Shift 切换中英文"), inputMethodCard);
  m_shiftTapInputMethod->setObjectName(QStringLiteral("shiftTapInputMethodSwitch"));
  m_shiftTapInputMethod->setAccessibleName(tr("轻按Shift切换中英文"));
  inputMethodLayout->addWidget(m_shiftTapInputMethod);
  inputMethodLayout->addWidget(label(tr("在 Mac 接收端，单独轻按远程左或右 Shift，并在 300 毫秒内松开时，发送 Ctrl+空格。组合键和长按不触发，Ctrl+空格保持原样；本机物理键盘不变。"), inputMethodCard, "secondaryText"));
  m_shiftTapInputMethodHint = label({}, inputMethodCard, "secondaryText");
  m_shiftTapInputMethodHint->setObjectName(QStringLiteral("shiftTapInputMethodHint"));
  inputMethodLayout->addWidget(m_shiftTapInputMethodHint);
  inputMethodLayout->addWidget(label(tr("此偏好独立于快捷键习惯及应用范围，需点按“应用更改”后生效。不会检测或修改系统输入法设置；Mac 需已将 Ctrl+空格设为输入法切换，Ubuntu 沿用原生的轻按 Shift。"), inputMethodCard, "secondaryText"));
  body->addWidget(inputMethodCard);
  auto *compatibility = new QPushButton(tr("远程键盘布局兼容选项…"), content);
  compatibility->setObjectName(QStringLiteral("keyboardCompatibilityOptions"));
  connect(compatibility, &QPushButton::clicked, this, &ShortcutPage::keyboardCompatibilityRequested);
  body->addWidget(compatibility, 0, Qt::AlignLeft);

  auto *fullCard = card(content);
  fullCard->setObjectName(QStringLiteral("fullShortcutCard"));
  auto *fullLayout = new QVBoxLayout(fullCard);
  fullLayout->setContentsMargins(20, 18, 20, 18);
  fullLayout->setSpacing(10);
  m_fullShortcuts = new ToggleSwitch(tr("完整快捷键适配"), fullCard);
  m_fullShortcuts->setObjectName(QStringLiteral("fullShortcutSwitch"));
  m_fullShortcuts->setAccessibleName(tr("完整快捷键适配"));
  fullLayout->addWidget(m_fullShortcuts);
  fullLayout->addWidget(label(tr("按上方习惯方案适配组合键，例如在 Mac 上将远程 Ctrl+A、Ctrl+Z、Ctrl+S、Ctrl+F 及 Ctrl+Shift 组合转为 Command 组合。Ctrl+空格保持原样，本机物理键盘不变。"), fullCard, "secondaryText"));
  fullLayout->addWidget(label(tr("仅适配快捷键；单独按下 Ctrl/Command 和 Ctrl+鼠标点击不变。单条规则及其停用设置优先，仍受各自应用范围限制。"), fullCard, "secondaryText"));
  m_allApplications = new QCheckBox(tr("用于全部应用"), fullCard);
  m_allApplications->setObjectName(QStringLiteral("fullShortcutAllApplications"));
  m_allApplications->setAccessibleName(tr("完整快捷键适配用于全部应用"));
  fullLayout->addWidget(m_allApplications);
  m_fullShortcutHint = label({}, fullCard, "secondaryText");
  m_fullShortcutHint->setObjectName(QStringLiteral("fullShortcutHint"));
  fullLayout->addWidget(m_fullShortcutHint);
  m_fullTerminalWarning = label(tr("远程 Ctrl 转为 Command 时，终端 Ctrl+C 也会作为复制，原有 Ctrl 语义可能变化。编辑复制/粘贴预设会切换为自定义，并关闭完整适配。"), fullCard, "dangerText");
  m_fullTerminalWarning->setObjectName(QStringLiteral("fullShortcutTerminalNotice"));
  fullLayout->addWidget(m_fullTerminalWarning);
  fullLayout->addWidget(label(tr("“用于全部应用”仅扩大完整适配的范围；下方规则仍按各自的生效应用处理。关闭后，完整适配只用于下方选中的应用。更改需点按“应用更改”，或选择一键方案。"), fullCard, "secondaryText"));
  body->addWidget(fullCard);

  body->addWidget(label(tr("应用范围"), content, "sectionTitle"));
  auto *appsCard = card(content);
  auto *appsCardLayout = new QVBoxLayout(appsCard);
  appsCardLayout->setContentsMargins(20, 16, 20, 16);
  auto *appHeading = new QHBoxLayout;
  appHeading->addWidget(label(tr("生效应用"), appsCard));
  appHeading->addStretch();
  m_addApp = new QPushButton(tr("＋ 添加应用…"), appsCard);
  m_addApp->setObjectName(QStringLiteral("addGlobalApplication"));
  m_addApp->setAccessibleName(tr("选择全局生效应用"));
  appHeading->addWidget(m_addApp);
  appsCardLayout->addLayout(appHeading);
  appsCardLayout->addWidget(label(tr("以下范围用于单条规则，以及未选择“用于全部应用”的完整适配。添加应用会默认用于现有全部规则。"), appsCard, "secondaryText"));
  m_apps = new QWidget(appsCard);
  m_apps->setObjectName(QStringLiteral("globalApplicationChips"));
  m_appsLayout = new QGridLayout(m_apps);
  m_appsLayout->setContentsMargins(0, 8, 0, 0);
  m_appsLayout->setSpacing(8);
  appsCardLayout->addWidget(m_apps);
  body->addWidget(appsCard);

  auto *rulesHeading = new QHBoxLayout;
  rulesHeading->addWidget(label(tr("映射规则"), content, "sectionTitle"));
  rulesHeading->addStretch();
  m_defaults = new QPushButton(tr("恢复默认规则…"), content);
  m_defaults->setObjectName(QStringLiteral("restoreDefaultShortcutRules"));
  rulesHeading->addWidget(m_defaults);
  body->addLayout(rulesHeading);
  m_rules = card(content);
  m_rules->setObjectName(QStringLiteral("shortcutRuleList"));
  m_rulesLayout = new QVBoxLayout(m_rules);
  m_rulesLayout->setContentsMargins(16, 4, 16, 4);
  m_rulesLayout->setSpacing(0);
  body->addWidget(m_rules);

  auto *reserved = card(content);
  reserved->setObjectName(QStringLiteral("reservedInputShortcut"));
  auto *reservedLayout = new QHBoxLayout(reserved);
  reservedLayout->setContentsMargins(20, 16, 20, 16);
  reservedLayout->addWidget(label(tr("中英文切换"), reserved));
  auto *reservedChord = label(tr("Ctrl + 空格"), reserved, "keycap");
  reservedChord->setMargin(6);
  reservedLayout->addWidget(reservedChord);
  reservedLayout->addStretch();
  reservedLayout->addWidget(label(tr("保持原样 · 保留快捷键"), reserved, "secondaryText"));
  body->addWidget(reserved);
  m_terminalWarning = label(
#ifdef Q_OS_MACOS
      tr("Codex 内置终端也会使用这些规则。需要 Ctrl+C 中断命令时，请将 Codex 从生效应用中移除。"), content
#else
      tr("规则按整个应用生效，内置终端不会单独区分。需要原始终端快捷键时，请将该应用从生效范围移除。"), content
#endif
  );
  m_terminalWarning->setObjectName(QStringLiteral("codexTerminalNotice"));
  body->addWidget(m_terminalWarning);
  body->addStretch();
  scroll->setWidget(content);
  outer->addWidget(scroll, 1);

  auto *footer = new QWidget(this);
  footer->setObjectName(QStringLiteral("shortcutApplyBar"));
  footer->setProperty("personalFooter", true);
  auto *footerLayout = new QHBoxLayout(footer);
  footerLayout->setContentsMargins(28, 12, 28, 14);
  m_status = label(tr("应用更改时会短暂重新连接。"), footer, "secondaryText");
  m_status->setObjectName(QStringLiteral("shortcutApplyStatus"));
  footerLayout->addWidget(m_status, 1);
  m_revert = new QPushButton(tr("还原"), footer);
  m_revert->setObjectName(QStringLiteral("revertShortcutChanges"));
  m_revert->setAccessibleName(tr("还原到上次应用的映射配置"));
  footerLayout->addWidget(m_revert);
  m_apply = new QPushButton(tr("应用更改"), footer);
  m_apply->setObjectName(QStringLiteral("applyShortcutChanges"));
  m_apply->setProperty("primaryButton", true);
  footerLayout->addWidget(m_apply);
  outer->addWidget(footer);

  connect(m_master, &QCheckBox::toggled, this, [this](bool enabled) {
    m_draft.enabled = enabled;
    draftChanged();
  });
  connect(m_shiftTapInputMethod, &QCheckBox::toggled, this, [this](bool enabled) {
    if (shortcuts::localOS() != shortcuts::DeviceOS::MacOS || m_applying || m_invalidLoad || !m_clientMode ||
        !m_draft.enabled || m_serverModifierMapping || !Settings::isWritable()) {
      refresh();
      return;
    }
    m_draft.shiftTapInputMethod = enabled;
    draftChanged();
  });
  connect(m_fullShortcuts, &QCheckBox::toggled, this, [this](bool enabled) {
    if (m_applying || m_invalidLoad || !m_clientMode || !m_draft.enabled || !Settings::isWritable()) {
      refresh();
      return;
    }
    m_draft.fullShortcuts = enabled;
    bool selectedHabit = false;
    if (enabled && m_draft.habit == shortcuts::Habit::Custom) {
      const auto source = shortcuts::effectiveSourceOS(m_draft, m_detectedSourceOS);
      const auto habit = source != shortcuts::DeviceOS::Unknown ? shortcuts::Habit::Automatic
          : (shortcuts::localOS() == shortcuts::DeviceOS::MacOS ? shortcuts::Habit::PC : shortcuts::Habit::Mac);
      shortcuts::setHabit(m_draft, habit);
      selectedHabit = true;
    }
    rebuildRules();
    draftChanged();
    if (selectedHabit)
      setStatusMessage(m_draft.habit == shortcuts::Habit::Automatic
          ? tr("已为完整适配选择自动方案；自定义规则已保留，点按“应用更改”后生效。")
          : tr("来源系统尚未确定，已选择跨系统的建议习惯；请核对上方方案，再点按“应用更改”。"));
  });
  connect(m_allApplications, &QCheckBox::toggled, this, [this](bool enabled) {
    if (m_applying || m_invalidLoad || !m_clientMode || !m_draft.enabled || !m_draft.fullShortcuts || !Settings::isWritable()) {
      refresh();
      return;
    }
    m_draft.allApplications = enabled;
    draftChanged();
  });
  connect(m_sourceOS, &QComboBox::currentIndexChanged, this, [this] {
    m_draft.sourceOS = static_cast<shortcuts::DeviceOS>(m_sourceOS->currentData().toInt());
    rebuildRules();
    draftChanged();
  });
  connect(m_addRule, &QPushButton::clicked, this, [this] { editRule(-1); });
  connect(m_defaults, &QPushButton::clicked, this, &ShortcutPage::restoreDefaults);
  connect(m_revert, &QPushButton::clicked, this, [this] {
    m_draft = m_saved;
    m_forceDirty = false;
    m_invalidLoad = !m_loadError.isEmpty();
    setStatusMessage(tr("已还原到上次应用的配置。"));
    rebuildApplications();
    rebuildRules();
    refresh();
  });
  connect(m_apply, &QPushButton::clicked, this, [this] {
    const auto error = shortcuts::validate(m_draft);
    if (!error.isEmpty()) {
      setStatusMessage(error);
      return;
    }
    if (isDirty() && !m_applying && m_clientMode && Settings::isWritable())
      Q_EMIT applyRequested(m_draft);
  });
  connect(m_addApp, &QPushButton::clicked, this, [this] {
    ApplicationPicker picker(m_draft.apps, this);
    if (picker.exec() != QDialog::Accepted)
      return;
    const auto selected = picker.selectedApplications();
    if (selected == m_draft.apps) {
      setStatusMessage(isDirty() ? tr("未新增应用；当前未应用的更改已保留。")
                                 : tr("未新增应用，现有配置保持不变。"));
      return;
    }
    for (const auto &id : selected) {
      if (!m_draft.apps.contains(id))
        for (auto &rule : m_draft.rules)
          if (!rule.apps.contains(id)) rule.apps.append(id);
    }
    m_draft.apps = selected;
    rebuildApplications();
    rebuildRules();
    draftChanged();
  });
  connect(Settings::instance(), &Settings::settingsWritableChanged, this, [this] { refresh(); });
  reload();
}

bool ShortcutPage::isDirty() const
{
  return m_forceDirty || m_draft != m_saved;
}

shortcuts::Configuration ShortcutPage::configuration() const
{
  return m_draft;
}

void ShortcutPage::setSourceOperatingSystem(shortcuts::DeviceOS os)
{
  if (m_detectedSourceOS == os) return;
  m_detectedSourceOS = os;
  rebuildRules();
  refresh();
}

void ShortcutPage::setClientMode(bool client)
{
  m_clientMode = client;
  refresh();
}

void ShortcutPage::setServerModifierMapping(bool enabled)
{
  if (m_serverModifierMapping == enabled) return;
  m_serverModifierMapping = enabled;
  rebuildRules();
  refresh();
}

void ShortcutPage::clearSourceOperatingSystemFallback()
{
  // Endpoint changes invalidate only the old peer hint, never unrelated drafts.
  m_saved.sourceOS = shortcuts::DeviceOS::Unknown;
  m_draft.sourceOS = shortcuts::DeviceOS::Unknown;
  m_detectedSourceOS = shortcuts::DeviceOS::Unknown;
  m_serverModifierMapping = false;
  rebuildRules();
  refresh();
}

void ShortcutPage::applyHabit(shortcuts::Habit habit)
{
  if (m_applying || m_invalidLoad || !m_clientMode || m_serverModifierMapping || !Settings::isWritable()) return;
  if (habit == shortcuts::Habit::Automatic &&
      shortcuts::effectiveSourceOS(m_draft, m_detectedSourceOS) == shortcuts::DeviceOS::Unknown) {
    setStatusMessage(tr("旧版服务端未报告系统。请先选择一次键鼠来源系统，再应用自动方案。"));
    refresh();
    return;
  }
  shortcuts::setHabit(m_draft, habit);
  rebuildRules();
  draftChanged();
  if (const auto error = shortcuts::validate(m_draft); !error.isEmpty()) {
    setStatusMessage(error);
    return;
  }
  if (isDirty()) Q_EMIT applyRequested(m_draft);
  else setStatusMessage(tr("此方案已经应用，无需重新连接。"));
}

void ShortcutPage::reload()
{
  m_loadError.clear();
  m_saved = shortcuts::load(&m_loadError);
  m_draft = m_saved;
  m_forceDirty = false;
  m_invalidLoad = !m_loadError.isEmpty();
  rebuildApplications();
  rebuildRules();
  setStatusMessage(tr("应用更改时会短暂重新连接。"));
  refresh();
}

void ShortcutPage::setApplying(bool applying)
{
  m_applying = applying;
  if (applying)
    setStatusMessage(tr("正在应用更改并恢复共享连接…"));
  refresh();
}

void ShortcutPage::setStatusMessage(const QString &message)
{
  m_status->setText(message);
}

void ShortcutPage::applied(bool success, const QString &message)
{
  m_applying = false;
  if (success)
    reload();
  else
    refresh();
  setStatusMessage(message.isEmpty() ? (success ? tr("更改已应用。") : tr("应用失败，草稿已保留。可重试或还原。")) : message);
}

void ShortcutPage::draftChanged()
{
  setStatusMessage(tr("有待应用的更改。应用时会短暂重新连接。"));
  refresh();
}

void ShortcutPage::refresh()
{
  const bool writable = Settings::isWritable();
  const bool editable = writable && !m_applying && !m_invalidLoad && m_clientMode;
  const QSignalBlocker blocker(m_master);
  m_master->setChecked(m_draft.enabled);
  m_master->setEnabled(editable);
  {
    const QSignalBlocker inputMethodBlocker(m_shiftTapInputMethod);
    m_shiftTapInputMethod->setChecked(m_draft.shiftTapInputMethod);
    m_shiftTapInputMethod->setEnabled(editable && m_draft.enabled && !m_serverModifierMapping &&
                                      shortcuts::localOS() == shortcuts::DeviceOS::MacOS);
  }
  QString inputMethodHint;
  if (shortcuts::localOS() != shortcuts::DeviceOS::MacOS)
    inputMethodHint = tr("本机为 %1，本版保留原生输入法切换，不在此额外转换。请在系统或输入法中管理轻按 Shift。").arg(displayOperatingSystem(shortcuts::localOS()));
  else if (!m_clientMode)
    inputMethodHint = tr("此电脑作为键鼠来源。请在 Mac 接收端启用轻按 Shift 转换，来源端仍按原生行为处理。");
  else if (m_serverModifierMapping)
    inputMethodHint = tr("轻按 Shift 转换已暂停：服务端正在全局替换修饰键，请先在服务端恢复原样。");
  else if (!writable)
    inputMethodHint = tr("设置文件为只读，可以查看此偏好，但无法更改或应用。");
  else if (m_applying)
    inputMethodHint = tr("正在应用更改，请稍候…");
  else if (!m_draft.enabled)
    inputMethodHint = tr("映射总开关已关闭，轻按 Shift 保持原样；此偏好已保留。");
  else
    inputMethodHint = m_draft.shiftTapInputMethod ? tr("应用后，仅将远程轻按 Shift 转为 Ctrl+空格；不会改变系统输入法配置。")
                                                : tr("当前保持远程 Shift 原样。开启后可在 Mac 接收端适配轻按切换。");
  m_shiftTapInputMethodHint->setText(inputMethodHint);
  {
    const QSignalBlocker fullBlocker(m_fullShortcuts);
    const QSignalBlocker appsBlocker(m_allApplications);
    m_fullShortcuts->setChecked(m_draft.fullShortcuts);
    m_fullShortcuts->setEnabled(editable && m_draft.enabled);
    m_allApplications->setChecked(m_draft.allApplications);
    m_allApplications->setEnabled(editable && m_draft.enabled && m_draft.fullShortcuts);
  }
  QString fullHint;
  if (!m_draft.fullShortcuts) {
    fullHint = tr("当前仅使用下方单条规则。首次开启时，有来源系统会选择自动方案；来源未知时会选择适合本机的跨系统习惯，自定义规则会保留。");
  } else if (m_serverModifierMapping) {
    fullHint = tr("完整适配已暂停：请先在服务端关闭全局修饰键替换。");
  } else if (const auto direction = shortcuts::shortcutModifierMapping(m_draft, m_detectedSourceOS)) {
    const auto source = shortcuts::effectiveSourceOS(m_draft, m_detectedSourceOS);
    fullHint = tr("%1 → %2；%3 → %4。%5")
        .arg(shortcuts::chordText({'a', direction->source}, source), shortcuts::chordText({'a', direction->target}),
             shortcuts::chordText({'z', KeyModifierMask(direction->source | KeyModifierShift)}, source),
             shortcuts::chordText({'z', KeyModifierMask(direction->target | KeyModifierShift)}),
             m_draft.allApplications ? tr("完整适配用于全部应用。")
                 : (m_draft.apps.isEmpty() ? tr("尚未选择应用；请添加应用，或勾选“用于全部应用”。") : tr("完整适配仅用于下方选中的应用。")));
  } else {
    fullHint = m_draft.habit == shortcuts::Habit::Automatic &&
               shortcuts::effectiveSourceOS(m_draft, m_detectedSourceOS) == shortcuts::DeviceOS::Unknown
        ? tr("来源系统尚未确定，完整适配暂时保持原样；请在上方选择来源系统或快捷键习惯。")
        : tr("当前习惯与本机一致，快捷键保持原样，无需完整转换。");
  }
  if (!m_draft.enabled && m_draft.fullShortcuts) fullHint.prepend(tr("映射总开关已关闭。"));
  m_fullShortcutHint->setText(fullHint);
  m_fullTerminalWarning->setVisible(m_draft.fullShortcuts);
  m_addApp->setEnabled(editable && m_draft.enabled);
  m_addRule->setEnabled(editable && m_draft.enabled);
  m_defaults->setEnabled(writable && !m_applying && m_clientMode);
  m_apps->setEnabled(editable && m_draft.enabled);
  m_rules->setEnabled(editable && m_draft.enabled);
  m_offHint->setVisible(!m_draft.enabled);
  m_terminalWarning->setVisible(!m_draft.fullShortcuts && (shortcuts::localOS() == shortcuts::DeviceOS::MacOS
      ? m_draft.apps.contains(QStringLiteral("com.openai.codex")) : !m_draft.apps.isEmpty()));
  m_readOnly->setVisible(!writable);
  m_roleNotice->setVisible(!m_clientMode);
  m_modifierNotice->setVisible(m_clientMode && m_serverModifierMapping);
  {
    const QSignalBlocker sourceBlocker(m_sourceOS);
    m_sourceOS->setCurrentIndex(m_sourceOS->findData(static_cast<int>(m_draft.sourceOS)));
  }
  m_sourceOS->setEnabled(editable);
  m_habitState->setText(m_draft.habit == shortcuts::Habit::Custom ? tr("当前：自定义") : tr("当前：已选方案"));
  m_habitDescription->setText(m_serverModifierMapping && m_draft.habit != shortcuts::Habit::Custom
      ? tr("服务端正在全局替换修饰键，完整适配与复制/粘贴预设暂停，其他自定义规则保持。")
      : shortcuts::habitDescription(m_draft, m_detectedSourceOS));
  for (auto *button : m_habitButtons) {
    const QSignalBlocker buttonBlocker(button);
    button->setChecked(button->property("habit").toInt() == static_cast<int>(m_draft.habit));
    button->setEnabled(editable && !m_serverModifierMapping);
  }
  const auto validation = m_invalidLoad ? m_loadError : shortcuts::validate(m_draft);
  m_error->setText(m_invalidLoad ? tr("无法载入规则：%1\n可以恢复默认规则后重新应用。").arg(validation) : validation);
  m_error->setVisible(!validation.isEmpty());
  m_apply->setEnabled(editable && isDirty() && validation.isEmpty());
  m_revert->setEnabled(!m_applying && isDirty());
  m_apply->setText(m_applying ? tr("正在应用…") : tr("应用更改"));
  if (m_lastDirty != isDirty()) {
    m_lastDirty = isDirty();
    Q_EMIT dirtyChanged(m_lastDirty);
  }
}

void ShortcutPage::rebuildApplications()
{
  clearLayout(m_appsLayout);
  int index = 0;
  for (const auto &id : m_draft.apps) {
    const auto app = ApplicationPicker::application(id);
    auto *chip = new QWidget(m_apps);
    chip->setObjectName(QStringLiteral("appChip_%1").arg(id));
    chip->setProperty("applicationChip", true);
    auto *layout = new QHBoxLayout(chip);
    layout->setContentsMargins(10, 6, 6, 6);
    layout->setSpacing(8);
    auto *icon = new QLabel(chip);
    icon->setPixmap(app.icon.pixmap(24, 24));
    layout->addWidget(icon);
    auto *name = label(app.name, chip);
    name->setWordWrap(false);
    layout->addWidget(name);
    auto *remove = new QToolButton(chip);
    remove->setProperty("applicationRemove", true);
    remove->setIcon(personalCloseIcon());
    remove->setIconSize(QSize(12, 12));
    remove->setFixedSize(24, 24);
    remove->setFocusPolicy(Qt::StrongFocus);
    remove->setCursor(Qt::PointingHandCursor);
    remove->setObjectName(QStringLiteral("removeApp_%1").arg(id));
    remove->setAccessibleName(tr("从生效应用中移除 %1").arg(app.name));
    remove->setToolTip(tr("移除 %1，保留其规则配置").arg(app.name));
    remove->setAutoRaise(true);
    layout->addWidget(remove);
    m_appsLayout->addWidget(chip, index / 3, index % 3, Qt::AlignLeft);
    ++index;
    connect(remove, &QToolButton::clicked, this, [this, id] {
      m_draft.apps.removeAll(id);
      // Rules keep their app selection; the global boundary stops it from matching.
      rebuildApplications();
      rebuildRules();
      draftChanged();
    });
  }
  if (index == 0)
    m_appsLayout->addWidget(label(tr("尚未选择应用；单条规则与仅用于所选应用的完整适配保持原样。"), m_apps, "secondaryText"), 0, 0);
}

void ShortcutPage::rebuildRules()
{
  clearLayout(m_rulesLayout);
  if (m_draft.rules.isEmpty()) {
    auto *empty = label(tr("尚无映射规则。添加一条规则，或恢复默认的复制、粘贴规则。"), m_rules, "secondaryText");
    empty->setMargin(16);
    m_rulesLayout->addWidget(empty);
    return;
  }
  auto *header = new QWidget(m_rules);
  auto *columns = new QGridLayout(header);
  columns->setContentsMargins(0, 10, 0, 10);
  const QStringList headings{tr("操作"), tr("远程键盘输入"), QString(), tr("本机收到"), tr("启用"), QString()};
  for (int i = 0; i < headings.size(); ++i)
    columns->addWidget(label(headings[i], header, "secondaryText"), 0, i);
  columns->setColumnStretch(0, 2);
  columns->setColumnStretch(1, 2);
  columns->setColumnStretch(3, 2);
  columns->setColumnMinimumWidth(4, 48);
  columns->setColumnMinimumWidth(5, 54);
  m_rulesLayout->addWidget(header);
  const auto effective = shortcuts::effectiveConfiguration(m_draft, m_detectedSourceOS);
  const auto sourceOS = shortcuts::effectiveSourceOS(m_draft, m_detectedSourceOS);
  for (int index = 0; index < m_draft.rules.size(); ++index) {
    const auto &rule = m_draft.rules[index];
    const auto active = std::find_if(effective.rules.cbegin(), effective.rules.cend(), [&rule](const auto &entry) {
      return entry.id == rule.id;
    });
    const bool managed = rule.id == QStringLiteral("copy") || rule.id == QStringLiteral("paste");
    const bool passthrough = m_draft.habit != shortcuts::Habit::Custom && managed &&
                             (active == effective.rules.cend() || m_serverModifierMapping);
    const auto shown = active == effective.rules.cend() ? rule : *active;
    auto *separator = new QFrame(m_rules);
    separator->setFrameShape(QFrame::HLine);
    m_rulesLayout->addWidget(separator);
    auto *row = new QWidget(m_rules);
    row->setObjectName(QStringLiteral("ruleRow_%1").arg(rule.id));
    auto *layout = new QGridLayout(row);
    layout->setContentsMargins(0, 14, 0, 14);
    layout->setHorizontalSpacing(12);
    auto *nameColumn = new QVBoxLayout;
    nameColumn->addWidget(label(rule.name, row));
    QStringList names;
    for (const auto &id : rule.apps)
      if (m_draft.apps.contains(id)) names.append(ApplicationPicker::application(id).name);
    auto *scope = label(names.isEmpty() ? tr("当前无生效应用") : names.join(tr("、")), row, "secondaryText");
    nameColumn->addWidget(scope);
    layout->addLayout(nameColumn, 0, 0);
    QString inputText = shortcuts::chordText(shown.input, sourceOS);
    if (passthrough) {
      const bool known = m_draft.habit != shortcuts::Habit::Automatic || sourceOS != shortcuts::DeviceOS::Unknown;
      const bool mac = m_draft.habit == shortcuts::Habit::Mac ||
                       (m_draft.habit == shortcuts::Habit::Automatic && sourceOS == shortcuts::DeviceOS::MacOS);
      inputText = known ? shortcuts::chordText({rule.id == QStringLiteral("copy") ? KeyID('c') : KeyID('v'),
                                               mac ? KeyModifierSuper : KeyModifierControl}, sourceOS)
                        : tr("来源尚未确定");
    }
    auto *input = label(inputText, row, "keycap");
    input->setObjectName(QStringLiteral("ruleInput_%1").arg(rule.id));
    input->setMargin(7);
    layout->addWidget(input, 0, 1, Qt::AlignLeft);
    layout->addWidget(label(QStringLiteral("→"), row, "secondaryText"), 0, 2);
    auto *output = label(passthrough ? tr("保持原样") : shortcuts::chordText(shown.output), row, "keycap");
    output->setObjectName(QStringLiteral("ruleOutput_%1").arg(rule.id));
    output->setMargin(7);
    layout->addWidget(output, 0, 3, Qt::AlignLeft);
    auto *enabled = new ToggleSwitch(row);
    enabled->setObjectName(QStringLiteral("enableRule_%1").arg(rule.id));
    enabled->setAccessibleName(tr("启用规则：%1").arg(rule.name));
    enabled->setChecked(rule.enabled);
    layout->addWidget(enabled, 0, 4);
    auto *edit = new QPushButton(tr("编辑"), row);
    edit->setObjectName(QStringLiteral("editRule_%1").arg(rule.id));
    edit->setAccessibleName(tr("编辑规则：%1").arg(rule.name));
    layout->addWidget(edit, 0, 5);
    layout->setColumnStretch(0, 2);
    layout->setColumnStretch(1, 2);
    layout->setColumnStretch(3, 2);
    layout->setColumnMinimumWidth(4, 48);
    layout->setColumnMinimumWidth(5, 54);
    m_rulesLayout->addWidget(row);
    connect(enabled, &QCheckBox::toggled, this, [this, index](bool checked) {
      m_draft.rules[index].enabled = checked;
      draftChanged();
    });
    connect(edit, &QPushButton::clicked, this, [this, index] { editRule(index); });
  }
}

void ShortcutPage::editRule(int index)
{
  auto editorConfig = m_draft;
  const bool managed = index >= 0 && (m_draft.rules[index].id == QStringLiteral("copy") ||
                                      m_draft.rules[index].id == QStringLiteral("paste"));
  if (managed && m_draft.habit != shortcuts::Habit::Custom) {
    const auto effective = shortcuts::effectiveConfiguration(m_draft, m_detectedSourceOS);
    for (auto &raw : editorConfig.rules) {
      const auto match = std::find_if(effective.rules.cbegin(), effective.rules.cend(), [&raw](const auto &entry) {
        return entry.id == raw.id;
      });
      if (match != effective.rules.cend()) raw = *match;
      else if (raw.id == QStringLiteral("copy") || raw.id == QStringLiteral("paste")) {
        const auto source = shortcuts::effectiveSourceOS(m_draft, m_detectedSourceOS);
        if (m_draft.habit != shortcuts::Habit::Automatic || source != shortcuts::DeviceOS::Unknown) {
          const bool mac = m_draft.habit == shortcuts::Habit::Mac ||
                           (m_draft.habit == shortcuts::Habit::Automatic && source == shortcuts::DeviceOS::MacOS);
          const KeyID key = raw.id == QStringLiteral("copy") ? 'c' : 'v';
          raw.input = {key, mac ? KeyModifierSuper : KeyModifierControl};
          raw.output = {key, shortcuts::localOS() == shortcuts::DeviceOS::MacOS ? KeyModifierSuper : KeyModifierControl};
        }
      }
    }
    editorConfig.habit = shortcuts::Habit::Custom;
    editorConfig.fullShortcuts = false;
  }
  const auto initial = index < 0 ? shortcuts::Rule{} : editorConfig.rules[index];
  RuleEditor editor(editorConfig, initial, window(), m_detectedSourceOS);
  if (editor.exec() != QDialog::Accepted)
    return;
  if (managed) {
    m_draft.rules = editorConfig.rules;
    m_draft.habit = shortcuts::Habit::Custom;
    m_draft.fullShortcuts = false;
  }
  if (editor.deletionRequested())
    m_draft.rules.removeAt(index);
  else {
    m_draft.apps = editor.globalApplications();
    if (index < 0) m_draft.rules.append(editor.rule());
    else m_draft.rules[index] = editor.rule();
  }
  rebuildApplications();
  rebuildRules();
  draftChanged();
}

void ShortcutPage::restoreDefaults()
{
  if (QMessageBox::question(this, tr("恢复默认规则"), tr("恢复本机默认的复制和粘贴规则？应用后才会生效。"),
                            QMessageBox::Cancel | QMessageBox::Yes, QMessageBox::Cancel) != QMessageBox::Yes)
    return;
  const bool shiftTapInputMethod = m_draft.shiftTapInputMethod;
  m_draft = shortcuts::defaults();
  m_draft.shiftTapInputMethod = shiftTapInputMethod;
  m_forceDirty = m_invalidLoad;
  m_invalidLoad = false;
  rebuildApplications();
  rebuildRules();
  draftChanged();
}

} // namespace deskflow::gui::personal
