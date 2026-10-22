/* SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception */
#include "PersonalWindow.h"
#include "AdvancedPage.h"
#include "DeviceIllustration.h"
#include "LoginStartup.h"
#include "PersonalStyle.h"
#include "ShortcutPage.h"
#include "ToggleSwitch.h"
#include "common/Settings.h"

#include <QAction>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyle>
#include <QToolBar>
#include <QVBoxLayout>

namespace deskflow::gui::personal {
namespace {
QLabel *label(const QString &text, QWidget *parent, const char *role = nullptr)
{
  auto *item = new QLabel(text, parent);
  item->setTextFormat(Qt::PlainText);
  item->setWordWrap(true);
  if (role)
    item->setProperty(role, true);
  return item;
}

QFrame *card(QWidget *parent)
{
  auto *item = new QFrame(parent);
  item->setProperty("personalCard", true);
  return item;
}

QWidget *scrollPage(QWidget *body, QWidget *parent)
{
  auto *scroll = new QScrollArea(parent);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setWidgetResizable(true);
  scroll->setWidget(body);
  return scroll;
}
} // namespace

PersonalWindow::PersonalWindow(QWidget *parent, LoginStartup *service)
    : QWidget(parent),
      m_loginStartup(service ? service : new LoginStartup(this))
{
  setObjectName(QStringLiteral("personalWindow"));
  auto *root = new QHBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);
  auto *sidebar = new QFrame(this);
  sidebar->setObjectName(QStringLiteral("personalSidebar"));
  sidebar->setFixedWidth(206);
  auto *side = new QVBoxLayout(sidebar);
  side->setContentsMargins(14, 24, 14, 20);
  side->setSpacing(7);
  auto *brand = new QHBoxLayout;
  auto *brandIcon = new QLabel(sidebar);
  brandIcon->setObjectName(QStringLiteral("personalBrandIcon"));
  brandIcon->setFixedSize(35, 35);
  brand->addWidget(brandIcon);
  auto *brandText = new QVBoxLayout;
  brandText->setSpacing(1);
  auto *name = label(tr("桌桥"), sidebar);
  QFont brandFont = name->font();
  brandFont.setPointSize(17);
  brandFont.setWeight(QFont::DemiBold);
  name->setFont(brandFont);
  brandText->addWidget(name);
  brandText->addWidget(label(QStringLiteral("DeskBridge"), sidebar, "secondaryText"));
  brand->addLayout(brandText);
  brand->addStretch();
  side->addLayout(brand);
  side->addSpacing(25);
  const QStringList names{tr("设备连接"), tr("快捷键映射"), tr("通用"), tr("高级")};
  for (int i = 0; i < names.size(); ++i) {
    if (i == Advanced) {
      side->addStretch();
      auto *line = new QFrame(sidebar);
      line->setFrameShape(QFrame::HLine);
      line->setProperty("personalSeparator", true);
      side->addWidget(line);
      side->addSpacing(6);
    }
    auto *button = new QPushButton(names[i], sidebar);
    button->setCheckable(true);
    button->setAutoExclusive(true);
    button->setProperty("personalNav", true);
    button->setIconSize(QSize(22, 22));
    connect(button, &QPushButton::toggled, this, [this, i](bool checked) {
      if (checked)
        showPage(static_cast<Page>(i));
    });
    m_navigation.append(button);
    side->addWidget(button);
  }
  side->addSpacing(16);
  m_sidebarStatus = label(tr("未连接"), sidebar, "secondaryText");
  side->addWidget(m_sidebarStatus);
  root->addWidget(sidebar);

  auto *content = new QWidget(this);
  content->setProperty("personalPageBody", true);
  auto *contentLayout = new QVBoxLayout(content);
  contentLayout->setContentsMargins(0, 0, 0, 0);
  contentLayout->setSpacing(0);
  m_toolbar = new QToolBar(content);
  m_toolbar->setObjectName(QStringLiteral("personalToolbar"));
  m_toolbar->setMovable(false);
  m_toolbar->setFloatable(false);
  auto *heading = new QWidget(m_toolbar);
  auto *headingLayout = new QVBoxLayout(heading);
  headingLayout->setContentsMargins(0, 0, 0, 0);
  headingLayout->setSpacing(3);
  m_title = label({}, heading, "pageTitle");
  m_subtitle = label({}, heading, "pageSubtitle");
  m_subtitle->setObjectName(QStringLiteral("personalPageSubtitle"));
  m_subtitle->setWordWrap(false);
  m_subtitle->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  headingLayout->addWidget(m_title);
  headingLayout->addWidget(m_subtitle);
  m_toolbar->addWidget(heading);
  auto *spacer = new QWidget(m_toolbar);
  spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  m_toolbar->addWidget(spacer);
  m_primaryButton = new QPushButton(tr("连接"), m_toolbar);
  m_primaryButton->setObjectName(QStringLiteral("personalPrimaryConnection"));
  m_primaryButton->setMinimumWidth(92);
  connect(m_primaryButton, &QPushButton::clicked, this, [this] {
    if (m_running)
      Q_EMIT disconnectRequested();
    else
      Q_EMIT connectRequested();
  });
  m_connectionAction = m_toolbar->addWidget(m_primaryButton);
  contentLayout->addWidget(m_toolbar);

