/* SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception */
#pragma once

#include "personal/DialogStyle.h"
#include "widgets/KeySequenceWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>

// These editors use the same surfaces and spacing as the main window. The Ui
// structures keep the configuration logic independent of widget construction.
namespace deskflow::gui::editor {
inline QLabel *label(const QString &text, QWidget *parent, const char *property = nullptr)
{
  auto *value = new QLabel(text, parent);
  value->setTextFormat(Qt::PlainText);
  value->setWordWrap(true);
  if (property)
    value->setProperty(property, true);
  return value;
}

inline QVBoxLayout *card(QVBoxLayout *body, const QString &title, const QString &description = {})
{
  auto *surface = new QWidget(body->parentWidget());
  surface->setProperty("personalCard", true);
  auto *layout = new QVBoxLayout(surface);
  layout->setContentsMargins(18, 16, 18, 18);
  layout->setSpacing(12);
  if (!title.isEmpty())
    layout->addWidget(label(title, surface, "sectionTitle"));
  if (!description.isEmpty())
    layout->addWidget(label(description, surface, "secondaryText"));
  body->addWidget(surface);
  return layout;
}

inline QVBoxLayout *body(QDialog *dialog, QVBoxLayout *root)
{
  auto *content = new QWidget(dialog);
  auto *layout = new QVBoxLayout(content);
  layout->setContentsMargins(24, 0, 24, 24);
  layout->setSpacing(16);
  root->addWidget(personal::dialogScroll(content, dialog), 1);
  return layout;
}

inline QDialogButtonBox *footer(QDialog *dialog, QVBoxLayout *root, const QString &saveText)
{
  auto *footer = new QWidget(dialog);
  footer->setProperty("personalFooter", true);
  auto *layout = new QHBoxLayout(footer);
  layout->setContentsMargins(24, 14, 24, 16);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok, footer);
  buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
  buttons->button(QDialogButtonBox::Ok)->setText(saveText);
  buttons->button(QDialogButtonBox::Ok)->setProperty("primaryButton", true);
  layout->addWidget(buttons);
  root->addWidget(footer);
  return buttons;
}

inline QFormLayout *form(QVBoxLayout *layout)
{
  auto *result = new QFormLayout;
  result->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  result->setRowWrapPolicy(QFormLayout::WrapLongRows);
  result->setHorizontalSpacing(22);
  result->setVerticalSpacing(12);
  layout->addLayout(result);
  return result;
}
} // namespace deskflow::gui::editor

namespace Ui {
class ComputerSettingsDialog
{
public:
  QLineEdit *lineNameEdit;
  QLabel *lblNameError;
  QLineEdit *lineAddAlias;
  QLabel *lblAliasError;
  QListWidget *listAliases;
  QPushButton *btnAddAlias;
  QPushButton *btnRemoveAlias;
  QComboBox *comboShift;
  QComboBox *comboCtrl;
  QComboBox *comboAlt;
  QComboBox *comboMeta;
  QComboBox *comboSuper;
  QComboBox *comboAltGr;
  QCheckBox *chkDeadTopLeft;
  QCheckBox *chkDeadTopRight;
  QCheckBox *chkDeadBottomLeft;
  QCheckBox *chkDeadBottomRight;
  QSpinBox *sbSwitchCornerSize;
  QCheckBox *chkFixCapsLock;
  QCheckBox *chkFixNumLock;
  QCheckBox *chkFixScrollLock;
  QCheckBox *chkFixXTest;
  QCheckBox *chkWeakX11Focus;
  QDialogButtonBox *buttonBox;

