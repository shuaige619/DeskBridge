/* SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception */
#include "ReceiverDeviceControlDialog.h"

#include "DialogStyle.h"
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>

namespace deskflow::gui::personal {
namespace {
QLabel *text(const QString &value, QWidget *parent, const QString &name = {}, bool secondary = true)
{
  auto *label = new QLabel(value, parent);
  label->setObjectName(name);
  label->setTextFormat(Qt::PlainText);
  label->setWordWrap(true);
  label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  label->setProperty("secondaryText", secondary);
  return label;
}

QVBoxLayout *card(QVBoxLayout *page, const QString &title)
{
  auto *surface = new QWidget(page->parentWidget());
  surface->setProperty("personalCard", true);
  auto *layout = new QVBoxLayout(surface);
  layout->setContentsMargins(18, 16, 18, 16);
  layout->setSpacing(12);
  auto *heading = text(title, surface, {}, false);
  heading->setProperty("sectionTitle", true);
  layout->addWidget(heading);
  page->addWidget(surface);
  return layout;
}

QVBoxLayout *page(QStackedWidget *pages)
{
  auto *content = new QWidget(pages);
  auto *layout = new QVBoxLayout(content);
  layout->setContentsMargins(0, 0, 8, 20);
  layout->setSpacing(14);
  pages->addWidget(dialogScroll(content, pages));
  return layout;
}

QVBoxLayout *sourceCard(QVBoxLayout *page, const QString &title)
{
  auto *layout = card(page, title);
  layout->addWidget(text(
      QObject::tr("这些设置由来源端管理，当前值未同步到本机。请在来源电脑打开“设备与控制”查看或修改。"),
      layout->parentWidget()
  ));
  return layout;
}

void sourceOption(QVBoxLayout *layout, const QString &title, const QString &description)
{
  auto *row = new QWidget(layout->parentWidget());
  row->setProperty("sourceManagedOption", true);
  auto *rowLayout = new QVBoxLayout(row);
  rowLayout->setContentsMargins(0, 3, 0, 3);
  rowLayout->setSpacing(4);
  auto *heading = new QHBoxLayout;
  heading->setSpacing(12);
  heading->addWidget(text(title, row, {}, false), 1);
  auto *access = text(QObject::tr("需要到来源端设置"), row);
  access->setObjectName(QStringLiteral("sourceManagedNotice"));
  heading->addWidget(access, 0, Qt::AlignTop);
  rowLayout->addLayout(heading);
  rowLayout->addWidget(text(description, row));
  layout->addWidget(row);
}
} // namespace

ReceiverDeviceControlDialog::ReceiverDeviceControlDialog(
    const layout::Snapshot &snapshot, const QString &sourceAddress, const QString &localName, bool connected,
    QWidget *parent
)
    : QDialog(parent)
{
  setObjectName(QStringLiteral("receiverDeviceControlDialog"));
  setWindowTitle(tr("设备与控制"));
  prepareDialog(this, 920, 700);
  setMinimumSize(720, 500);

  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);
  root->addWidget(dialogHeading(
      tr("设备与控制"), tr("本机是接收端。可修改本机快捷键和来源端允许的布局；其他选项需到来源端设置。"), this
  ));
  auto *body = new QWidget(this);
  auto *bodyLayout = new QHBoxLayout(body);
  bodyLayout->setContentsMargins(16, 0, 16, 0);
  bodyLayout->setSpacing(16);
  auto *navigation = new QListWidget(body);
  navigation->setObjectName(QStringLiteral("receiverDeviceControlNavigation"));
  navigation->setProperty("dialogNavigation", true);
  navigation->setFixedWidth(150);
  navigation->setSpacing(4);
  navigation->addItems({tr("设备布局"), tr("控制热键"), tr("共享行为"), tr("连接与配置")});
  for (int i = 0; i < navigation->count(); ++i)
    navigation->item(i)->setSizeHint(QSize(138, 42));
  bodyLayout->addWidget(navigation);
  auto *pages = new QStackedWidget(body);
  pages->setObjectName(QStringLiteral("receiverDeviceControlPages"));
  bodyLayout->addWidget(pages, 1);
  root->addWidget(body, 1);