  m_pages = new QStackedWidget(content);
  m_pages->addWidget(makeConnectionPage());
  m_shortcuts = new ShortcutPage(m_pages);
  if (auto *internalTitle = m_shortcuts->findChild<QLabel *>(QStringLiteral("shortcutPageInternalTitle")))
    internalTitle->hide();
  m_pages->addWidget(m_shortcuts);
  m_pages->addWidget(makeGeneralPage());
  m_advanced = new AdvancedPage(m_pages);
  if (auto *heading = m_advanced->findChild<QWidget *>(QStringLiteral("advancedPageHeading")))
    heading->hide();
  if (auto *body = m_advanced->findChild<QVBoxLayout *>(QStringLiteral("advancedBodyLayout")))
    body->setContentsMargins(28, 22, 28, 22);
  m_pages->addWidget(m_advanced);
  contentLayout->addWidget(m_pages, 1);
  root->addWidget(content, 1);
  reloadPreferences();
  connect(m_loginStartup, &LoginStartup::statusChanged, this, &PersonalWindow::updateLoginStartup);
  connect(m_loginStartup, &LoginStartup::applyingChanged, this, [this](bool applying) {
    if (applying) {
      m_loginStartupFeedback.clear();
      m_loginStartupFailed = false;
    }
    updateLoginStartup();
  });
  connect(m_loginStartup, &LoginStartup::finished, this, [this](bool success, const QString &message) {
    m_loginStartupFeedback = message;
    m_loginStartupFailed = !success;
    updateLoginStartup();
  });
  window()->installEventFilter(this);
  refreshLoginStartup();
  showPage(Connection);
  refreshAppearance();
}

QWidget *PersonalWindow::makeConnectionPage()
{
  auto *body = new QWidget;
  body->setProperty("personalPageBody", true);
  auto *layout = new QVBoxLayout(body);
  layout->setContentsMargins(28, 22, 28, 22);
  layout->setSpacing(12);
  auto *stateRow = new QHBoxLayout;
  stateRow->addStretch();
  m_status = label(tr("未连接"), body, "sectionTitle");
  m_status->setObjectName(QStringLiteral("personalConnectionStatus"));
  m_status->setWordWrap(false);
  QFont stateFont = m_status->font();
  stateFont.setPointSize(16);
  m_status->setFont(stateFont);
  stateRow->addWidget(m_status);
  stateRow->addSpacing(20);
  m_security = label(tr("TLS 已开启"), body, "secondaryText");
  m_security->setObjectName(QStringLiteral("personalConnectionSecurity"));
  m_security->setWordWrap(false);
  stateRow->addWidget(m_security);
  stateRow->addStretch();
  layout->addLayout(stateRow);
  auto *intro = label(tr("一套键鼠，在多台电脑间自由移动"), body, "secondaryText");
  intro->setAlignment(Qt::AlignCenter);
  layout->addWidget(intro);
  m_devices = new DeviceIllustration(body);
  connect(m_devices, &DeviceIllustration::screenLayoutRequested, this, &PersonalWindow::screenLayoutRequested);
  connect(m_devices, &DeviceIllustration::screenMoveRequested, this, &PersonalWindow::screenMoveRequested);
  layout->addWidget(m_devices, 1);
  m_connectionHint = label(tr("从来源电脑的屏幕边缘进入本机，再移回边缘返回"), body);
  m_connectionHint->setObjectName(QStringLiteral("personalConnectionHint"));
  m_connectionHint->setAlignment(Qt::AlignCenter);
  layout->addWidget(m_connectionHint);
  auto *layoutNote = label(tr("屏幕布局由键鼠来源电脑管理"), body, "secondaryText");
  m_layoutNote = layoutNote;
  layoutNote->setObjectName(QStringLiteral("personalLayoutNote"));
  layoutNote->setAlignment(Qt::AlignCenter);
  layout->addWidget(layoutNote);
  m_manageDevices = new QPushButton(tr("管理设备与控制…"), body);
  m_manageDevices->setObjectName(QStringLiteral("manageDevicesButton"));
  m_manageDevices->setToolTip(tr("查看设备与控制选项；来源端专属设置会标明设置位置"));
  connect(m_manageDevices, &QPushButton::clicked, this, &PersonalWindow::deviceManagementRequested);
  layout->addWidget(m_manageDevices, 0, Qt::AlignHCenter);
  refreshScreenLayout();
  layout->addSpacing(10);
  auto *options = new QPushButton(body);
  m_connectionOptions = options;
  options->setObjectName(QStringLiteral("connectionSettingsButton"));
  options->setAccessibleName(tr("连接设置"));
  options->setProperty("personalRow", true);
  options->setMinimumHeight(76);
  auto *optionsLayout = new QHBoxLayout(options);
  optionsLayout->setContentsMargins(18, 14, 18, 14);
  auto *optionsText = new QVBoxLayout;
  optionsText->setSpacing(4);
  optionsText->addWidget(label(tr("连接设置"), options, "sectionTitle"));
  m_connectionDetails = label(tr("设备名称、本机角色与来源地址"), options, "secondaryText");
  optionsText->addWidget(m_connectionDetails);
  optionsLayout->addLayout(optionsText, 1);
  optionsLayout->addWidget(label(QStringLiteral("›"), options, "secondaryText"));
  connect(options, &QPushButton::clicked, this, &PersonalWindow::connectionOptionsRequested);
  layout->addWidget(options);
  m_connectionFeedback = label({}, body, "secondaryText");
  m_connectionFeedback->setObjectName(QStringLiteral("personalConnectionFeedback"));
  m_connectionFeedback->hide();
  layout->addWidget(m_connectionFeedback);
  return scrollPage(body, this);
}