  void setupUi(QDialog *dialog)
  {
    using namespace deskflow::gui;
    dialog->setObjectName(QStringLiteral("computerSettingsEditor"));
    dialog->setWindowTitle(QStringLiteral("设备设置"));
    auto *root = new QVBoxLayout(dialog);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(
        personal::dialogHeading(
            QStringLiteral("设备设置"), QStringLiteral("设置设备的识别名称、输入行为与兼容性。"), dialog
        )
    );
    auto *content = editor::body(dialog, root);

    auto *identity = editor::card(content, QStringLiteral("设备识别"));
    auto *nameForm = editor::form(identity);
    lineNameEdit = new QLineEdit(dialog);
    lineNameEdit->setObjectName(QStringLiteral("lineNameEdit"));
    lineNameEdit->setMaxLength(255);
    lineNameEdit->setPlaceholderText(QStringLiteral("与这台设备显示的屏幕名称一致"));
    nameForm->addRow(QStringLiteral("设备名称"), lineNameEdit);
    lblNameError = editor::label({}, dialog, "dangerText");
    identity->addWidget(lblNameError);
    identity->addWidget(
        editor::label(
            QStringLiteral("名称可使用字母、数字、下划线、短横线和句点，最长 255 个字符。"), dialog, "secondaryText"
        )
    );

    auto *aliases = editor::card(
        content, QStringLiteral("其他识别名称"),
        QStringLiteral("设备也可以使用以下别名连接。设备名称发生变化时，可在这里保留原名称。")
    );
    auto *aliasInput = new QHBoxLayout;
    lineAddAlias = new QLineEdit(dialog);
    lineAddAlias->setObjectName(QStringLiteral("lineAddAlias"));
    lineAddAlias->setMaxLength(255);
    lineAddAlias->setPlaceholderText(QStringLiteral("输入一个别名"));
    btnAddAlias = new QPushButton(QStringLiteral("添加"), dialog);
    btnAddAlias->setEnabled(false);
    btnAddAlias->setAutoDefault(false);
    aliasInput->addWidget(lineAddAlias, 1);
    aliasInput->addWidget(btnAddAlias);
    aliases->addLayout(aliasInput);
    lblAliasError = editor::label({}, dialog, "dangerText");
    aliases->addWidget(lblAliasError);
    listAliases = new QListWidget(dialog);
    listAliases->setObjectName(QStringLiteral("listAliases"));
    listAliases->setSelectionMode(QAbstractItemView::ExtendedSelection);
    listAliases->setMinimumHeight(82);
    listAliases->setMaximumHeight(126);
    listAliases->setAccessibleName(QStringLiteral("已添加的设备别名"));
    aliases->addWidget(listAliases);
    btnRemoveAlias = new QPushButton(QStringLiteral("移除所选别名"), dialog);
    btnRemoveAlias->setEnabled(false);
    btnRemoveAlias->setAutoDefault(false);
    btnRemoveAlias->setProperty("dangerButton", true);
    aliases->addWidget(btnRemoveAlias, 0, Qt::AlignLeft);

    auto *modifiers = editor::card(
        content, QStringLiteral("设备修饰键"),
        QStringLiteral("对发送到此设备的修饰键进行全局替换。按应用转换组合键，请使用侧栏的“快捷键映射”。")
    );
    auto *modifierForm = editor::form(modifiers);
    const QStringList keys = {QStringLiteral("Shift"),       QStringLiteral("Ctrl"),  QStringLiteral("Alt"),
                              QStringLiteral("Meta"),        QStringLiteral("Super"), QStringLiteral("Alt Gr"),
                              QStringLiteral("不发送修饰键")};
    auto addModifier = [&](QComboBox *&combo, const QString &name, int index) {
      combo = new QComboBox(dialog);
      combo->setObjectName(QStringLiteral("combo") + QString(name).remove(QLatin1Char(' ')));
      combo->addItems(keys);
      combo->setCurrentIndex(index);
      modifierForm->addRow(name + QStringLiteral(" →"), combo);
    };
    addModifier(comboShift, QStringLiteral("Shift"), 0);
    addModifier(comboCtrl, QStringLiteral("Ctrl"), 1);
    addModifier(comboAlt, QStringLiteral("Alt"), 2);
    addModifier(comboMeta, QStringLiteral("Meta"), 3);
    addModifier(comboSuper, QStringLiteral("Super"), 4);
    addModifier(comboAltGr, QStringLiteral("Alt Gr"), 5);
    modifiers->addWidget(
        editor::label(
            QStringLiteral("更改全局修饰键后，接收端的快捷键适配可能暂停，以避免同一按键被重复转换。"), dialog,
            "secondaryText"
        )
    );

    auto *corners = editor::card(
        content, QStringLiteral("角落保护"),
        QStringLiteral("指针位于选中的角落区域时，停留在当前设备，防止操作系统角落手势触发意外切屏。")
    );
    auto *cornerGrid = new QGridLayout;
    cornerGrid->setHorizontalSpacing(36);
    cornerGrid->setVerticalSpacing(12);
    chkDeadTopLeft = new QCheckBox(QStringLiteral("左上角"), dialog);
    chkDeadTopRight = new QCheckBox(QStringLiteral("右上角"), dialog);
    chkDeadBottomLeft = new QCheckBox(QStringLiteral("左下角"), dialog);
    chkDeadBottomRight = new QCheckBox(QStringLiteral("右下角"), dialog);
    chkDeadTopLeft->setObjectName(QStringLiteral("chkDeadTopLeft"));
    chkDeadTopRight->setObjectName(QStringLiteral("chkDeadTopRight"));
    chkDeadBottomLeft->setObjectName(QStringLiteral("chkDeadBottomLeft"));
    chkDeadBottomRight->setObjectName(QStringLiteral("chkDeadBottomRight"));
    cornerGrid->addWidget(chkDeadTopLeft, 0, 0);
    cornerGrid->addWidget(chkDeadTopRight, 0, 1);
    cornerGrid->addWidget(chkDeadBottomLeft, 1, 0);
    cornerGrid->addWidget(chkDeadBottomRight, 1, 1);
    cornerGrid->setColumnStretch(0, 1);
    cornerGrid->setColumnStretch(1, 1);
    corners->addLayout(cornerGrid);
    auto *cornerForm = editor::form(corners);
    sbSwitchCornerSize = new QSpinBox(dialog);
    sbSwitchCornerSize->setObjectName(QStringLiteral("sbSwitchCornerSize"));
    sbSwitchCornerSize->setRange(0, 9999);
    sbSwitchCornerSize->setSuffix(QStringLiteral(" 像素"));
    cornerForm->addRow(QStringLiteral("保护区域大小"), sbSwitchCornerSize);

    auto *compatibility = editor::card(
        content, QStringLiteral("输入兼容性"),
        QStringLiteral("仅在此设备出现锁定键状态或 Linux / X11 输入异常时启用对应修复。")
    );
    chkFixCapsLock = new QCheckBox(QStringLiteral("修复 Caps Lock 大写锁定状态"), dialog);
    chkFixNumLock = new QCheckBox(QStringLiteral("修复 Num Lock 数字锁定状态"), dialog);
    chkFixScrollLock = new QCheckBox(QStringLiteral("修复 Scroll Lock 滚动锁定状态"), dialog);
    chkFixXTest = new QCheckBox(QStringLiteral("X11：为 Xinerama 启用 XTest 修复"), dialog);
    chkWeakX11Focus = new QCheckBox(QStringLiteral("X11：使用弱焦点兼容模式"), dialog);
    chkFixCapsLock->setObjectName(QStringLiteral("chkFixCapsLock"));
    chkFixNumLock->setObjectName(QStringLiteral("chkFixNumLock"));
    chkFixScrollLock->setObjectName(QStringLiteral("chkFixScrollLock"));
    chkFixXTest->setObjectName(QStringLiteral("chkFixXTest"));
    chkWeakX11Focus->setObjectName(QStringLiteral("chkWeakX11Focus"));
    for (auto *check : {chkFixCapsLock, chkFixNumLock, chkFixScrollLock, chkFixXTest, chkWeakX11Focus})
      compatibility->addWidget(check);
    content->addStretch();
    buttonBox = editor::footer(dialog, root, QStringLiteral("保存设备"));
    dialog->setMinimumSize(500, 420);
    personal::prepareDialog(dialog, 650, 740);
  }
};

class HotkeyDialog
{
public:
  KeySequenceWidget *m_pKeySequenceWidgetHotkey;
  QDialogButtonBox *buttonBox;
  QLabel *recordingHelp;

