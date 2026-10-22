/* SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception */
#pragma once

#include "gui/personal/DialogStyle.h"
#include "gui/widgets/ComputerSetupView.h"
#include "gui/widgets/NewComputerWidget.h"
#include "gui/widgets/RemoveComputerWidget.h"

#include <QCheckBox>
#include <QDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace Ui {

class ServerConfigDialog
{
public:
  QListWidget *navigation = nullptr;
  QStackedWidget *tabWidget = nullptr;
  QWidget *tabComputers = nullptr;
  QWidget *tabHotkeys = nullptr;
  QLabel *externalConfigNotice = nullptr;
  ComputerSetupView *computerSetupView = nullptr;
  NewComputerWidget *lblNewComputer = nullptr;
  RemoveComputerWidget *lblRemoveComputer = nullptr;
  QPushButton *btnAddComputer = nullptr;
  QPushButton *btnEditComputer = nullptr;
  QPushButton *btnRemoveComputer = nullptr;
  QListWidget *listHotkeys = nullptr;
  QListWidget *listActions = nullptr;
  QPushButton *btnNewHotkey = nullptr;
  QPushButton *btnEditHotkey = nullptr;
  QPushButton *btnRemoveHotkey = nullptr;
  QPushButton *btnNewAction = nullptr;
  QPushButton *btnEditAction = nullptr;
  QPushButton *btnRemoveAction = nullptr;
  QCheckBox *cbDisableLockToComputer = nullptr;
  QCheckBox *cbDefaultLockToComputerState = nullptr;
  QCheckBox *cbRelativeMouseMoves = nullptr;
  QCheckBox *cbWin32KeepForeground = nullptr;
  QCheckBox *cbEnableClipboard = nullptr;
  QSpinBox *sbClipboardSizeLimit = nullptr;
  QCheckBox *cbSwitchDelay = nullptr;
  QSpinBox *sbSwitchDelay = nullptr;
  QCheckBox *cbSwitchDoubleTap = nullptr;
  QSpinBox *sbSwitchDoubleTap = nullptr;
  QCheckBox *cbHeartbeat = nullptr;
  QSpinBox *sbHeartbeat = nullptr;
  QRadioButton *rbProtocolSynergy = nullptr;
  QRadioButton *rbProtocolBarrier = nullptr;
  QCheckBox *groupExternalConfig = nullptr;
  QWidget *widgetExternalConfigControls = nullptr;
  QLineEdit *lineConfigFile = nullptr;
  QPushButton *btnBrowseConfigFile = nullptr;
  QPushButton *btnExportConfig = nullptr;

  void setupUi(QDialog *dialog)
  {
    using namespace deskflow::gui::personal;
    dialog->setObjectName(QStringLiteral("ServerConfigDialog"));
    dialog->setWindowTitle(QStringLiteral("设备与控制"));
    dialog->setMinimumSize(920, 640);
    auto *root = new QVBoxLayout(dialog);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(dialogHeading(
        QStringLiteral("设备与控制"), QStringLiteral("在键鼠来源电脑上管理设备、控制热键和共享行为。保存后生效。"),
        dialog
    ));
    externalConfigNotice = text(QStringLiteral("正在使用外部配置文件。设备布局与控制热键由该文件管理。"), dialog);
    externalConfigNotice->setContentsMargins(24, 0, 24, 14);
    root->addWidget(externalConfigNotice);

    auto *body = new QWidget(dialog);
    auto *bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(16, 0, 16, 0);
    bodyLayout->setSpacing(16);
    navigation = new QListWidget(body);
    navigation->setObjectName(QStringLiteral("serverSettingsNavigation"));
    navigation->setProperty("dialogNavigation", true);
    navigation->setFixedWidth(156);
    navigation->setSpacing(4);
    navigation->addItems(
        {QStringLiteral("设备布局"), QStringLiteral("控制热键"), QStringLiteral("共享行为"),
         QStringLiteral("连接与配置")}
    );
    for (int i = 0; i < navigation->count(); ++i)
      navigation->item(i)->setSizeHint(QSize(144, 42));
    bodyLayout->addWidget(navigation);
    tabWidget = new QStackedWidget(body);
    tabWidget->setObjectName(QStringLiteral("serverSettingsPages"));
    bodyLayout->addWidget(tabWidget, 1);
    root->addWidget(body, 1);

    makeDevices();
    makeHotkeys();
    makeSharing();
    makeNetwork();
    QObject::connect(navigation, &QListWidget::currentRowChanged, tabWidget, &QStackedWidget::setCurrentIndex);
    navigation->setCurrentRow(0);
    prepareDialog(dialog, 1060, 760);
  }

private:
  static QLabel *text(const QString &value, QWidget *parent)
  {
    auto *label = new QLabel(value, parent);
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    label->setProperty("secondaryText", true);
    return label;
  }