QWidget *PersonalWindow::makeGeneralPage()
{
  auto *body = new QWidget;
  body->setProperty("personalPageBody", true);
  auto *layout = new QVBoxLayout(body);
  layout->setContentsMargins(28, 24, 28, 24);
  layout->setSpacing(14);
  auto *startup = card(body);
  auto *startupLayout = new QVBoxLayout(startup);
  startupLayout->setContentsMargins(18, 16, 18, 16);
  startupLayout->setSpacing(12);
  startupLayout->addWidget(label(tr("启动与窗口"), startup, "sectionTitle"));
  m_loginStartupSwitch = new ToggleSwitch(tr("登录时启动"), startup);
  m_loginStartupSwitch->setObjectName(QStringLiteral("loginStartupSwitch"));
  m_loginStartupSwitch->setAccessibleName(tr("登录时启动桌桥"));
  startupLayout->addWidget(m_loginStartupSwitch);
  m_loginStartupHint = label({}, startup, "secondaryText");
  m_loginStartupHint->setObjectName(QStringLiteral("loginStartupHint"));
  startupLayout->addWidget(m_loginStartupHint);
  startupLayout->addWidget(label(tr("登录启动独立即时生效，不会中断当前共享。"), startup, "secondaryText"));
  m_loginStartupSettings = new QPushButton(tr("在系统设置中允许…"), startup);
  m_loginStartupSettings->setObjectName(QStringLiteral("loginStartupSystemSettings"));
  m_loginStartupSettings->setAccessibleName(tr("打开系统设置以允许登录启动"));
  startupLayout->addWidget(m_loginStartupSettings, 0, Qt::AlignLeft);
  connect(m_loginStartupSwitch, &QCheckBox::toggled, this, [this](bool enabled) {
    const auto status = m_loginStartup->status();
    if (m_configurationBusy || m_loginStartup->isApplying() || !status.supported || !status.error.isEmpty()) {
      updateLoginStartup();
      return;
    }
    m_loginStartupFeedback.clear();
    m_loginStartupFailed = false;
    m_loginStartup->setEnabled(enabled);
    updateLoginStartup();
  });
  connect(m_loginStartupSettings, &QPushButton::clicked, this, [this] {
    const auto status = m_loginStartup->status();
    if (!m_configurationBusy && !m_loginStartup->isApplying() && status.supported && status.approvalRequired &&
        status.error.isEmpty())
      m_loginStartup->openSystemSettings();
  });
  auto *startupSeparator = new QFrame(startup);
  startupSeparator->setFrameShape(QFrame::HLine);
  startupSeparator->setProperty("personalSeparator", true);
  startupLayout->addWidget(startupSeparator);
  m_autoConnect = new ToggleSwitch(tr("打开桌桥时自动启动共享"), startup);
  m_autoConnect->setObjectName(QStringLiteral("automaticSharingSwitch"));
  m_autoHide = new ToggleSwitch(tr("启动时隐藏主窗口"), startup);
  m_autoHide->setObjectName(QStringLiteral("hideMainWindowSwitch"));
  m_closeToTray = new ToggleSwitch(
#ifdef Q_OS_MACOS
      tr("关闭窗口后继续在菜单栏运行"), startup
  );
#else
      tr("关闭窗口后继续在系统托盘中运行"), startup
  );