  void setupUi(QDialog *dialog)
  {
    using namespace deskflow::gui;
    dialog->setObjectName(QStringLiteral("controlHotkeyEditor"));
    dialog->setWindowTitle(QStringLiteral("控制热键"));
    auto *root = new QVBoxLayout(dialog);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(
        personal::dialogHeading(
            QStringLiteral("设置控制热键"), QStringLiteral("在来源电脑上触发切屏、发送按键或共享控制动作。"), dialog
        )
    );
    auto *content = editor::body(dialog, root);
    auto *input = editor::card(content, QStringLiteral("触发按键"));
    m_pKeySequenceWidgetHotkey = new KeySequenceWidget(dialog);
    m_pKeySequenceWidgetHotkey->setObjectName(QStringLiteral("hotkeyRecorder"));
    input->addWidget(m_pKeySequenceWidgetHotkey);
    recordingHelp = editor::label(
        QStringLiteral("点击上方按钮，再按下一个组合键或鼠标按钮。点击其他位置可取消本次录入。"), dialog,
        "secondaryText"
    );
    input->addWidget(recordingHelp);
    content->addWidget(
        editor::label(
            QStringLiteral("这里配置来源电脑的控制动作。接收端按应用转换快捷键，请在“快捷键映射”中设置。"), dialog,
            "secondaryText"
        )
    );
    content->addStretch();
    buttonBox = editor::footer(dialog, root, QStringLiteral("保存热键"));
    dialog->setMinimumSize(440, 360);
    personal::prepareDialog(dialog, 560, 390);
  }
};

class ActionDialog
{
public:
  QComboBox *comboTriggerOn;
  QComboBox *comboActionType;
  QComboBox *comboSwitchToComputer;
  QComboBox *comboSwitchInDirection;
  QComboBox *comboLockCursorToComputer;
  KeySequenceWidget *keySequenceWidget;
  QWidget *groupComputers;
  QListWidget *listComputers;
  QDialogButtonBox *buttonBox;
  QLabel *actionDescription;
  QLabel *parameterLabel;
  QLabel *mouseTargetHelp;

