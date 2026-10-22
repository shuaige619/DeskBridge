/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2024 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
// 桌桥提示界面重设计，2026-10-21；保留原有响应与设置语义。

#include "Messages.h"
#include "Logger.h"
#include "common/Settings.h"
#include "common/UrlConstants.h"
#include "common/VersionInfo.h"
#include "personal/PersonalStyle.h"

#include <QApplication>
#include <QClipboard>
#include <QGridLayout>
#include <QMessageBox>
#include <QPushButton>
#include <memory>

namespace deskflow::gui::messages {

namespace {
void styleMessage(QMessageBox &message, const QString &title)
{
  message.setWindowTitle(title);
  message.setProperty("personalDialog", true);
  message.setWindowModality(Qt::WindowModal);
  message.setTextFormat(Qt::RichText);
  message.setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
  message.setMinimumWidth(420);
  deskflow::gui::personal::applyPersonalStyle(&message);
}

void information(QWidget *parent, const QString &title, const QString &text)
{
  QMessageBox message(QMessageBox::Information, title, text, QMessageBox::Ok, parent);
  styleMessage(message, title);
  message.button(QMessageBox::Ok)->setText(QObject::tr("知道了"));
  message.button(QMessageBox::Ok)->setProperty("primaryButton", true);
  message.exec();
}

void addDiagnosticCopy(QMessageBox &message, const QString &details)
{
  auto *copy = new QPushButton(QObject::tr("复制错误信息"), &message);
  // Place the utility action outside QMessageBox's closing button box.
  auto *layout = qobject_cast<QGridLayout *>(message.layout());
  layout->addWidget(copy, layout->rowCount(), 0, 1, layout->columnCount(), Qt::AlignLeft);
  QObject::connect(copy, &QPushButton::clicked, &message, [details, copy] {
    QApplication::clipboard()->setText(details);
    copy->setText(QObject::tr("已复制错误信息"));
  });
}
} // namespace

struct Errors
{
  static std::unique_ptr<QMessageBox> s_criticalMessage;
  static QStringList s_ignoredErrors;
};

std::unique_ptr<QMessageBox> Errors::s_criticalMessage;
QStringList Errors::s_ignoredErrors;

void raiseCriticalDialog()
{
  if (Errors::s_criticalMessage) {
    Errors::s_criticalMessage->raise();
  }
}

void showErrorDialog(const QString &message, const QString &fileLine, QtMsgType type)
{
  const bool fatal = type == QtFatalMsg;
  const auto title = fatal ? QObject::tr("桌桥 · 无法继续运行") : QObject::tr("桌桥 · 发生错误");
  const auto text = fatal ? QObject::tr(
                                "<p>桌桥遇到了无法恢复的错误，关闭此提示后将退出。</p>"
                                "<p>请复制错误信息以便排查，随后重新打开桌桥。</p>"
                            )
                          : QObject::tr(
                                "<p>桌桥遇到了错误。你可以复制信息以便排查。</p>"
                                "<p>选择「忽略此错误」后，本次运行期间不再重复提醒相同错误。</p>"
                            );
  const auto details =
      QStringLiteral("桌桥\nVersion: %1 (%2)\n%3\n%4").arg(kVersion, kVersionGitSha, message, fileLine);
  if (fatal) {
    // Fatal errors must remain blocking until the user dismisses the dialog,
    // after which messageHandler aborts the process as before.
    QMessageBox error(QMessageBox::Critical, title, text, QMessageBox::Abort);
    styleMessage(error, title);
    error.setDetailedText(details);
    error.button(QMessageBox::Abort)->setText(QObject::tr("退出桌桥"));
    addDiagnosticCopy(error, details);
    error.exec();
    return;
  }

  if (!Errors::s_ignoredErrors.contains(message)) {
    Errors::s_criticalMessage.reset();
    Errors::s_criticalMessage =
        std::make_unique<QMessageBox>(QMessageBox::Critical, title, text, QMessageBox::Ok | QMessageBox::Ignore);
    styleMessage(*Errors::s_criticalMessage, title);
    Errors::s_criticalMessage->setDetailedText(details);
    Errors::s_criticalMessage->button(QMessageBox::Ok)->setText(QObject::tr("关闭提示"));
    Errors::s_criticalMessage->button(QMessageBox::Ignore)->setText(QObject::tr("忽略此错误"));
    addDiagnosticCopy(*Errors::s_criticalMessage, details);
    QObject::connect(Errors::s_criticalMessage.get(), &QMessageBox::finished, [message](int result) {
      if (result == QMessageBox::Ignore) {
        Errors::s_ignoredErrors.append(message);
      }
    });
    Errors::s_criticalMessage->open();
  }
}

QString fileLine(const QMessageLogContext &context)
{
  if (!context.file) {
    return {};
  }
  return QStringLiteral("%1:%2").arg(context.file, QString::number(context.line));
}

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
  const auto source = messages::fileLine(context);
  Logger::instance()->handleMessage(type, source, message);
  if (type == QtFatalMsg || type == QtCriticalMsg) {
    showErrorDialog(message, source, type);
  }
  if (type == QtFatalMsg) {
    abort();
  }
}

void showCloseReminder(QWidget *parent)
{
  auto message = QObject::tr(
      "<p>桌桥会继续在后台共享键鼠。</p>"
      "<p>点击系统通知区域的桌桥图标，可以重新打开窗口。"
      "你可以在「通用设置」中调整关闭窗口后的行为。</p>"
  );
#if defined(Q_OS_LINUX)
  message.append(
      QObject::tr(
          "<p>GNOME 3 的通知区域可能默认关闭。"
          "如果看不到图标，可<a href=\"%1\">启用托盘扩展</a>。</p>"
      )
          .arg(kUrlGnomeTrayFix)
  );
#endif
  information(parent, QObject::tr("桌桥仍在后台运行"), message);
}