#endif
  for (auto *item : {m_autoConnect, m_autoHide, m_closeToTray}) {
    startupLayout->addWidget(item);
    connect(item, &QCheckBox::toggled, this, &PersonalWindow::markPreferencesDirty);
  }
  layout->addWidget(startup);
  auto *appearance = card(body);
  auto *appearanceLayout = new QVBoxLayout(appearance);
  appearanceLayout->setContentsMargins(18, 16, 18, 16);
  appearanceLayout->setSpacing(12);
  appearanceLayout->addWidget(label(tr("外观"), appearance, "sectionTitle"));
  appearanceLayout->addWidget(label(tr("界面随系统切换浅色与深色外观。"), appearance, "secondaryText"));
  m_showVersion = new ToggleSwitch(tr("在窗口标题中显示版本"), appearance);
  m_showVersion->setObjectName(QStringLiteral("showVersionInTitleSwitch"));
  appearanceLayout->addWidget(m_showVersion);
  m_symbolicTray = new ToggleSwitch(tr("使用单色托盘图标"), appearance);
  m_symbolicTray->setObjectName(QStringLiteral("symbolicTrayIconSwitch"));
  appearanceLayout->addWidget(m_symbolicTray);
#ifndef Q_OS_LINUX
  m_symbolicTray->hide();
#endif
  connect(m_showVersion, &QCheckBox::toggled, this, &PersonalWindow::markPreferencesDirty);
  connect(m_symbolicTray, &QCheckBox::toggled, this, &PersonalWindow::markPreferencesDirty);
  layout->addWidget(appearance);
  auto *sharing = card(body);
  auto *sharingLayout = new QVBoxLayout(sharing);
  sharingLayout->setContentsMargins(18, 16, 18, 16);
  sharingLayout->setSpacing(10);
  sharingLayout->addWidget(label(tr("共享行为"), sharing, "sectionTitle"));
  m_preventSleep = new ToggleSwitch(tr("共享期间防止本机休眠"), sharing);
  m_preventSleep->setObjectName(QStringLiteral("preventSleepSwitch"));
  sharingLayout->addWidget(m_preventSleep);
  sharingLayout->addWidget(
      label(tr("仅在共享核心运行期间保持本机唤醒。更改后需要重新连接。"), sharing, "secondaryText")
  );
  connect(m_preventSleep, &QCheckBox::toggled, this, &PersonalWindow::markPreferencesDirty);
  layout->addWidget(sharing);
  auto *scrolling = card(body);
  auto *scrollingLayout = new QVBoxLayout(scrolling);
  scrollingLayout->setContentsMargins(18, 16, 18, 16);
  scrollingLayout->setSpacing(12);
  scrollingLayout->addWidget(label(tr("远程鼠标"), scrolling, "sectionTitle"));
  m_invertScroll = new ToggleSwitch(tr("反转远程鼠标的垂直滚动方向"), scrolling);
  scrollingLayout->addWidget(m_invertScroll);
  connect(m_invertScroll, &QCheckBox::toggled, this, &PersonalWindow::markPreferencesDirty);
  auto *speedRow = new QHBoxLayout;
  speedRow->addWidget(label(tr("滚动速度"), scrolling));
  speedRow->addStretch();
  m_scrollSpeed = new QDoubleSpinBox(scrolling);
  m_scrollSpeed->setObjectName(QStringLiteral("verticalScrollSpeed"));
  m_scrollSpeed->setRange(0.1, 10.0);
  m_scrollSpeed->setSingleStep(0.1);
  m_scrollSpeed->setDecimals(1);
  m_scrollSpeed->setSuffix(QStringLiteral(" ×"));
  speedRow->addWidget(m_scrollSpeed);
  scrollingLayout->addLayout(speedRow);
  connect(m_scrollSpeed, &QDoubleSpinBox::valueChanged, this, &PersonalWindow::markPreferencesDirty);
  layout->addWidget(scrolling);
  m_invertXScroll = new ToggleSwitch(tr("反转远程鼠标的水平滚动方向"), scrolling);
  m_invertXScroll->setObjectName(QStringLiteral("horizontalScrollInvert"));
  scrollingLayout->addWidget(m_invertXScroll);
  connect(m_invertXScroll, &QCheckBox::toggled, this, &PersonalWindow::markPreferencesDirty);
  auto *horizontalSpeed = new QHBoxLayout;
  horizontalSpeed->addWidget(label(tr("水平滚动速度"), scrolling));
  horizontalSpeed->addStretch();
  m_xScrollSpeed = new QDoubleSpinBox(scrolling);
  m_xScrollSpeed->setObjectName(QStringLiteral("horizontalScrollSpeed"));
  m_xScrollSpeed->setRange(0.1, 10.0);
  m_xScrollSpeed->setSingleStep(0.1);
  m_xScrollSpeed->setDecimals(1);
  m_xScrollSpeed->setSuffix(QStringLiteral(" ×"));
  horizontalSpeed->addWidget(m_xScrollSpeed);
  scrollingLayout->addLayout(horizontalSpeed);
  connect(m_xScrollSpeed, &QDoubleSpinBox::valueChanged, this, &PersonalWindow::markPreferencesDirty);
  layout->addStretch();
  auto *page = new QWidget(this);
  auto *outer = new QVBoxLayout(page);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(0);
  outer->addWidget(scrollPage(body, page), 1);
  auto *footer = new QWidget(page);
  footer->setProperty("personalFooter", true);
  auto *footerLayout = new QHBoxLayout(footer);
  footerLayout->setContentsMargins(28, 12, 28, 14);
  footerLayout->setSpacing(10);
  m_preferencesMessage = label({}, footer, "secondaryText");
  m_preferencesMessage->setMinimumWidth(0);
  footerLayout->addWidget(m_preferencesMessage, 1);
  m_revertPreferences = new QPushButton(tr("还原更改"), footer);
  m_revertPreferences->setObjectName(QStringLiteral("revertGeneralPreferences"));
  connect(m_revertPreferences, &QPushButton::clicked, this, [this] {
    m_preferencesDirty = false;
    reloadPreferences();
    m_preferencesMessage->setText(tr("已还原未应用的通用设置"));
  });
  footerLayout->addWidget(m_revertPreferences);
  m_savePreferences = new QPushButton(tr("应用更改"), footer);
  m_savePreferences->setObjectName(QStringLiteral("applyGeneralPreferences"));
  m_savePreferences->setDefault(true);
  m_savePreferences->setProperty("primaryButton", true);
  connect(m_savePreferences, &QPushButton::clicked, this, &PersonalWindow::savePreferences);
  footerLayout->addWidget(m_savePreferences);
  outer->addWidget(footer);
  return page;
}