  const auto action = [this](QVBoxLayout *layout, const QString &title, const char *name, Destination destination) {
    auto *button = new QPushButton(title, layout->parentWidget());
    button->setObjectName(QString::fromLatin1(name));
    button->setAutoDefault(false);
    layout->addWidget(button, 0, Qt::AlignLeft);
    connect(button, &QPushButton::clicked, this, [this, destination] {
      m_destination = destination;
      accept();
    });
    return button;
  };

  auto *devicesPage = page(pages);
  auto *identity = card(devicesPage, tr("当前连接"));
  auto *identityForm = new QFormLayout;
  identityForm->setHorizontalSpacing(20);
  identityForm->setVerticalSpacing(10);
  identityForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  identityForm->addRow(
      tr("连接状态"), text(connected ? tr("已连接来源电脑") : tr("未连接"), body, QStringLiteral("receiverConnectionState"), false)
  );
  identityForm->addRow(
      tr("键鼠来源"),
      text(snapshot.serverName.isEmpty() ? tr("尚未收到来源电脑名称") : snapshot.serverName, body,
           QStringLiteral("receiverSourceName"), false)
  );
  identityForm->addRow(
      tr("来源地址"), text(sourceAddress.isEmpty() ? tr("尚未填写") : sourceAddress, body,
                             QStringLiteral("receiverSourceAddress"), false)
  );
  const auto receiverName = snapshot.localName.isEmpty() ? localName : snapshot.localName;
  identityForm->addRow(
      tr("本机名称"), text(receiverName.isEmpty() ? tr("尚未设置") : receiverName, body,
                             QStringLiteral("receiverLocalName"), false)
  );
  identity->addLayout(identityForm);

  auto *layoutCard = card(devicesPage, tr("来源电脑提供的布局"));
  auto *devices = new QListWidget(body);
  devices->setObjectName(QStringLiteral("receiverLayoutDevices"));
  devices->setAccessibleName(tr("来源电脑报告的设备列表"));
  devices->setSelectionMode(QAbstractItemView::NoSelection);
  devices->setFocusPolicy(Qt::NoFocus);
  devices->setWordWrap(true);
  devices->setMinimumHeight(104);
  QStringList deviceNames;
  for (const auto &screen : snapshot.screens)
    deviceNames.append(screen.name);
  if (deviceNames.isEmpty()) {
    if (!snapshot.serverName.isEmpty())
      deviceNames.append(snapshot.serverName);
    if (!snapshot.clientName.isEmpty() && !deviceNames.contains(snapshot.clientName))
      deviceNames.append(snapshot.clientName);
  }
  for (const auto &name : deviceNames) {
    QStringList roles;
    if (name == snapshot.serverName)
      roles.append(tr("键鼠来源"));
    if (name == receiverName)
      roles.append(tr("本机 · 接收端"));
    devices->addItem(roles.isEmpty() ? name : tr("%1  ·  %2").arg(name, roles.join(tr("、"))));
  }
  devices->setFixedHeight(qBound(78, int(deviceNames.size()) * 34 + 16, 200));
  devices->setVisible(!deviceNames.isEmpty());
  layoutCard->addWidget(devices);
  const bool hasLayout = !snapshot.screens.isEmpty() || layout::isCardinal(snapshot.direction);
  const bool editable = connected && snapshot.editable && hasLayout && snapshot.error.isEmpty();
  QString layoutMessage;
  if (!connected)
    layoutMessage = deviceNames.isEmpty() ? tr("连接来源电脑后，将显示实际设备布局。")
                                        : tr("显示上次收到的设备信息。重新连接来源电脑后可确认最新布局。");
  else if (!snapshot.error.isEmpty())
    layoutMessage = snapshot.error;
  else if (editable)
    layoutMessage = tr("来源电脑允许本机调整屏幕位置。设备增删和单机行为仍由来源端管理。");
  else if (hasLayout)
    layoutMessage = tr("当前布局只读，需要到来源端设置。");
  else
    layoutMessage = tr("尚未收到可调整的布局。若来源端未提供布局同步，需要到来源端设置。");
  layoutCard->addWidget(text(layoutMessage, body, QStringLiteral("receiverLayoutStatus")));
  auto *layoutButton = action(layoutCard, tr("在连接页调整布局"), "receiverLayoutAction", Destination::Layout);
  layoutButton->setEnabled(editable);
  layoutButton->setToolTip(
      editable ? tr("返回设备连接页调整屏幕位置") : tr("当前无法在本机调整布局，请参阅上方说明。")
  );
  auto *deviceOptions = sourceCard(devicesPage, tr("设备管理"));
  sourceOption(deviceOptions, tr("添加与移除设备"), tr("管理允许接入的电脑及其布局关系。"));
  sourceOption(deviceOptions, tr("设备名称、别名与全局修饰键"), tr("设置来源端设备记录、别名和修饰键替换。"));
  sourceOption(deviceOptions, tr("单机控制选项"), tr("四角防切换区、锁定键和 X11 兼容选项。"));
  devicesPage->addStretch();

