/* SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception */
#include "common/Settings.h"
#include "gui/personal/ReceiverDeviceControlDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QtTest>

using deskflow::gui::personal::ReceiverDeviceControlDialog;
using Destination = ReceiverDeviceControlDialog::Destination;

class ReceiverDeviceControlDialogTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase()
  {
    if (QGuiApplication::platformName() != QStringLiteral("offscreen") &&
        QGuiApplication::platformName() != QStringLiteral("minimal"))
      QSKIP("Requires isolated widget platform.");
    if (qEnvironmentVariableIntValue("DESKBRIDGE_UI_CAPTURE_DARK") == 1) {
      QApplication::setStyle(QStringLiteral("Fusion"));
      QPalette palette;
      palette.setColor(QPalette::Window, QColor(32, 33, 37));
      palette.setColor(QPalette::Base, QColor(41, 42, 47));
      palette.setColor(QPalette::AlternateBase, QColor(36, 37, 42));
      palette.setColor(QPalette::Text, QColor(235, 235, 235));
      palette.setColor(QPalette::WindowText, QColor(235, 235, 235));
      palette.setColor(QPalette::Button, QColor(41, 42, 47));
      palette.setColor(QPalette::ButtonText, QColor(235, 235, 235));
      palette.setColor(QPalette::Highlight, QColor(0, 103, 217));
      palette.setColor(QPalette::HighlightedText, Qt::white);
      QApplication::setPalette(palette);
    }
    QVERIFY(m_profiles.isValid());
    qputenv("XDG_CONFIG_HOME", m_profiles.filePath("config").toUtf8());
    qputenv("XDG_STATE_HOME", m_profiles.filePath("state").toUtf8());
    Settings::setSettingsFile(m_profiles.filePath(QStringLiteral("settings.conf")));
    Settings::setStateFile(m_profiles.filePath(QStringLiteral("state.conf")));
    Settings::setValue(Settings::Core::CoreMode, QVariant::fromValue(Settings::Client));
    Settings::setValue(Settings::Core::ComputerName, QStringLiteral("local-mac"));
    Settings::setValue(Settings::Client::RemoteHost, QStringLiteral("source.local"));
    Settings::setValue(Settings::Server::Heartbeat, 9000);
    Settings::save(false);
  }

  void showsActualSnapshotNamesWithoutInventingDeviceStates()
  {
    auto snapshot = actualLayout();
    // The reported local identity takes precedence over a potentially stale
    // saved identity, and a configured receiver is not reported as online.
    snapshot.localName = QStringLiteral("reported-mac");
    snapshot.screens[1].name = snapshot.localName;
    snapshot.screens.append({QStringLiteral("offline-pc"), 0, 1});
    snapshot.connectedNames = {snapshot.serverName, snapshot.localName};
    ReceiverDeviceControlDialog dialog(snapshot, QStringLiteral("192.168.1.8"), QStringLiteral("stale-mac"), true);
    QCOMPARE(label(dialog, "receiverSourceName")->text(), snapshot.serverName);
    QCOMPARE(label(dialog, "receiverLocalName")->text(), snapshot.localName);
    QCOMPARE(label(dialog, "receiverSourceAddress")->text(), QStringLiteral("192.168.1.8"));
    auto *devices = dialog.findChild<QListWidget *>(QStringLiteral("receiverLayoutDevices"));
    QVERIFY(devices);
    QCOMPARE(devices->count(), 3);
    QCOMPARE(devices->item(2)->text(), QStringLiteral("offline-pc"));
    QVERIFY(button(dialog, "receiverLayoutAction")->isEnabled());
    QVERIFY(label(dialog, "receiverLayoutStatus")->text().contains(QStringLiteral("允许本机调整")));
  }

  void disconnectedEmptyOverviewKeepsEverySettingDiscoverable()
  {
    ReceiverDeviceControlDialog dialog({}, {}, QStringLiteral("local-mac"), false);
    auto *navigation = dialog.findChild<QListWidget *>(QStringLiteral("receiverDeviceControlNavigation"));
    auto *pages = dialog.findChild<QStackedWidget *>(QStringLiteral("receiverDeviceControlPages"));
    QVERIFY(navigation && pages);
    QCOMPARE(navigation->count(), 4);
    QCOMPARE(pages->count(), 4);
    QCOMPARE(label(dialog, "receiverSourceName")->text(), QStringLiteral("尚未收到来源电脑名称"));
    QCOMPARE(label(dialog, "receiverSourceAddress")->text(), QStringLiteral("尚未填写"));
    QCOMPARE(label(dialog, "receiverConnectionState")->text(), QStringLiteral("未连接"));
    QVERIFY(!button(dialog, "receiverLayoutAction")->isEnabled());
    QVERIFY(button(dialog, "receiverConnectionAction")->isEnabled());
    QVERIFY(button(dialog, "receiverShortcutsAction")->isEnabled());
    const auto notices = dialog.findChildren<QLabel *>(QStringLiteral("sourceManagedNotice"));
    QCOMPARE(notices.count(), 12);
    for (auto *notice : notices)
      QCOMPARE(notice->text(), QStringLiteral("需要到来源端设置"));
    const QString allText = combinedText(dialog);
    for (const auto &capability : {QStringLiteral("添加与移除设备"), QStringLiteral("设备名称、别名与全局修饰键"),
                                   QStringLiteral("控制热键与触发时机"), QStringLiteral("热键动作"),
                                   QStringLiteral("剪贴板共享与大小限制"), QStringLiteral("延迟与双击切屏"),
                                   QStringLiteral("锁定与切换"), QStringLiteral("连接心跳"),
                                   QStringLiteral("兼容协议"), QStringLiteral("外部配置文件与导出")})
      QVERIFY2(allText.contains(capability), qPrintable(capability));
    QVERIFY(allText.contains(QStringLiteral("当前值未同步到本机")));
    QVERIFY(dialog.findChildren<QCheckBox *>().isEmpty());
    QVERIFY(dialog.findChildren<QComboBox *>().isEmpty());
    QVERIFY(dialog.findChildren<QLineEdit *>().isEmpty());
    QVERIFY(dialog.findChildren<QSpinBox *>().isEmpty());
  }

  void disconnectedSnapshotDoesNotOfferStaleLayoutEditing()
  {
    ReceiverDeviceControlDialog dialog(actualLayout(), QStringLiteral("source.local"), QStringLiteral("local-mac"), false);
    QVERIFY(!button(dialog, "receiverLayoutAction")->isEnabled());
    QVERIFY(label(dialog, "receiverLayoutStatus")->text().contains(QStringLiteral("上次收到")));
  }

  void readonlyOrFailedRemoteLayoutExplainsWhereToChangeIt()
  {
    auto snapshot = actualLayout();
    snapshot.editable = false;
    ReceiverDeviceControlDialog readonly(snapshot, QStringLiteral("source.local"), QStringLiteral("local-mac"), true);
    QVERIFY(!button(readonly, "receiverLayoutAction")->isEnabled());
    QVERIFY(label(readonly, "receiverLayoutStatus")->text().contains(QStringLiteral("需要到来源端设置")));
    snapshot.editable = true;
    snapshot.error = QStringLiteral("来源端正在应用布局，请稍后重试。");
    ReceiverDeviceControlDialog failed(snapshot, QStringLiteral("source.local"), QStringLiteral("local-mac"), true);
    QVERIFY(!button(failed, "receiverLayoutAction")->isEnabled());
    QCOMPARE(label(failed, "receiverLayoutStatus")->text(), snapshot.error);
  }

  void remoteStringsArePlainTextAndDoNotBecomeLinks()
  {
    auto snapshot = actualLayout();
    snapshot.serverName = QStringLiteral("<a href='https://invalid.example'>source</a>");
    snapshot.localName = QStringLiteral("<b>receiver</b>");
    snapshot.error = QStringLiteral("<img src='file:///not-a-setting'>");
    const QString address = QStringLiteral("<a href='file:///tmp/config'>host</a>");
    ReceiverDeviceControlDialog dialog(snapshot, address, {}, true);
    QCOMPARE(label(dialog, "receiverSourceName")->text(), snapshot.serverName);
    QCOMPARE(label(dialog, "receiverLocalName")->text(), snapshot.localName);
    QCOMPARE(label(dialog, "receiverSourceAddress")->text(), address);
    QCOMPARE(label(dialog, "receiverLayoutStatus")->text(), snapshot.error);
    QVERIFY(!button(dialog, "receiverLayoutAction")->toolTip().contains(snapshot.error));
    QVERIFY(!button(dialog, "receiverLayoutAction")->toolTip().contains(QStringLiteral("<img")));
    for (const auto *name : {"receiverSourceName", "receiverLocalName", "receiverSourceAddress", "receiverLayoutStatus"}) {
      const auto *value = label(dialog, name);
      QCOMPARE(value->textFormat(), Qt::PlainText);
      QVERIFY(!value->openExternalLinks());
      QVERIFY(!value->textInteractionFlags().testFlag(Qt::LinksAccessibleByMouse));
    }
  }

  void destinationsAndDismissalNeverWriteConfiguration()
  {
    const auto before = settingsBytes();
    const auto role = Settings::value(Settings::Core::CoreMode);
    const auto heartbeat = Settings::value(Settings::Server::Heartbeat);
    const QList<QPair<const char *, Destination>> destinations{
        {"receiverLayoutAction", Destination::Layout}, {"receiverShortcutsAction", Destination::Shortcuts},
        {"receiverConnectionAction", Destination::Connection}
    };
    for (const auto &destination : destinations) {
      ReceiverDeviceControlDialog dialog(actualLayout(), QStringLiteral("source.local"), QStringLiteral("local-mac"), true);
      QCOMPARE(dialog.destination(), Destination::None);
      button(dialog, destination.first)->click();
      QCOMPARE(dialog.result(), int(QDialog::Accepted));
      QCOMPARE(dialog.destination(), destination.second);
      QCOMPARE(settingsBytes(), before);
      QCOMPARE(Settings::value(Settings::Core::CoreMode), role);
      QCOMPARE(Settings::value(Settings::Server::Heartbeat), heartbeat);
    }
    ReceiverDeviceControlDialog dismissed(actualLayout(), {}, {}, true);
    button(dismissed, "receiverDeviceControlClose")->click();
    QCOMPARE(dismissed.result(), int(QDialog::Rejected));
    QCOMPARE(dismissed.destination(), Destination::None);
    QCOMPARE(settingsBytes(), before);
  }

  void captureReceiverOverview()
  {
    const auto directory = qEnvironmentVariable("DESKBRIDGE_UI_CAPTURE_DIR");
    if (directory.isEmpty())
      return;
    QVERIFY(QDir().mkpath(directory));
    ReceiverDeviceControlDialog dialog(actualLayout(), QStringLiteral("192.168.8.203"), QStringLiteral("local-mac"), true);
    dialog.show();
    auto *navigation = dialog.findChild<QListWidget *>(QStringLiteral("receiverDeviceControlNavigation"));
    for (int i = 0; i < navigation->count(); ++i) {
      navigation->setCurrentRow(i);
      QApplication::processEvents();
      QVERIFY(dialog.grab().save(directory + QStringLiteral("/receiver-device-control-%1.png").arg(i)));
    }
  }