void PersonalWindow::showPage(Page page)
{
  m_pages->setCurrentIndex(page);
  for (int i = 0; i < m_navigation.size(); ++i) {
    const QSignalBlocker blocker(m_navigation[i]);
    m_navigation[i]->setChecked(i == page);
  }
  const QStringList titles{tr("设备连接"), tr("快捷键映射"), tr("通用"), tr("高级")};
  const QStringList subtitles{
      m_serverMode ? tr("本机（%1）· 键鼠来源").arg(displayOperatingSystem(shortcuts::localOS()))
                   : (m_sourceOS == shortcuts::DeviceOS::Unknown
                          ? tr("来源待识别 → 本机（%1）").arg(displayOperatingSystem(shortcuts::localOS()))
                          : tr("%1 → 本机（%2）")
                                .arg(displayOperatingSystem(m_sourceOS), displayOperatingSystem(shortcuts::localOS()))),
      tr("远程键盘 → 本机（%1）").arg(displayOperatingSystem(shortcuts::localOS())), tr("让桌桥适合你的使用习惯"),
      tr("连接安全、日志与维护")
  };
  m_title->setText(titles[page]);
  m_subtitle->setText(subtitles[page]);
  m_primaryButton->setVisible(page == Connection);
  m_connectionAction->setVisible(page == Connection);
  if (page == General)
    refreshLoginStartup();
  if (page == Advanced)
    m_advanced->refreshFromSettings();
  refreshAppearance();
}

void PersonalWindow::setConnectionStatus(
    deskflow::core::ConnectionState connection, deskflow::core::ProcessState process, bool tlsEnabled, bool secure,
    bool canConnect, const QString &host, const QString &macName
)
{
  using deskflow::core::ConnectionState;
  using deskflow::core::ProcessState;
  const bool connected = connection == ConnectionState::Connected;
  m_running = process != ProcessState::Stopped;
  QString status = tr("未连接");
  const auto peer = displayOperatingSystem(m_sourceOS);
  if (m_serverMode && connection == ConnectionState::Listening)
    status = tr("正在共享键鼠，等待客户端");
  else if (connected)
    status = m_serverMode ? tr("客户端已连接") : tr("已连接 %1 来源电脑").arg(peer);
  else if (process == ProcessState::Stopping)
    status = tr("正在断开连接…");
  else if (process == ProcessState::RetryPending)
    status = tr("正在重试连接…");
  else if (m_running)
    status = m_serverMode ? tr("正在启动共享…") : tr("正在连接来源电脑…");
  m_status->setText((connected ? QStringLiteral("●  ") : QStringLiteral("○  ")) + status);
  m_sidebarStatus->setText(m_status->text());
  m_status->setStyleSheet(connected ? QStringLiteral("color: #249b3c;") : QString());
  m_sidebarStatus->setStyleSheet(m_status->styleSheet());
  m_security->setText(connected && secure ? tr("已加密连接") : (tlsEnabled ? tr("TLS 已开启") : tr("未启用加密")));
  m_primaryButton->setText(
      m_serverMode ? (m_running ? tr("停止共享") : tr("开始共享")) : (m_running ? tr("断开连接") : tr("连接"))
  );
  m_connectionCanInteract = process != ProcessState::Stopping && (m_running || canConnect);
  m_primaryButton->setEnabled(
      m_connectionCanInteract && (!m_configurationBusy || (m_allowDisconnectWhileBusy && m_running))
  );
  m_primaryButton->setToolTip(
      !m_running && !canConnect ? tr("请先在连接设置中填写来源电脑地址并选择本机角色") : QString()
  );
  m_devices->setConnection(connected, host, macName);
  m_connectionDetails->setText(
      m_serverMode ? tr("%1 · 共享本机键鼠").arg(macName)
                   : tr("%1 · 接收自 %2").arg(macName, host.isEmpty() ? tr("尚未设置来源地址") : host)
  );
}

