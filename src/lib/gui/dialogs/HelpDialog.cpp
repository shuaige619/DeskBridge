/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
// 桌桥离线使用指南，2026-10-21；覆盖重新设计后的完整工作流。

#include "HelpDialog.h"
#include "personal/DialogStyle.h"

#include <QDesktopServices>
#include <QEvent>
#include <QListWidget>
#include <QSignalBlocker>
#include <QVBoxLayout>

HelpDialog::HelpDialog(QWidget *parent, const QUrl &source)
    : QDialog(parent),
      m_btnBack{new QPushButton(this)},
      m_btnHome{new QPushButton(this)},
      m_browser{new QTextBrowser(this)},
      m_btnClose{new QPushButton(this)},
      m_topics{new QListWidget(this)}
{
  Q_UNUSED(source);
  using namespace deskflow::gui::personal;
  prepareDialog(this, 860, 660);
  setMinimumSize(660, 480);
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(dialogHeading(tr("桌桥使用指南"), tr("从连接第一台电脑，到按自己的习惯控制整个工作空间。"), this));
  auto *body = new QWidget(this);
  auto *bodyLayout = new QHBoxLayout(body);
  bodyLayout->setContentsMargins(24, 0, 24, 18);
  bodyLayout->setSpacing(18);
  m_topics->setMinimumWidth(150);
  m_topics->setMaximumWidth(180);
  m_topics->setAccessibleName(tr("帮助主题"));
  bodyLayout->addWidget(m_topics);
  m_browser->setOpenExternalLinks(false);
  m_browser->setOpenLinks(false);
  m_browser->setAccessibleName(tr("帮助正文"));
  bodyLayout->addWidget(m_browser, 1);
  layout->addWidget(body, 1);

  auto *footer = new QWidget(this);
  footer->setProperty("personalFooter", true);
  auto *footerLayout = new QHBoxLayout(footer);
  footerLayout->setContentsMargins(24, 14, 24, 14);
  m_btnBack->setShortcut(QKeySequence::Back);
  footerLayout->addWidget(m_btnBack);
  footerLayout->addWidget(m_btnHome);
  footerLayout->addStretch();
  m_btnClose->setProperty("primaryButton", true);
  m_btnClose->setDefault(true);
  footerLayout->addWidget(m_btnClose);
  layout->addWidget(footer);

  updateText();
  connect(m_topics, &QListWidget::currentRowChanged, this, [this](int page) { showPage(page); });
  connect(m_browser, &QTextBrowser::anchorClicked, this, &HelpDialog::linkClicked);
  connect(m_btnBack, &QPushButton::clicked, this, [this] {
    if (m_historyPosition > 0) {
      --m_historyPosition;
      showPage(m_history.at(m_historyPosition), false);
    }
  });
  connect(m_btnHome, &QPushButton::clicked, this, [this] { showPage(0); });
  connect(m_btnClose, &QPushButton::clicked, this, &QDialog::accept);
  showPage(0);
  applyPersonalStyle(this);
}

void HelpDialog::changeEvent(QEvent *e)
{
  QDialog::changeEvent(e);
  if (e->type() == QEvent::LanguageChange || e->type() == QEvent::PaletteChange) {
    updateText();
  }
}

void HelpDialog::updateText()
{
  setWindowTitle(tr("桌桥使用指南"));
  m_btnBack->setText(tr("返回"));
  m_btnHome->setText(tr("开始使用"));
  m_btnClose->setText(tr("完成"));
  const int previous = qMax(0, m_topics->currentRow());
  {
    const QSignalBlocker blocker(m_topics);
    m_topics->clear();
    m_topics->addItems(
        {tr("连接设备"), tr("设备与控制"), tr("快捷键映射"), tr("通用设置"), tr("高级设置"), tr("排查连接")}
    );
    m_topics->setCurrentRow(previous);
  }
  if (m_historyPosition >= 0)
    showPage(previous, false);
}

void HelpDialog::showPage(int page, bool remember)
{
  if (page < 0 || page >= m_topics->count())
    return;
  if (remember && (m_historyPosition < 0 || m_history.at(m_historyPosition) != page)) {
    m_history.resize(m_historyPosition + 1);
    m_history.append(page);
    ++m_historyPosition;
  }
  {
    const QSignalBlocker blocker(m_topics);
    m_topics->setCurrentRow(page);
  }
  const auto text = palette().color(QPalette::Text).name();
  const auto muted = palette().color(QPalette::PlaceholderText).name();
  const auto accent = deskflow::gui::personal::personalAccentColor().name();
  m_browser->setHtml(QStringLiteral(
                         "<html><head><style>body { color: %1; margin: 14px; font-size: 13px; }"
                         "h2 { font-size: 21px; font-weight: 600; margin: 0 0 14px; }"
                         "h3 { font-size: 14px; margin-top: 22px; margin-bottom: 8px; }"
                         "p, li { line-height: 155%; } li { margin-bottom: 8px; }"
                         "a { color: %3; } .note { color: %2; }</style></head><body>%4</body></html>"
  )
                         .arg(text, muted, accent, pageContent(page)));
  m_browser->moveCursor(QTextCursor::Start);
  historyChanged();
}