private:
  static deskflow::layout::Snapshot actualLayout()
  {
    deskflow::layout::Snapshot snapshot;
    snapshot.serverName = QStringLiteral("linux-source");
    snapshot.localName = QStringLiteral("local-mac");
    snapshot.clientName = snapshot.localName;
    snapshot.direction = deskflow::layout::Direction::Right;
    snapshot.editable = true;
    snapshot.protocolVersion = 2;
    snapshot.screens = {{snapshot.serverName, 0, 0}, {snapshot.localName, 1, 0}};
    snapshot.revision = deskflow::layout::revisionFor(snapshot);
    snapshot.connectedNames = {snapshot.serverName, snapshot.localName};
    return snapshot;
  }

  static QLabel *label(const ReceiverDeviceControlDialog &dialog, const char *name)
  {
    return dialog.findChild<QLabel *>(QString::fromLatin1(name));
  }
  static QPushButton *button(const ReceiverDeviceControlDialog &dialog, const char *name)
  {
    return dialog.findChild<QPushButton *>(QString::fromLatin1(name));
  }
  static QString combinedText(const ReceiverDeviceControlDialog &dialog)
  {
    QStringList values;
    for (const auto *label : dialog.findChildren<QLabel *>())
      values.append(label->text());
    return values.join(u'\n');
  }
  static QByteArray settingsBytes()
  {
    QFile file(Settings::settingsFile());
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
  }
  QTemporaryDir m_profiles;
};

QTEST_MAIN(ReceiverDeviceControlDialogTests)
#include "ReceiverDeviceControlDialogTests.moc"