void PersonalWindow::setDeviceContext(shortcuts::DeviceOS sourceOS, bool server)
{
  if (m_sourceOS == sourceOS && m_serverMode == server)
    return;
  m_sourceOS = sourceOS;
  m_serverMode = server;
  m_manageDevices->setToolTip(
      server ? tr("添加或移除设备，设置控制热键、剪贴板共享与切屏行为")
             : tr("查看设备与控制选项；来源端专属设置会标明设置位置")
  );
  m_devices->setDeviceContext(sourceOS, server);
  m_shortcuts->setClientMode(!server);
  refreshScreenLayout();
  showPage(static_cast<Page>(m_pages->currentIndex()));
}

void PersonalWindow::setScreenLayout(const layout::Snapshot &snapshot, bool pending, const QString &feedback)
{
  m_screenLayout = snapshot;
  m_layoutPending = pending;
  m_layoutFeedback = feedback;
  refreshScreenLayout();
}

void PersonalWindow::refreshScreenLayout()
{
  m_devices->setScreenLayout(m_screenLayout, m_layoutPending, m_layoutFeedback);
  using Direction = layout::Direction;
  QString exitEdge;
  QString returnEdge;
  switch (m_screenLayout.direction) {
  case Direction::Left:
    exitEdge = tr("左");
    returnEdge = tr("右");
    break;
  case Direction::Right:
    exitEdge = tr("右");
    returnEdge = tr("左");
    break;
  case Direction::Above:
    exitEdge = tr("上");
    returnEdge = tr("下");
    break;
  case Direction::Below:
    exitEdge = tr("下");
    returnEdge = tr("上");
    break;
  default:
    break;
  }
  m_connectionHint->setText(
      !m_screenLayout.screens.isEmpty() ? tr("鼠标从相邻屏幕的对应边缘切换 · 点亮的圆点表示设备在线")
      : exitEdge.isEmpty()              ? tr("读取实际布局后，将显示鼠标切换的屏幕边缘")
                           : tr("从键鼠来源的%1边缘进入接收电脑，从接收电脑的%2边缘返回").arg(exitEdge, returnEdge)
  );
  QString note;
  if (m_layoutPending)
    note = m_layoutFeedback.isEmpty() ? tr("正在同步屏幕布局…确认后更新示意图") : m_layoutFeedback;
  else if (m_configurationBusy)
    note = tr("正在应用设置，暂时无法调整布局");
  else if (!m_layoutFeedback.isEmpty())
    note = m_layoutFeedback;
  else if (!m_screenLayout.error.isEmpty())
    note = m_screenLayout.error;
  else if (!m_screenLayout.screens.isEmpty() && m_screenLayout.editable)
    note = tr("拖动电脑到空白格；所有电脑需保持相邻连通 · 方向键移动选中设备");
  else if (m_screenLayout.editable && layout::isCardinal(m_screenLayout.direction))
    note = tr("拖动任一电脑到上、下、左、右位置 · 也可聚焦后使用方向键");
  else
    note = (!m_screenLayout.screens.isEmpty() || layout::isCardinal(m_screenLayout.direction))
               ? tr("当前屏幕布局只读，暂时无法在此调整")
               : tr("正在等待来源电脑提供实际屏幕布局");
  m_layoutNote->setText(note);
}

void PersonalWindow::setMappingStatus(bool enabled, int enabledRules, const QString &fullShortcutStatus)
{
  const auto summary =
      m_serverMode ? tr("快捷键适配由接收端管理")
      : enabled ? (fullShortcutStatus.isEmpty() ? tr("已开启 %1 条快捷键规则").arg(enabledRules) : fullShortcutStatus)
                : tr("快捷键适配已关闭");
  m_navigation[Shortcuts]->setToolTip(summary);
}

QVariantMap PersonalWindow::preferenceChanges() const
{
  const QVariantMap values{
      {Settings::Gui::AutoStartCore, m_autoConnect->isChecked()},
      {Settings::Gui::ShowVersionInTitle, m_showVersion->isChecked()},
      {Settings::Gui::SymbolicTrayIcon, m_symbolicTray->isChecked()},
      {Settings::Gui::Autohide, m_autoHide->isChecked()},
      {Settings::Gui::CloseToTray, m_closeToTray->isChecked()},
      {Settings::Core::PreventSleep, m_preventSleep->isChecked()},
      {Settings::Client::InvertYScroll, m_invertScroll->isChecked()},
      {Settings::Client::YScrollScale, m_scrollSpeed->value()},
      {Settings::Client::InvertXScroll, m_invertXScroll->isChecked()},
      {Settings::Client::XScrollScale, m_xScrollSpeed->value()}
  };
  QVariantMap changes;
  for (auto it = values.cbegin(); it != values.cend(); ++it)
    if (it.value() != m_preferencesBaseline.value(it.key()))
      changes.insert(it.key(), it.value());
  return changes;
}