void showFirstServerStartMessage(QWidget *parent)
{
  information(
      parent, QObject::tr("已开始共享键鼠"),
      QObject::tr(
          "<p>这台电脑已成为来源端。</p>"
          "<p>在其他电脑中选择接收端，并填写这台电脑的地址。"
          "新设备请求连接时，桌桥会提示你确认。</p>"
          "<p>连接后，在「设备与控制」中按实际摆放位置排列电脑。</p>"
      )
  );
}

void showFirstConnectedMessage(QWidget *parent)
{
  auto message = QObject::tr("<p>设备已连接，可以开始共享键鼠了。</p>");
  if (Settings::value(Settings::Core::CoreMode).value<Settings::CoreMode>() == Settings::Server) {
    message.append(
        QObject::tr(
            "<p>将鼠标移向布局中另一台电脑的方向，即可切换控制并输入文字。"
            "开启剪贴板共享后，也可以跨电脑复制粘贴。</p>"
        )
    );
  } else {
    message.append(
        QObject::tr(
            "<p>使用来源端的鼠标和键盘控制这台电脑。"
            "如需适配当前系统的组合键，请前往「快捷键映射」。</p>"
        )
    );
  }
  using ProcessMode = Settings::ProcessMode;
  if (Settings::value(Settings::Core::ProcessMode).value<ProcessMode>() == ProcessMode::Desktop &&
      !Settings::value(Settings::Gui::CloseToTray).toBool()) {
    message.append(QObject::tr("<p>请保持桌桥窗口打开或最小化；当前设置下，关闭窗口会结束共享。</p>"));
  } else {
    message.append(
        QObject::tr(
            "<p>可以关闭窗口，桌桥会继续在后台运行。"
            "关闭窗口后的行为可在「通用设置」中调整。</p>"
        )
    );
  }
  information(parent, QObject::tr("桌桥 · 连接成功"), message);
}

bool showNewClientPrompt(QWidget *parent, const QString &clientName)
{
  if (Settings::value(Settings::Security::TlsEnabled).toBool() &&
      Settings::value(Settings::Security::CheckPeers).toBool()) {
    // Peer trust is confirmed before this informational layout prompt.
    information(
        parent, QObject::tr("添加已确认的设备"),
        QObject::tr(
            "<p>已接受设备「%1」的连接。</p>"
            "<p>接下来，将它加入「设备与控制」的电脑布局，以便通过屏幕边缘切换控制。</p>"
        )
            .arg(clientName.toHtmlEscaped())
    );
    return true;
  }
  QMessageBox message(parent);
  styleMessage(message, QObject::tr("新设备请求连接"));
  message.setIcon(QMessageBox::Question);
  auto *ignore = message.addButton(QObject::tr("暂不添加"), QMessageBox::RejectRole);
  auto *add = message.addButton(QObject::tr("添加设备"), QMessageBox::AcceptRole);
  add->setProperty("primaryButton", true);
  message.setDefaultButton(ignore);
  message.setText(
      QObject::tr(
          "<p>设备「%1」希望连接到这台电脑。</p>"
          "<p>添加后可以在电脑布局中安排它的位置。</p>"
      )
          .arg(clientName.toHtmlEscaped())
  );
  message.exec();
  return message.buttonRole(message.clickedButton()) == QMessageBox::AcceptRole;
}

bool showUpdateCheckOption(QWidget *parent)
{
  QMessageBox message(parent);
  styleMessage(message, QObject::tr("启动时检查更新"));
  message.setIcon(QMessageBox::Question);
  message.addButton(QObject::tr("暂不开启"), QMessageBox::RejectRole);
  const auto checkButton = message.addButton(QObject::tr("开启检查"), QMessageBox::AcceptRole);
  checkButton->setProperty("primaryButton", true);
  message.setText(
      QObject::tr(
          "<p>每次启动桌桥时，检查是否有可用更新？</p>"
          "<p>检查需要联网，使用以下更新地址：</p><p>%1</p>"
      )
          .arg(Settings::value(Settings::Gui::UpdateCheckUrl).toString().toHtmlEscaped())
  );
  message.exec();
  return message.clickedButton() == checkButton;
}

bool showDaemonOffline(QWidget *parent)
{
  QMessageBox message(parent);
  styleMessage(message, QObject::tr("后台服务尚未就绪"));
  message.setIcon(QMessageBox::Warning);
  auto *retry = message.addButton(QObject::tr("重试连接"), QMessageBox::AcceptRole);
  retry->setProperty("primaryButton", true);
  message.setDefaultButton(retry);
  const auto ignore = message.addButton(QObject::tr("稍后处理"), QMessageBox::RejectRole);
  const auto disable = message.addButton(QObject::tr("改用桌面模式"), QMessageBox::NoRole);
  message.setText(
      QObject::tr(
          "<p>无法连接桌桥后台服务。</p>"
          "<p>后台服务用于支持 Windows 登录界面与 UAC 权限提示。"
          "如果你需要这些功能，请重试，或在 Windows「服务」中重启桌桥服务。</p>"
          "<p>如果你主动停止了服务，可以改用桌面模式；这会停用后台服务运行方式。</p>"
      )
  );
  message.exec();
  if (message.clickedButton() == ignore) {
    return false;
  } else if (message.clickedButton() == disable) {
    Settings::setValue(Settings::Core::ProcessMode, Settings::ProcessMode::Desktop);
    return false;
  }
  return true;
}

} // namespace deskflow::gui::messages