void HelpDialog::historyChanged()
{
  m_btnBack->setEnabled(m_historyPosition > 0);
  m_btnHome->setEnabled(m_topics->currentRow() != 0);
}

void HelpDialog::linkClicked(const QUrl &url)
{
  if (url.scheme() == QLatin1String("deskbridge-help")) {
    bool ok = false;
    const int page = url.path().mid(1).toInt(&ok);
    if (ok)
      showPage(page);
  } else if (url.scheme() == QLatin1String("https") && url.host() == QLatin1String("github.com")) {
    QDesktopServices::openUrl(url);
  }
}

QString HelpDialog::pageContent(int page) const
{
  switch (page) {
  case 0:
    return tr(
        "<h2>连接你的电脑</h2>"
        "<p>桌桥让多台电脑共用一套鼠标和键盘。每台电脑都需要运行桌桥，并能通过网络互相访问。</p>"
        "<h3>1 · 选择来源端</h3>"
        "<p>连接实体鼠标和键盘的电脑作为<b>来源端</b>。在「设备连接」中打开「连接设置」，"
        "选择本机角色并保存，然后开始共享。</p>"
        "<h3>2 · 连接接收端</h3>"
        "<p>在另一台电脑中选择<b>接收端</b>，填写来源电脑的地址。"
        "可使用来源端首页显示的局域网 IP；设备名称必须与来源端布局中的名称一致。</p>"
        "<h3>3 · 确认身份并排列设备</h3>"
        "<p>首次加密连接时，在对方电脑上查看本机证书指纹，逐字核对连接窗口里的「对方」指纹。"
        "确认一致后再信任。</p>"
        "<p>在来源端的「设备与控制」中添加接收端，按照实际摆放位置排列电脑。"
        "鼠标移向对应屏幕边缘即可切换控制。</p>"
        "<p class='note'>只有一台电脑作为来源端，其余电脑作为接收端。"
        "本机与对方各有自己的证书，两者指纹通常不同。</p>"
        "<p><a href='deskbridge-help:/1'>了解设备布局与切换控制 →</a></p>"
    );
  case 1:
    return tr(
        "<h2>设备与控制</h2>"
        "<p>在来源端首页打开「管理设备与控制…」，集中设置电脑布局与共享行为。</p>"
        "<h3>设备布局</h3>"
        "<p>添加、移除和排列设备。每台接收端的设备名称应与其本机名称一致；"
        "同一台电脑存在多个名称时，可以在设备设置中配置别名。</p>"
        "<p>设备设置还包括修饰键替换、屏幕角落限制与双击锁定等针对单台设备的行为。"
        "修饰键替换作用于整个设备，配置前请与接收端的快捷键规则协调。</p>"
        "<h3>控制热键</h3>"
        "<p>设置用于切换到指定电脑、按方向切屏、锁定光标以及发送按键等操作的热键。"
        "一个热键可以关联多个动作，也可以限定动作发生在按下或释放时。</p>"
        "<h3>共享行为</h3>"
        "<p>设置剪贴板、鼠标切屏条件、屏幕保护程序同步等共享行为。"
        "切屏延迟、双击边缘切换和角落限制可帮助减少误切换。</p>"
        "<h3>连接与配置</h3>"
        "<p>管理来源端的监听与配置方式。需要在其他安装中复用设置时，"
        "可使用导出服务端配置功能保存配置文件；使用外部文件时，"
        "请确认文件中的设备名称与当前电脑一致。</p>"
        "<p class='note'>来源端控制热键负责电脑之间的控制；"
        "接收端「快捷键映射」负责组合键适配。两者的执行位置和用途不同。</p>"
        "<p><a href='deskbridge-help:/2'>了解按应用适配快捷键 →</a></p>"
    );
  case 2:
    return tr(
        "<h2>快捷键映射</h2>"
        "<p>在<b>接收端</b>的「快捷键映射」页，按当前系统和应用适配来自另一台电脑的组合键。</p>"
        "<h3>选择来源系统与习惯方案</h3>"
        "<p>选择实体键盘所在电脑的系统，再选择一键习惯方案。保存后，接收端会将收到的匹配组合键"
        "转换为目标组合键。</p>"
        "<h3>按应用管理规则</h3>"
        "<p>添加或编辑规则，录入来源组合键、目标组合键与适用应用。"
        "通过启用开关决定规则是否生效；规则匹配当前前台应用后才会执行。</p>"
        "<p>如果同一组合键同时受到服务端修饰键替换或控制热键影响，"
        "请先检查「设备与控制」中的相关设置，避免同一次按键经过多次转换。</p>"
        "<h3>验证结果</h3>"
        "<p>保存并应用后，切换到目标应用，使用来源键盘验证常用操作。"
        "如果结果不符合预期，先停用相关规则，再逐条启用检查。</p>"
        "<p class='note'>来源系统与一键方案统一在此管理。连接设置只负责建立设备连接。</p>"
    );
  case 3:
    return tr(
        "<h2>通用设置</h2>"
        "<p>「通用」页管理桌桥的启动、窗口与日常使用习惯。</p>"
        "<h3>打开时自动开始共享</h3>"
        "<p>控制桌桥启动后是否直接开始共享或连接。设置前，请先完成本机角色与连接地址。</p>"
        "<h3>登录启动与后台运行</h3>"
        "<p>登录启动控制系统登录后是否打开桌桥；关闭窗口后的行为决定桌桥是否继续在后台运行。"
        "后台运行时，可从系统菜单栏或通知区域重新打开窗口。</p>"
        "<h3>保存设置</h3>"
        "<p>修改后按页面中的保存或应用操作提交。"
        "涉及连接核心的设置会在应用过程中重新建立连接，请留意首页连接状态。</p>"
        "<p class='note'>「打开时自动开始共享」与「逐步延长重试间隔」是两个设置："
        "前者控制启动行为，后者控制连接失败后的重试节奏。</p>"
    );
  case 4:
    return tr(
        "<h2>高级设置</h2>"
        "<p>「高级」页集中管理网络、安全、日志与运行方式。"
        "保留默认设置通常即可完成局域网连接。</p>"
        "<h3>网络与重试</h3>"
        "<p>来源端与接收端使用相同端口，来源地址应能从接收端访问。"
        "「逐步延长重试间隔」会在连接失败后逐渐增加等待时间；"
        "关闭该选项仍会重试连接。</p>"
        "<h3>加密与设备信任</h3>"
        "<p>TLS 加密保护电脑之间传输的内容。启用对端验证后，首次连接需要核对证书指纹。"
        "你可以查看本机指纹、管理已信任设备；证书更新后需要重新核对。</p>"
        "<h3>日志与诊断</h3>"
        "<p>遇到连接问题时查看日志中的时间与错误信息。"
        "在「关于桌桥」中复制诊断信息，可以同时获得版本、构建、Qt 和系统信息。</p>"
        "<h3>运行方式与权限</h3>"
        "<p>macOS 需要相应的辅助功能与输入权限。"
        "Windows 的后台服务模式用于支持登录界面和 UAC 权限提示；"
        "桌面模式只在当前用户会话中运行。</p>"
        "<p><a href='deskbridge-help:/5'>查看连接排查步骤 →</a></p>"
    );
  default:
    return tr(
        "<h2>排查连接</h2>"
        "<h3>找不到来源电脑</h3>"
        "<ol><li>确认来源端已经开始共享，接收端填写了正确的 IP 地址或主机名。</li>"
        "<li>确认两台电脑位于可互通的网络，端口一致，防火墙允许桌桥连接。</li>"
        "<li>检查接收端设备名称是否已加入来源端布局，名称是否完全一致。</li></ol>"
        "<h3>连接后无法控制</h3>"
        "<p>检查布局中的相邻方向，以及切屏延迟、角落限制和光标锁定设置。"
        "macOS 请检查系统权限；更改权限后可能需要重新打开桌桥。</p>"
        "<h3>证书指纹发生变化</h3>"
        "<p>先在对方电脑上查看本机证书指纹。只有确认设备身份和新指纹一致后，"
        "才更新信任记录。不要为跳过未知证书提示而关闭验证。</p>"
        "<h3>快捷键结果不符合预期</h3>"
        "<p>确认规则在接收端启用，并匹配当前前台应用；"
        "检查来源系统选择、目标组合键以及来源端的修饰键替换和控制热键。</p>"
        "<h3>获取更新与反馈信息</h3>"
        "<p>可在 <a href='https://github.com/shuaige619/DeskBridge/releases'>DeskBridge 发布页</a>"
        "手动查看更新。反馈问题时，附上复现步骤、相关日志与「关于桌桥」中的诊断信息。</p>"
    );
  }
}