void PersonalWindow::reloadPreferences()
{
  const bool writable = Settings::isWritable() && !m_configurationBusy;
  for (auto *box :
       {m_autoConnect, m_autoHide, m_closeToTray, m_preventSleep, m_invertScroll, m_invertXScroll, m_showVersion,
        m_symbolicTray})
    box->setEnabled(writable);
  for (auto *spin : {m_scrollSpeed, m_xScrollSpeed})
    spin->setEnabled(writable);
  m_savePreferences->setEnabled(writable && m_preferencesDirty);
  m_revertPreferences->setEnabled(!m_configurationBusy && m_preferencesDirty);
  if (!Settings::isWritable())
    m_preferencesMessage->setText(tr("设置当前只读，无法保存更改。"));
  if (m_preferencesDirty)
    return;
  m_loadingPreferences = true;
  m_autoConnect->setChecked(Settings::value(Settings::Gui::AutoStartCore).toBool());
  m_showVersion->setChecked(Settings::value(Settings::Gui::ShowVersionInTitle).toBool());
  m_symbolicTray->setChecked(Settings::value(Settings::Gui::SymbolicTrayIcon).toBool());
  m_autoHide->setChecked(Settings::value(Settings::Gui::Autohide).toBool());
  m_closeToTray->setChecked(Settings::value(Settings::Gui::CloseToTray).toBool());
  m_preventSleep->setChecked(Settings::value(Settings::Core::PreventSleep).toBool());
  m_invertScroll->setChecked(Settings::value(Settings::Client::InvertYScroll).toBool());
  m_scrollSpeed->setValue(Settings::value(Settings::Client::YScrollScale).toDouble());
  m_invertXScroll->setChecked(Settings::value(Settings::Client::InvertXScroll).toBool());
  m_xScrollSpeed->setValue(Settings::value(Settings::Client::XScrollScale).toDouble());
  m_preferencesBaseline = {
      {Settings::Gui::AutoStartCore, m_autoConnect->isChecked()},
      {Settings::Gui::ShowVersionInTitle, m_showVersion->isChecked()},
      {Settings::Gui::SymbolicTrayIcon, m_symbolicTray->isChecked()},
      {Settings::Gui::Autohide, m_autoHide->isChecked()},
      {Settings::Gui::CloseToTray, m_closeToTray->isChecked()},
      {Settings::Core::PreventSleep, m_preventSleep->isChecked()},
      {Settings::Client::InvertYScroll, m_invertScroll->isChecked()},
      {Settings::Client::YScrollScale, m_scrollSpeed->value()},
      {Settings::Client::InvertXScroll, m_invertXScroll->isChecked()},
      {Settings::Client::XScrollScale, m_xScrollSpeed->value()}
  };
  m_loadingPreferences = false;
  m_savePreferences->setEnabled(false);
  m_revertPreferences->setEnabled(false);
}

void PersonalWindow::markPreferencesDirty()
{
  if (m_loadingPreferences)
    return;
  m_preferencesDirty = !preferenceChanges().isEmpty();
  m_savePreferences->setEnabled(!m_configurationBusy && m_preferencesDirty && Settings::isWritable());
  m_revertPreferences->setEnabled(!m_configurationBusy && m_preferencesDirty);
  m_preferencesMessage->setText(
      m_preferencesDirty ? tr("有待应用的更改。修改远程鼠标或防休眠设置将短暂重新连接。") : tr("没有待应用的更改")
  );
}

void PersonalWindow::savePreferences()
{
  if (m_configurationBusy || !Settings::isWritable())
    return;
  Q_EMIT preferencesApplyRequested(preferenceChanges());
}

void PersonalWindow::setPreferencesStatus(const QString &message)
{
  m_preferencesMessage->setText(message);
}

void PersonalWindow::setConnectionFeedback(const QString &message)
{
  m_connectionFeedback->setText(message);
  m_connectionFeedback->setVisible(!message.isEmpty());
}

void PersonalWindow::finishPreferencesApply(bool success, const QString &message)
{
  if (success)
    m_preferencesDirty = false;
  reloadPreferences();
  m_preferencesMessage->setText(message);
}