  void setupUi(QDialog *dialog)
  {
    using namespace deskflow::gui;
    dialog->setObjectName(QStringLiteral("controlActionEditor"));
    dialog->setWindowTitle(QStringLiteral("热键动作"));
    auto *root = new QVBoxLayout(dialog);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(
        personal::dialogHeading(
            QStringLiteral("设置热键动作"), QStringLiteral("选择触发时机、动作内容和接收设备。"), dialog
        )
    );
    auto *content = editor::body(dialog, root);
    auto *trigger = editor::card(content, QStringLiteral("触发与动作"));
    auto *triggerForm = editor::form(trigger);
    comboTriggerOn = new QComboBox(dialog);
    comboTriggerOn->setObjectName(QStringLiteral("comboTriggerOn"));
    comboTriggerOn->addItems({QStringLiteral("热键按下时"), QStringLiteral("热键释放时")});
    triggerForm->addRow(QStringLiteral("触发时机"), comboTriggerOn);
    comboActionType = new QComboBox(dialog);
    comboActionType->setObjectName(QStringLiteral("comboActionType"));
    comboActionType->addItems(
        {QStringLiteral("按住按键"), QStringLiteral("释放按键"), QStringLiteral("按下并释放按键"),
         QStringLiteral("切换到指定设备"), QStringLiteral("按方向切换设备"), QStringLiteral("切换到下一台设备"),
         QStringLiteral("更改指针锁定"), QStringLiteral("重新启动共享服务"), QStringLiteral("随热键保持按下")}
    );
    triggerForm->addRow(QStringLiteral("执行动作"), comboActionType);
    actionDescription = editor::label({}, dialog, "secondaryText");
    trigger->addWidget(actionDescription);
    parameterLabel = editor::label({}, dialog, "sectionTitle");
    trigger->addWidget(parameterLabel);
    keySequenceWidget = new KeySequenceWidget(dialog);
    keySequenceWidget->setObjectName(QStringLiteral("actionKeyRecorder"));
    trigger->addWidget(keySequenceWidget);
    mouseTargetHelp = editor::label(
        QStringLiteral("鼠标事件发送到当前控制的设备。键盘按键可选择下方的接收设备。"), dialog, "secondaryText"
    );
    trigger->addWidget(mouseTargetHelp);
    comboSwitchToComputer = new QComboBox(dialog);
    comboSwitchToComputer->setObjectName(QStringLiteral("comboSwitchToComputer"));
    trigger->addWidget(comboSwitchToComputer);
    comboSwitchInDirection = new QComboBox(dialog);
    comboSwitchInDirection->setObjectName(QStringLiteral("comboSwitchInDirection"));
    comboSwitchInDirection->addItems(
        {QStringLiteral("左侧"), QStringLiteral("右侧"), QStringLiteral("上方"), QStringLiteral("下方")}
    );
    trigger->addWidget(comboSwitchInDirection);
    comboLockCursorToComputer = new QComboBox(dialog);
    comboLockCursorToComputer->setObjectName(QStringLiteral("comboLockCursorToComputer"));
    comboLockCursorToComputer->addItems(
        {QStringLiteral("切换锁定状态"), QStringLiteral("锁定到当前设备"), QStringLiteral("解除锁定")}
    );
    trigger->addWidget(comboLockCursorToComputer);
    auto *targets = editor::card(
        content, QStringLiteral("接收设备"), QStringLiteral("勾选接收此键盘事件的设备。全选表示发送到所有设备。")
    );
    groupComputers = targets->parentWidget();
    listComputers = new QListWidget(dialog);
    listComputers->setObjectName(QStringLiteral("actionTargetComputers"));
    listComputers->setMinimumHeight(96);
    listComputers->setMaximumHeight(180);
    listComputers->setAccessibleName(QStringLiteral("接收按键事件的设备"));
    targets->addWidget(listComputers);
    content->addStretch();
    buttonBox = editor::footer(dialog, root, QStringLiteral("保存动作"));
    dialog->setMinimumSize(470, 400);
    personal::prepareDialog(dialog, 600, 640);
  }
};
} // namespace Ui
