/* SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception */
#include "common/Settings.h"
#include "gui/personal/ConnectionOptionsDialog.h"
#include "gui/personal/PersonalWindow.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QtTest>

using namespace deskflow::gui::personal;
class ConnectionOptionsDialogTests : public QObject
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
    QVERIFY(m_files.isValid());
    qputenv("XDG_CONFIG_HOME", m_files.path().toUtf8());
    qputenv("XDG_STATE_HOME", m_files.path().toUtf8());
  }
  void init()
  {
    const auto profile = QString::number(++m_profile);
    Settings::setSettingsFile(m_files.filePath(profile + ".conf"));
    Settings::setStateFile(m_files.filePath(profile + ".state"));
    Settings::setValue(Settings::Core::ComputerName, QStringLiteral("my-mac"));
    Settings::setValue(Settings::Core::CoreMode, QVariant::fromValue(Settings::Client));
    Settings::setValue(Settings::Client::RemoteHost, QStringLiteral("source.local"));
    Settings::save(false);
  }
  void editingAndCancellingDoesNotWriteSettings()
  {
    const auto before = settingsBytes();
    ConnectionOptionsDialog dialog;
    dialog.findChild<QLineEdit *>(QStringLiteral("connectionRemoteHost"))->setText(QStringLiteral("new.local"));
    dialog.findChild<QLineEdit *>(QStringLiteral("connectionComputerName"))->setText(QStringLiteral("new-mac"));
    QCOMPARE(dialog.changes().value(Settings::Client::RemoteHost).toString(), QStringLiteral("new.local"));
    dialog.reject();
    QCOMPARE(settingsBytes(), before);
    QCOMPARE(Settings::value(Settings::Client::RemoteHost).toString(), QStringLiteral("source.local"));
  }
  void roleChangesDoNotSaveInactiveClientFields()
  {
    ConnectionOptionsDialog dialog;
    auto *role = dialog.findChild<QComboBox *>(QStringLiteral("personalComputerRole"));
    auto *host = dialog.findChild<QLineEdit *>(QStringLiteral("connectionRemoteHost"));
    host->setText(QStringLiteral("ignored.local"));
    role->setCurrentIndex(role->findData(Settings::Server));
    const auto changes = dialog.changes();
    QCOMPARE(changes.value(Settings::Core::CoreMode).value<Settings::CoreMode>(), Settings::Server);
    QVERIFY(!changes.contains(Settings::Client::RemoteHost));
    QVERIFY(!changes.contains(Settings::Client::DynamicConnectionRetry));
  }
  void validationPreventsEmptyAddressAndDuplicateServerIdentity()
  {
    ConnectionOptionsDialog dialog({QStringLiteral("other-pc")});
    auto *save = dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save);
    auto *host = dialog.findChild<QLineEdit *>(QStringLiteral("connectionRemoteHost"));
    auto *role = dialog.findChild<QComboBox *>(QStringLiteral("personalComputerRole"));
    auto *name = dialog.findChild<QLineEdit *>(QStringLiteral("connectionComputerName"));
    QVERIFY(save->isEnabled());
    host->clear();
    QVERIFY(!save->isEnabled());
    role->setCurrentIndex(role->findData(Settings::Server));
    QVERIFY(save->isEnabled());
    name->setText(QStringLiteral("other-pc"));
    QVERIFY(!save->isEnabled());
    name->setText(QStringLiteral("invalid name"));
    QVERIFY(!save->isEnabled());
    name->setText(QStringLiteral("---"));
    QVERIFY(!save->isEnabled());
    name->setText(QStringLiteral(".other-pc"));
    QVERIFY(!save->isEnabled());
    name->setText(QStringLiteral("valid-pc"));
    QVERIFY(save->isEnabled());
  }
  void retryDraftRemainsIndependentOfStartupBehavior()
  {
    ConnectionOptionsDialog dialog;
    const auto startup = Settings::value(Settings::Gui::AutoStartCore);
    auto *retry = dialog.findChild<QCheckBox *>(QStringLiteral("personalDynamicRetry"));
    retry->setChecked(!retry->isChecked());
    const auto changes = dialog.changes();
    QVERIFY(changes.contains(Settings::Client::DynamicConnectionRetry));
    QVERIFY(!changes.contains(Settings::Gui::AutoStartCore));
    QCOMPARE(Settings::value(Settings::Gui::AutoStartCore), startup);
  }

  void identityConflictsIgnoreCaseButItsOwnCaseCanChange_data()
  {
    QTest::addColumn<Settings::CoreMode>("mode");
    QTest::newRow("source") << Settings::Server;
    QTest::newRow("receiver") << Settings::Client;
  }

  void identityConflictsIgnoreCaseButItsOwnCaseCanChange()
  {
    QFETCH(Settings::CoreMode, mode);
    ConnectionOptionsDialog dialog({QStringLiteral("other-pc"), QStringLiteral("other-alias")});
    auto *save = dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save);
    auto *role = dialog.findChild<QComboBox *>(QStringLiteral("personalComputerRole"));
    auto *name = dialog.findChild<QLineEdit *>(QStringLiteral("connectionComputerName"));
    role->setCurrentIndex(role->findData(mode));
    name->setText(QStringLiteral("OTHER-PC"));
    QVERIFY(!save->isEnabled());
    name->setText(QStringLiteral("OTHER-ALIAS"));
    QVERIFY(!save->isEnabled());
    name->setText(QStringLiteral("MY-MAC"));
    QVERIFY(save->isEnabled());
    QCOMPARE(dialog.changes().value(Settings::Core::ComputerName).toString(), QStringLiteral("MY-MAC"));
  }
  void captureRedesignedPages()
  {
    const auto directory = qEnvironmentVariable("DESKBRIDGE_UI_CAPTURE_DIR");
    if (directory.isEmpty())
      return;
    QDir().mkpath(directory);
    ConnectionOptionsDialog dialog;
    dialog.show();
    QApplication::processEvents();
    QVERIFY(dialog.grab().save(directory + "/connection-settings.png"));
    dialog.findChild<QComboBox *>(QStringLiteral("personalComputerRole"))->setCurrentIndex(1);
    QApplication::processEvents();
    QVERIFY(dialog.grab().save(directory + "/connection-source.png"));
    dialog.hide();
    PersonalWindow window;
    window.resize(1060, 720);
    window.setDeviceContext(deskflow::shortcuts::DeviceOS::Linux, true);
    window.setConnectionStatus(
        deskflow::core::ConnectionState::Disconnected, deskflow::core::ProcessState::Stopped, true, false, true,
        QStringLiteral("source.local"), QStringLiteral("my-mac")
    );
    window.show();
    const QStringList names{"connection", "shortcuts", "general", "advanced"};
    for (int i = 0; i < names.size(); ++i) {
      window.showPage(static_cast<PersonalWindow::Page>(i));
      QApplication::processEvents();
      QVERIFY(window.grab().save(directory + "/main-" + names[i] + ".png"));
    }
  }

private:
  QByteArray settingsBytes() const
  {
    QFile file(Settings::settingsFile());
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
  }
  QTemporaryDir m_files;
  int m_profile = 0;
};
QTEST_MAIN(ConnectionOptionsDialogTests)
#include "ConnectionOptionsDialogTests.moc"