  auto *hotkeysPage = page(pages);
  auto *localShortcuts = card(hotkeysPage, tr("本机快捷键适配"));
  localShortcuts->addWidget(text(tr("接收端的组合键转换、应用范围、完整适配和轻按 Shift 在本机设置。"), body));
  action(localShortcuts, tr("打开快捷键映射"), "receiverShortcutsAction", Destination::Shortcuts);
  auto *hotkeys = sourceCard(hotkeysPage, tr("来源端控制热键"));
  sourceOption(hotkeys, tr("控制热键与触发时机"), tr("添加、编辑或移除热键，设置按下或松开时触发。"));
  sourceOption(hotkeys, tr("热键动作"), tr("发送按键或鼠标按钮、切换设备或方向、切换下一台、锁定光标和重启共享。"));
  hotkeysPage->addStretch();

  auto *sharingPage = page(pages);
  auto *sharing = sourceCard(sharingPage, tr("共享行为"));
  sourceOption(sharing, tr("剪贴板共享与大小限制"), tr("是否共享剪贴板，以及单次传输的大小上限。"));
  sourceOption(sharing, tr("延迟与双击切屏"), tr("触碰屏幕边缘后的停留时间和连续两次触边的最长间隔。"));
  sourceOption(sharing, tr("锁定与切换"), tr("启动时锁定当前电脑、Scroll Lock 锁定功能和跨屏切换行为。"));
  sourceOption(sharing, tr("鼠标与窗口行为"), tr("相对鼠标移动，以及切换时保留 Windows 前台窗口。"));
  sharingPage->addStretch();

  auto *connectionPage = page(pages);
  auto *localConnection = card(connectionPage, tr("本机连接设置"));
  localConnection->addWidget(text(tr("本机名称、接收或来源角色、来源地址和断线重试可在本机调整。离线时也可打开。"), body));
  action(localConnection, tr("打开连接设置"), "receiverConnectionAction", Destination::Connection);
  auto *sourceConnection = sourceCard(connectionPage, tr("来源端连接与配置"));
  sourceOption(sourceConnection, tr("连接心跳"), tr("定期检测接收端连接及心跳间隔。"));
  sourceOption(sourceConnection, tr("兼容协议"), tr("选择与接收端匹配的共享协议。"));
  sourceOption(sourceConnection, tr("外部配置文件与导出"), tr("使用文件管理设备与热键，或导出来源端配置草稿。"));
  connectionPage->addStretch();

  connect(navigation, &QListWidget::currentRowChanged, pages, &QStackedWidget::setCurrentIndex);
  navigation->setCurrentRow(0);
  auto *footer = new QWidget(this);
  footer->setProperty("personalFooter", true);
  auto *footerLayout = new QHBoxLayout(footer);
  footerLayout->setContentsMargins(24, 14, 24, 16);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, footer);
  buttons->button(QDialogButtonBox::Close)->setText(tr("完成"));
  buttons->button(QDialogButtonBox::Close)->setObjectName(QStringLiteral("receiverDeviceControlClose"));
  footerLayout->addWidget(buttons);
  root->addWidget(footer);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  applyPersonalStyle(this);
}

ReceiverDeviceControlDialog::Destination ReceiverDeviceControlDialog::destination() const
{
  return m_destination;
}
} // namespace deskflow::gui::personal