  static QVBoxLayout *card(QVBoxLayout *page, const QString &title, const QString &description = {})
  {
    auto *surface = new QWidget(page->parentWidget());
    surface->setProperty("personalCard", true);
    auto *layout = new QVBoxLayout(surface);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(12);
    auto *heading = new QLabel(title, surface);
    heading->setProperty("sectionTitle", true);
    layout->addWidget(heading);
    if (!description.isEmpty())
      layout->addWidget(text(description, surface));
    page->addWidget(surface);
    return layout;
  }

  QVBoxLayout *page(const QString &description, QWidget **pageWidget = nullptr)
  {
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 8, 20);
    layout->setSpacing(14);
    layout->addWidget(text(description, content));
    auto *scroll = deskflow::gui::personal::dialogScroll(content, tabWidget);
    tabWidget->addWidget(scroll);
    if (pageWidget)
      *pageWidget = scroll;
    return layout;
  }

  static QCheckBox *check(const QString &label, const char *name, QVBoxLayout *layout)
  {
    auto *box = new QCheckBox(label, layout->parentWidget());
    box->setObjectName(QString::fromLatin1(name));
    layout->addWidget(box);
    return box;
  }

  static QSpinBox *spin(const char *name, int minimum, int maximum, int step, const QString &suffix, QWidget *parent)
  {
    auto *box = new QSpinBox(parent);
    box->setObjectName(QString::fromLatin1(name));
    box->setRange(minimum, maximum);
    box->setSingleStep(step);
    box->setSuffix(suffix);
    box->setMinimumWidth(128);
    return box;
  }

  static QPushButton *button(const QString &label, const char *name, QWidget *parent)
  {
    auto *item = new QPushButton(label, parent);
    item->setObjectName(QString::fromLatin1(name));
    item->setAutoDefault(false);
    return item;
  }

  static void valueRow(QVBoxLayout *layout, const QString &label, QWidget *control)
  {
    auto *row = new QHBoxLayout;
    row->addWidget(text(label, layout->parentWidget()), 1);
    row->addWidget(control);
    layout->addLayout(row);
  }

  void makeDevices()
  {
    auto *layout = page(
        QStringLiteral("拖动设备来调整屏幕相邻关系。双击设备，或选中后点按“编辑设备”，配置名称、别名与单机行为。"),
        &tabComputers
    );
    auto *devices = card(layout, QStringLiteral("完整屏幕布局"));
    auto *tools = new QHBoxLayout;
    btnAddComputer = button(QStringLiteral("添加设备…"), "btnAddComputer", tabComputers);
    btnEditComputer = button(QStringLiteral("编辑设备…"), "btnEditComputer", tabComputers);
    btnRemoveComputer = button(QStringLiteral("移除设备"), "btnRemoveComputer", tabComputers);
    btnRemoveComputer->setProperty("dangerButton", true);
    tools->addWidget(btnAddComputer);
    tools->addWidget(btnEditComputer);
    tools->addWidget(btnRemoveComputer);
    tools->addStretch();
    devices->addLayout(tools);
    computerSetupView = new ComputerSetupView(tabComputers);
    computerSetupView->setObjectName(QStringLiteral("computerSetupView"));
    computerSetupView->setMinimumSize(560, 360);
    computerSetupView->setAccessibleName(QStringLiteral("完整设备布局，拖动调整位置，双击编辑设备"));
    devices->addWidget(computerSetupView, 1);
    auto *dragTools = new QHBoxLayout;
    lblNewComputer = new NewComputerWidget(tabComputers);
    lblNewComputer->setObjectName(QStringLiteral("lblNewComputer"));
    lblNewComputer->setFixedSize(44, 44);
    lblNewComputer->setAlignment(Qt::AlignCenter);
    lblNewComputer->setToolTip(QStringLiteral("将此图标拖入空白网格来添加设备"));
    dragTools->addWidget(lblNewComputer);
    dragTools->addWidget(text(QStringLiteral("拖入网格，添加设备"), tabComputers));
    dragTools->addStretch();
    lblRemoveComputer = new RemoveComputerWidget(tabComputers);
    lblRemoveComputer->setObjectName(QStringLiteral("lblRemoveComputer"));
    lblRemoveComputer->setAcceptDrops(true);
    lblRemoveComputer->setFixedSize(44, 44);
    lblRemoveComputer->setAlignment(Qt::AlignCenter);
    lblRemoveComputer->setToolTip(QStringLiteral("将设备拖到此处移除"));
    dragTools->addWidget(text(QStringLiteral("拖到此处，移除设备"), tabComputers));
    dragTools->addWidget(lblRemoveComputer);
    devices->addLayout(dragTools);
    layout->addStretch();
  }

  void makeHotkeys()
  {
    auto *layout = page(
        QStringLiteral(
            "控制热键在来源电脑执行，用来切换设备、锁定键鼠或执行一组动作。接收端的按键习惯转换位于主窗口“快捷键映射”。"
        ),
        &tabHotkeys
    );
    auto *hotkeys = card(layout, QStringLiteral("热键"), QStringLiteral("先选择一个热键，再配置它触发的动作。"));
    listHotkeys = new QListWidget(tabHotkeys);
    listHotkeys->setObjectName(QStringLiteral("listHotkeys"));
    listHotkeys->setMinimumHeight(130);
    listHotkeys->setAccessibleName(QStringLiteral("控制热键列表"));
    hotkeys->addWidget(listHotkeys);
    auto *hotkeyTools = new QHBoxLayout;
    btnNewHotkey = button(QStringLiteral("添加热键…"), "btnNewHotkey", tabHotkeys);
    btnEditHotkey = button(QStringLiteral("编辑…"), "btnEditHotkey", tabHotkeys);
    btnRemoveHotkey = button(QStringLiteral("移除"), "btnRemoveHotkey", tabHotkeys);
    for (auto *item : {btnNewHotkey, btnEditHotkey, btnRemoveHotkey})
      hotkeyTools->addWidget(item);
    hotkeyTools->addStretch();
    hotkeys->addLayout(hotkeyTools);
    auto *actions = card(
        layout, QStringLiteral("所选热键的动作"), QStringLiteral("一个热键可按顺序执行多个动作。双击条目可编辑。")
    );
    listActions = new QListWidget(tabHotkeys);
    listActions->setObjectName(QStringLiteral("listActions"));
    listActions->setMinimumHeight(130);
    listActions->setAccessibleName(QStringLiteral("所选热键的动作列表"));
    actions->addWidget(listActions);
    auto *actionTools = new QHBoxLayout;
    btnNewAction = button(QStringLiteral("添加动作…"), "btnNewAction", tabHotkeys);
    btnEditAction = button(QStringLiteral("编辑…"), "btnEditAction", tabHotkeys);
    btnRemoveAction = button(QStringLiteral("移除"), "btnRemoveAction", tabHotkeys);
    for (auto *item : {btnNewAction, btnEditAction, btnRemoveAction})
      actionTools->addWidget(item);
    actionTools->addStretch();
    actions->addLayout(actionTools);
    layout->addStretch();
  }

  void makeSharing()
  {
    auto *layout = page(QStringLiteral("这些设置由来源电脑管理，影响各接收端的共享行为。"));
    auto *switching =
        card(layout, QStringLiteral("跨屏方式"), QStringLiteral("为屏幕边缘添加等待或再次触边的条件，减少误切换。"));
    cbSwitchDelay = check(QStringLiteral("在屏幕边缘停留后切换"), "cbSwitchDelay", switching);
    sbSwitchDelay = spin("sbSwitchDelay", 10, 10000, 10, QStringLiteral(" 毫秒"), switching->parentWidget());
    valueRow(switching, QStringLiteral("停留时间"), sbSwitchDelay);
    cbSwitchDoubleTap = check(QStringLiteral("连续两次触碰屏幕边缘后切换"), "cbSwitchDoubleTap", switching);
    sbSwitchDoubleTap = spin("sbSwitchDoubleTap", 10, 10000, 10, QStringLiteral(" 毫秒"), switching->parentWidget());
    valueRow(switching, QStringLiteral("两次触边的最长间隔"), sbSwitchDoubleTap);
    auto *clipboard = card(layout, QStringLiteral("剪贴板"));
    cbEnableClipboard = check(QStringLiteral("在电脑间共享剪贴板"), "cbEnableClipboard", clipboard);
    sbClipboardSizeLimit = spin("sbClipboardSizeLimit", 0, 10000, 1, QStringLiteral(" MB"), clipboard->parentWidget());
    valueRow(clipboard, QStringLiteral("单次传输大小上限"), sbClipboardSizeLimit);
    auto *input = card(layout, QStringLiteral("键鼠控制"));
    cbDefaultLockToComputerState =
        check(QStringLiteral("启动共享时将键鼠锁定在当前电脑"), "cbDefaultLockToComputerState", input);
    cbDisableLockToComputer = check(QStringLiteral("禁用 Scroll Lock 键的锁定功能"), "cbDisableLockToComputer", input);
    cbRelativeMouseMoves = check(QStringLiteral("使用相对鼠标移动"), "cbRelativeMouseMoves", input);
    input->addWidget(
        text(QStringLiteral("相对移动适用于部分游戏、虚拟机或使用鼠标捕获的应用。"), input->parentWidget())
    );
    cbWin32KeepForeground = check(QStringLiteral("切换时保留 Windows 前台窗口"), "cbWin32KeepForeground", input);
    layout->addStretch();
  }

  void makeNetwork()
  {
    auto *layout = page(QStringLiteral("仅在兼容旧版客户端、排查连接或使用复杂布局文件时调整。"));
    auto *heartbeat = card(layout, QStringLiteral("连接检测"));
    cbHeartbeat = check(QStringLiteral("定期检测客户端连接"), "cbHeartbeat", heartbeat);
    sbHeartbeat = spin("sbHeartbeat", 1000, 30000, 1000, QStringLiteral(" 毫秒"), heartbeat->parentWidget());
    valueRow(heartbeat, QStringLiteral("心跳间隔"), sbHeartbeat);
    auto *protocol = card(
        layout, QStringLiteral("兼容协议"), QStringLiteral("选择与接收端兼容的协议。双方仍需使用一致的连接安全设置。")
    );
    rbProtocolSynergy = new QRadioButton(QStringLiteral("Synergy"), protocol->parentWidget());
    rbProtocolSynergy->setObjectName(QStringLiteral("rbProtocolSynergy"));
    rbProtocolBarrier = new QRadioButton(QStringLiteral("Barrier / Deskflow"), protocol->parentWidget());
    rbProtocolBarrier->setObjectName(QStringLiteral("rbProtocolBarrier"));
    protocol->addWidget(rbProtocolSynergy);
    protocol->addWidget(rbProtocolBarrier);
    auto *external = card(
        layout, QStringLiteral("外部配置文件"),
        QStringLiteral("使用文本配置定义复杂连接关系。启用后，文件将接管设备布局与控制热键；关闭后可继续使用图形编辑。")
    );
    groupExternalConfig = check(QStringLiteral("使用外部配置文件"), "groupExternalConfig", external);
    widgetExternalConfigControls = new QWidget(external->parentWidget());
    widgetExternalConfigControls->setObjectName(QStringLiteral("widgetExternalConfigControls"));
    auto *fileLayout = new QVBoxLayout(widgetExternalConfigControls);
    fileLayout->setContentsMargins(0, 0, 0, 0);
    fileLayout->setSpacing(8);
    fileLayout->addWidget(text(QStringLiteral("配置文件路径"), widgetExternalConfigControls));
    auto *fileRow = new QHBoxLayout;
    lineConfigFile = new QLineEdit(widgetExternalConfigControls);
    lineConfigFile->setObjectName(QStringLiteral("lineConfigFile"));
    lineConfigFile->setAccessibleName(QStringLiteral("外部配置文件路径"));
    lineConfigFile->setPlaceholderText(QStringLiteral("选择 .conf 配置文件"));
    btnBrowseConfigFile = button(QStringLiteral("选择文件…"), "btnBrowseConfigFile", widgetExternalConfigControls);
    fileRow->addWidget(lineConfigFile, 1);
    fileRow->addWidget(btnBrowseConfigFile);
    fileLayout->addLayout(fileRow);
    external->addWidget(widgetExternalConfigControls);
    auto *exporting = card(
        layout, QStringLiteral("导出配置"),
        QStringLiteral("导出当前图形布局与控制热键的草稿，便于备份或在其他电脑上使用。导出不会保存或应用本窗口的更改。")
    );
    btnExportConfig = button(QStringLiteral("导出服务端配置…"), "btnExportConfig", exporting->parentWidget());
    exporting->addWidget(btnExportConfig, 0, Qt::AlignLeft);
    layout->addStretch();
  }
};
} // namespace Ui
