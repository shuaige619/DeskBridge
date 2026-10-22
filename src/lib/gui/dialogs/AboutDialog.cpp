/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2024 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2008 Volker Lanz <vl@fidra.de>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "AboutDialog.h"
#include "ui_AboutDialog.h"

#include "SupportViews.h"
#include "common/Constants.h"
#include "common/Settings.h"
#include "common/VersionInfo.h"
#include "gui/StyleUtils.h"
#include "personal/DialogStyle.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QLabel>
#include <QPushButton>
#include <QSysInfo>
#include <QTabWidget>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

// 桌桥界面重设计，2026-10-21；保留上游版权与许可信息。

AboutDialog::AboutDialog(QWidget *parent) : QDialog(parent), ui{std::make_unique<Ui::AboutDialog>()}
{
  ui->setupUi(this);
  using namespace deskflow::gui::personal;
  setWindowTitle(tr("关于桌桥"));
  prepareDialog(this, 660, 570);
  ui->rootLayout->addWidget(dialogHeading(tr("桌桥"), tr("一套键鼠，连接你的工作空间。"), this));

  auto *body = new QWidget(this);
  auto *bodyLayout = new QVBoxLayout(body);
  bodyLayout->setContentsMargins(24, 0, 24, 16);
  bodyLayout->setSpacing(18);
  auto *identity = new QWidget(body);
  identity->setProperty("personalCard", true);
  auto *identityLayout = new QHBoxLayout(identity);
  identityLayout->setContentsMargins(18, 18, 18, 18);
  identityLayout->setSpacing(18);
  auto *icon = new QLabel(identity);
  icon->setPixmap(deskflow::gui::appIcon().pixmap(64, 64));
  identityLayout->addWidget(icon);
  auto *summaryLayout = new QVBoxLayout;
  auto *version = new QLabel(tr("桌桥  %1").arg(kDisplayVersion), identity);
  version->setProperty("sectionTitle", true);
  summaryLayout->addWidget(version);
  auto *description = new QLabel(tr("跨电脑共享鼠标、键盘与剪贴板，按应用适配快捷键。"), identity);
  description->setProperty("secondaryText", true);
  description->setWordWrap(true);
  summaryLayout->addWidget(description);
  identityLayout->addLayout(summaryLayout, 1);
  bodyLayout->addWidget(identity);

  auto *tabs = new QTabWidget(body);
  auto *diagnostics = new QTextBrowser(tabs);
  diagnostics->setAccessibleName(tr("版本与诊断信息"));
  diagnostics->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  diagnostics->setPlainText(versionText());
  tabs->addTab(diagnostics, tr("版本与诊断"));
  auto *credits = new QTextBrowser(tabs);
  credits->setOpenExternalLinks(false);
  credits->setHtml(
      tr("<h3>开源与致谢</h3>"
         "<p>桌桥（DeskBridge）基于 Deskflow 的共享引擎开发，重新设计了界面与操作流程，"
         "并提供接收端快捷键适配。</p>"
         "<p>上游版权：© Deskflow Developers、Synergy App Ltd、Symless Ltd、"
         "Chris Rizzitello、Volker Lanz 及各源文件注明的贡献者。</p>"
         "<p>桌桥遵循 GNU GPL 第 2 版，附 OpenSSL 链接例外。"
         "你可以依照许可复制、修改与分发本软件；本软件不提供担保。完整条款见「许可全文」。</p>"
         "<p>第三方组件包括 Qt、OpenSSL 与项目中注明的其他依赖。"
         "各组件的原始版权与许可声明保留在源代码及随附的 LICENSES 目录中。</p>")
  );
  tabs->addTab(credits, tr("开源与致谢"));
  auto *license = new QTextBrowser(tabs);
  license->setAccessibleName(tr("GNU GPL 第 2 版与 OpenSSL 例外全文"));
  license->setPlainText(supportLicenseText());
  tabs->addTab(license, tr("许可全文"));
  bodyLayout->addWidget(tabs, 1);
  ui->rootLayout->addWidget(body, 1);

  auto *footer = new QWidget(this);
  footer->setProperty("personalFooter", true);
  auto *footerLayout = new QHBoxLayout(footer);
  footerLayout->setContentsMargins(24, 14, 24, 14);
  auto *copy = new QPushButton(tr("复制诊断信息"), footer);
  copy->setToolTip(tr("复制版本、构建、Qt 与操作系统信息，用于排查问题"));
  connect(copy, &QPushButton::clicked, this, [this, copy] {
    copyVersionText();
    copy->setText(tr("已复制诊断信息"));
  });
  footerLayout->addWidget(copy);
  auto *releases = new QPushButton(tr("查看更新…"), footer);
  releases->setToolTip(tr("在浏览器中打开桌桥发布页，手动查看可用更新"));
  connect(releases, &QPushButton::clicked, this, [] {
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/shuaige619/DeskBridge/releases")));
  });
  footerLayout->addWidget(releases);
  footerLayout->addStretch();
  auto *close = new QPushButton(tr("完成"), footer);
  close->setProperty("primaryButton", true);
  close->setDefault(true);
  connect(close, &QPushButton::clicked, this, &QDialog::accept);
  footerLayout->addWidget(close);
  ui->rootLayout->addWidget(footer);
  applyPersonalStyle(this);
}

QString AboutDialog::versionText() const
{
  QString infoString = QStringLiteral("桌桥 / %1\n版本: %2\n构建: %3\nQt: %4\n系统: %5\n架构: %6")
                           .arg(
                               kAppName, kVersion, kVersionGitSha, qVersion(), QSysInfo::prettyProductName(),
                               QSysInfo::currentCpuArchitecture()
                           );
  if (Settings::isPortableMode()) {
    infoString.append(QStringLiteral("\n运行模式: 便携模式"));
  }
#ifdef Q_OS_LINUX
  infoString.append(QStringLiteral("\nSession: %1 (%2)")
                        .arg(qEnvironmentVariable("XDG_CURRENT_DESKTOP"), qEnvironmentVariable("XDG_SESSION_TYPE")));
#endif
  return infoString;
}

void AboutDialog::copyVersionText() const
{
  QGuiApplication::clipboard()->setText(versionText());
}

AboutDialog::~AboutDialog() = default;