void PersonalWindow::setConfigurationBusy(bool busy, bool allowDisconnect)
{
  m_configurationBusy = busy;
  m_allowDisconnectWhileBusy = allowDisconnect;
  m_devices->setConfigurationBusy(busy);
  refreshScreenLayout();
  m_primaryButton->setEnabled(m_connectionCanInteract && (!busy || (allowDisconnect && m_running)));
  const bool preferencesEnabled = !busy && Settings::isWritable();
  for (auto *box :
       {m_autoConnect, m_autoHide, m_closeToTray, m_preventSleep, m_invertScroll, m_invertXScroll, m_showVersion,
        m_symbolicTray})
    box->setEnabled(preferencesEnabled);
  for (auto *spin : {m_scrollSpeed, m_xScrollSpeed})
    spin->setEnabled(preferencesEnabled);
  m_revertPreferences->setEnabled(!busy && m_preferencesDirty);
  m_advanced->setApplying(busy);
  m_connectionOptions->setEnabled(!busy);
  m_manageDevices->setEnabled(!busy);
  m_savePreferences->setEnabled(!busy && m_preferencesDirty && Settings::isWritable());
  updateLoginStartup();
}

void PersonalWindow::refreshLoginStartup()
{
  if (m_loginStartup->isApplying())
    return;
  m_loginStartupFeedback.clear();
  m_loginStartupFailed = false;
  m_loginStartup->refresh();
  updateLoginStartup();
}

void PersonalWindow::updateLoginStartup()
{
  const auto status = m_loginStartup->status();
  const bool applying = m_loginStartup->isApplying();
  const QSignalBlocker blocker(m_loginStartupSwitch);
  m_loginStartupSwitch->setChecked(status.enabled || status.approvalRequired);
  m_loginStartupSwitch->setEnabled(status.supported && status.error.isEmpty() && !applying && !m_configurationBusy);
  m_loginStartupSettings->setVisible(status.supported && status.approvalRequired);
  m_loginStartupSettings->setEnabled(status.error.isEmpty() && !applying && !m_configurationBusy);
  QString message;
  if (applying) {
    message = tr("正在更新登录启动…");
  } else if (!status.error.isEmpty()) {
    message = tr("暂时无法确认登录启动状态。");
  } else if (!status.supported) {
#ifdef Q_OS_MACOS
    message = tr("当前环境不支持应用内管理登录启动，请在 macOS“系统设置 → 通用 → 登录项”中管理。");
#elif defined(Q_OS_WIN)
    message = tr("Windows 登录启动暂由系统管理，请前往“设置 → 应用 → 启动”。");
#else
    message = tr("登录启动暂由系统管理，请前往桌面环境的“启动应用程序”。");
#endif
  } else if (status.approvalRequired) {
    message = tr("已请求登录启动，但仍需在系统设置中允许后才会生效。");
  } else {
    message = status.enabled ? tr("已开启，下次登录时会启动桌桥。") : tr("已关闭，下次登录时不会自动启动桌桥。");
  }
  const QStringList details =
      status.error.isEmpty() ? QStringList{status.message, m_loginStartupFeedback} : QStringList{status.error};
  for (const auto &detail : details)
    if (!detail.isEmpty() && !message.contains(detail))
      message += QStringLiteral("\n") + detail;
  m_loginStartupHint->setText(message);
  m_loginStartupHint->setProperty("dangerText", !status.error.isEmpty() || m_loginStartupFailed);
  m_loginStartupHint->setProperty("secondaryText", status.error.isEmpty() && !m_loginStartupFailed);
  m_loginStartupHint->style()->unpolish(m_loginStartupHint);
  m_loginStartupHint->style()->polish(m_loginStartupHint);
}

bool PersonalWindow::eventFilter(QObject *watched, QEvent *event)
{
  if (event->type() == QEvent::WindowActivate)
    refreshLoginStartup();
  return QWidget::eventFilter(watched, event);
}

void PersonalWindow::refreshAppearance()
{
  if (m_updatingAppearance)
    return;
  m_updatingAppearance = true;
  applyPersonalStyle(this);
  const QStringList icons{
      QStringLiteral("devices"), QStringLiteral("keyboard"), QStringLiteral("settings"), QStringLiteral("advanced")
  };
  for (int i = 0; i < m_navigation.size(); ++i) {
    const auto color = m_navigation[i]->isChecked() ? QColor(Qt::white) : palette().color(QPalette::WindowText);
    m_navigation[i]->setIcon(personalIcon(icons[i], color));
  }
  if (auto *brand = findChild<QLabel *>(QStringLiteral("personalBrandIcon")))
    brand->setPixmap(personalIcon(QStringLiteral("bridge"), palette().color(QPalette::WindowText)).pixmap(35, 35));
  m_devices->update();
  m_updatingAppearance = false;
}

void PersonalWindow::changeEvent(QEvent *event)
{
  QWidget::changeEvent(event);
  if (event->type() == QEvent::ApplicationPaletteChange || event->type() == QEvent::PaletteChange)
    refreshAppearance();
}
} // namespace deskflow::gui::personal
