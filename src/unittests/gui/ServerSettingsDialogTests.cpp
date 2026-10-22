/* SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception */
#include "common/Settings.h"
#include "gui/ComputerSetupModel.h"
#include "gui/config/ServerConfig.h"
#include "gui/dialogs/ServerConfigDialog.h"
#include "gui/widgets/ComputerSetupView.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGroupBox>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QtTest>
#include <utility>

class ServerSettingsDialogTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase()
  {
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
    qputenv("XDG_CONFIG_HOME", m_profiles.filePath("isolated-config").toUtf8());
    qputenv("XDG_STATE_HOME", m_profiles.filePath("isolated-state").toUtf8());
    Settings::setStateFile(m_profiles.filePath(QStringLiteral("state.conf")));
  }

  void init()
  {
    const QString path = m_profiles.filePath(QString::number(++m_profile));
    QVERIFY(QDir().mkpath(path));
    Settings::setSettingsFile(path + QStringLiteral("/settings.conf"));
    Settings::setValue(Settings::Core::ComputerName, QStringLiteral("source"));
    Settings::setValue(Settings::Core::CoreMode, Settings::CoreMode::Server);
    Settings::setValue(Settings::Server::GridWidth, 5);
    Settings::setValue(Settings::Server::GridHeight, 3);
    Settings::setValue(Settings::Server::ExternalConfig, false);
    Settings::setValue(Settings::Server::ExternalConfigFile, QString());
    Settings::setValue(Settings::Server::EnableSwitchDelay, false);
    Settings::setValue(Settings::Server::SwitchDelay, 250);
    Settings::setValue(Settings::Server::EnableSwitchDoubleTap, false);
    Settings::setValue(Settings::Server::SwitchDoubleTap, 250);
    Settings::setValue(Settings::Server::EnableClipboard, true);
    Settings::setValue(Settings::Server::ClipboardSize, 3);
    Settings::setValue(Settings::Server::EnableHeartbeat, false);
    Settings::setValue(Settings::Server::Heartbeat, 5000);
    Settings::setValue(Settings::Server::Protocol, QStringLiteral("barrier"));
    ServerConfig initial;
    initial.addClient(QStringLiteral("target"));
    QVERIFY(initial.setScreenLayout(deskflow::layout::Direction::Right));
    initial.commit();
    Settings::save(false);
  }

  void exposesEveryCapabilityInFourScrollablePages()
  {
    ServerConfig config;
    ServerConfigDialog dialog(nullptr, config);
    QCOMPARE(dialog.windowTitle(), QStringLiteral("设备与控制"));
    QCOMPARE(dialog.windowType(), Qt::Dialog);
    QCOMPARE(dialog.windowModality(), Qt::WindowModal);
    auto *navigation = dialog.findChild<QListWidget *>(QStringLiteral("serverSettingsNavigation"));
    auto *pages = dialog.findChild<QStackedWidget *>(QStringLiteral("serverSettingsPages"));
    QVERIFY(navigation && pages);
    QCOMPARE(navigation->count(), 4);
    QCOMPARE(pages->count(), 4);
    QVERIFY(dialog.findChildren<QGroupBox *>().isEmpty());
    QVERIFY(dialog.findChildren<QTabWidget *>().isEmpty());

    const QStringList controls{
        QStringLiteral("computerSetupView"),
        QStringLiteral("btnAddComputer"),
        QStringLiteral("btnEditComputer"),
        QStringLiteral("btnRemoveComputer"),
        QStringLiteral("listHotkeys"),
        QStringLiteral("listActions"),
        QStringLiteral("btnNewHotkey"),
        QStringLiteral("btnEditHotkey"),
        QStringLiteral("btnRemoveHotkey"),
        QStringLiteral("btnNewAction"),
        QStringLiteral("btnEditAction"),
        QStringLiteral("btnRemoveAction"),
        QStringLiteral("cbDisableLockToComputer"),
        QStringLiteral("cbDefaultLockToComputerState"),
        QStringLiteral("cbRelativeMouseMoves"),
        QStringLiteral("cbWin32KeepForeground"),
        QStringLiteral("cbEnableClipboard"),
        QStringLiteral("sbClipboardSizeLimit"),
        QStringLiteral("cbSwitchDelay"),
        QStringLiteral("sbSwitchDelay"),
        QStringLiteral("cbSwitchDoubleTap"),
        QStringLiteral("sbSwitchDoubleTap"),
        QStringLiteral("cbHeartbeat"),
        QStringLiteral("sbHeartbeat"),
        QStringLiteral("rbProtocolSynergy"),
        QStringLiteral("rbProtocolBarrier"),
        QStringLiteral("groupExternalConfig"),
        QStringLiteral("lineConfigFile"),
        QStringLiteral("btnBrowseConfigFile"),
        QStringLiteral("btnExportConfig")
    };
    for (const QString &name : controls)
      QVERIFY2(dialog.findChild<QWidget *>(name), qPrintable(name));
    QVERIFY(!buttons(dialog)->button(QDialogButtonBox::Save)->isEnabled());
    QVERIFY(!dialog.findChild<QPushButton *>(QStringLiteral("btnNewAction"))->isEnabled());

    const QString captureDir = qEnvironmentVariable("DESKBRIDGE_UI_CAPTURE_DIR");
    if (!captureDir.isEmpty()) {
      QVERIFY(QDir().mkpath(captureDir));
      dialog.show();
      for (int i = 0; i < pages->count(); ++i) {
        navigation->setCurrentRow(i);
        QApplication::processEvents();
        QVERIFY(dialog.grab().save(captureDir + QStringLiteral("/server-settings-%1.png").arg(i)));
      }
    }
  }

  void sharedBehaviorDraftRestoresAndSavesWithoutEarlyWrites()
  {
    ServerConfig config;
    ServerConfigDialog dialog(nullptr, config);
    auto *delay = dialog.findChild<QCheckBox *>(QStringLiteral("cbSwitchDelay"));
    auto *interval = dialog.findChild<QSpinBox *>(QStringLiteral("sbSwitchDelay"));
    auto *heartbeat = dialog.findChild<QCheckBox *>(QStringLiteral("cbHeartbeat"));
    auto *heartbeatRate = dialog.findChild<QSpinBox *>(QStringLiteral("sbHeartbeat"));
    auto *clipboard = dialog.findChild<QCheckBox *>(QStringLiteral("cbEnableClipboard"));
    QVERIFY(delay && interval && heartbeat && heartbeatRate && clipboard);
    QVERIFY(!interval->isEnabled());
    QVERIFY(!heartbeatRate->isEnabled());
    const QByteArray before = settingsBytes();
    delay->setChecked(true);
    interval->setValue(640);
    heartbeat->setChecked(true);
    heartbeatRate->setValue(9000);
    clipboard->setChecked(false);
    QVERIFY(interval->isEnabled());
    QVERIFY(heartbeatRate->isEnabled());
    QVERIFY(!dialog.findChild<QSpinBox *>(QStringLiteral("sbClipboardSizeLimit"))->isEnabled());
    QVERIFY(buttons(dialog)->button(QDialogButtonBox::Save)->isEnabled());
    QCOMPARE(settingsBytes(), before);
    QVERIFY(!Settings::value(Settings::Server::EnableSwitchDelay).toBool());
    buttons(dialog)->button(QDialogButtonBox::Reset)->click();
    QVERIFY(!delay->isChecked());
    QCOMPARE(interval->value(), 250);
    QVERIFY(!heartbeat->isChecked());
    QVERIFY(clipboard->isChecked());
    QVERIFY(!buttons(dialog)->button(QDialogButtonBox::Save)->isEnabled());
    delay->setChecked(true);
    interval->setValue(640);
    buttons(dialog)->button(QDialogButtonBox::Save)->click();
    QCOMPARE(dialog.result(), int(QDialog::Accepted));
    QVERIFY(Settings::value(Settings::Server::EnableSwitchDelay).toBool());
    QCOMPARE(Settings::value(Settings::Server::SwitchDelay).toInt(), 640);
  }

  void hotkeyAndActionListsDescribeControlsWithoutProtocolSyntax()
  {
    KeySequence keys;
    keys.appendKey(Qt::Key_Control, Qt::ControlModifier);
    keys.appendKey(Qt::Key_F2, Qt::ControlModifier);
    Hotkey hotkey;
    hotkey.setKeySequence(keys);
    Action action;
    action.setType(static_cast<int>(Action::Type::switchToComputer));
    action.setSwitchComputerName(QStringLiteral("target"));
    action.setActiveOnRelease(true);
    action.setRestartServer(false);
    hotkey.addAction(action);
    const QString protocolHotkey = hotkey.text();
    const QString protocolAction = action.text();
    auto &settings = Settings::proxy();
    settings.beginGroup(QStringLiteral("internalConfig"));
    settings.beginWriteArray(QStringLiteral("hotkeys"));
    settings.setArrayIndex(0);
    hotkey.saveSettings(settings.get());
    settings.endArray();
    settings.endGroup();
    Settings::save(false);
    ServerConfig config;
    ServerConfigDialog dialog(nullptr, config);
    auto *hotkeys = dialog.findChild<QListWidget *>(QStringLiteral("listHotkeys"));
    auto *actions = dialog.findChild<QListWidget *>(QStringLiteral("listActions"));
    QVERIFY(hotkeys && actions);
    QCOMPARE(hotkeys->count(), 1);
    QVERIFY(hotkeys->item(0)->text().contains(QStringLiteral("Ctrl")));
    QVERIFY(hotkeys->item(0)->text().contains(QStringLiteral("F2")));
    QVERIFY(hotkeys->item(0)->text().contains(QStringLiteral("1 个动作")));
    QVERIFY(!hotkeys->item(0)->text().contains(QStringLiteral("keystroke(")));
    hotkeys->setCurrentRow(0);
    QCOMPARE(actions->count(), 1);
    QVERIFY(actions->item(0)->text().contains(QStringLiteral("松开热键时")));
    QVERIFY(actions->item(0)->text().contains(QStringLiteral("切换到设备 target")));
    QVERIFY(!actions->item(0)->text().contains(QStringLiteral("switchToScreen(")));
    QCOMPARE(std::as_const(config).hotkeys().first().text(), protocolHotkey);
    QCOMPARE(std::as_const(config).hotkeys().first().actions().first().text(), protocolAction);
  }

  void externalConfigurationIsDraftedValidatedAndCancelled()
  {
    ServerConfig config;
    ServerConfigDialog dialog(nullptr, config);
    auto *external = dialog.findChild<QCheckBox *>(QStringLiteral("groupExternalConfig"));
    auto *path = dialog.findChild<QLineEdit *>(QStringLiteral("lineConfigFile"));
    auto *layout = dialog.findChild<ComputerSetupView *>(QStringLiteral("computerSetupView"));
    QVERIFY(external && path && layout);
    const QByteArray before = settingsBytes();
    external->setChecked(true);
    path->setText(m_profiles.filePath(QStringLiteral("missing.conf")));
    QVERIFY(!buttons(dialog)->button(QDialogButtonBox::Save)->isEnabled());
    QVERIFY(buttons(dialog)->button(QDialogButtonBox::Reset)->isEnabled());
    QVERIFY(!layout->isEnabled());
    const QString existing = configurationFile();
    QVERIFY(!existing.isEmpty());
    path->setText(existing);
    QVERIFY(buttons(dialog)->button(QDialogButtonBox::Save)->isEnabled());
    QVERIFY(!Settings::value(Settings::Server::ExternalConfig).toBool());
    QCOMPARE(Settings::value(Settings::Server::ExternalConfigFile).toString(), QString());
    QCOMPARE(settingsBytes(), before);
    buttons(dialog)->button(QDialogButtonBox::Cancel)->click();
    QCOMPARE(dialog.result(), int(QDialog::Rejected));
    QCOMPARE(settingsBytes(), before);
    QVERIFY(!Settings::value(Settings::Server::ExternalConfig).toBool());
  }

  void validExternalConfigurationSavesOnlyOnConfirmation()
  {
    ServerConfig config;
    ServerConfigDialog dialog(nullptr, config);
    const QString existing = configurationFile();
    QVERIFY(!existing.isEmpty());
    dialog.findChild<QCheckBox *>(QStringLiteral("groupExternalConfig"))->setChecked(true);
    dialog.findChild<QLineEdit *>(QStringLiteral("lineConfigFile"))->setText(existing);
    buttons(dialog)->button(QDialogButtonBox::Save)->click();
    QCOMPARE(dialog.result(), int(QDialog::Accepted));
    QVERIFY(Settings::value(Settings::Server::ExternalConfig).toBool());
    QCOMPARE(Settings::value(Settings::Server::ExternalConfigFile).toString(), existing);
  }

  void deviceRemovalAndAdditionStayInDraftUntilSave()
  {
    ServerConfig config;
    ServerConfigDialog dialog(nullptr, config);
    auto *view = dialog.findChild<ComputerSetupView *>(QStringLiteral("computerSetupView"));
    auto *remove = dialog.findChild<QPushButton *>(QStringLiteral("btnRemoveComputer"));
    QVERIFY(view && remove);
    const auto selected = computerIndex(view, QStringLiteral("target"));
    QVERIFY(selected.isValid());
    view->setCurrentIndex(selected);
    QVERIFY(remove->isEnabled());
    remove->click();
    QVERIFY(!computerIndex(view, QStringLiteral("target")).isValid());
    QVERIFY(config.computerExists(QStringLiteral("target")));
    buttons(dialog)->button(QDialogButtonBox::Reset)->click();
    QVERIFY(computerIndex(view, QStringLiteral("target")).isValid());
    QVERIFY(dialog.addClient(QStringLiteral("another")));
    QVERIFY(!config.computerExists(QStringLiteral("another")));
    buttons(dialog)->button(QDialogButtonBox::Save)->click();
    QCOMPARE(dialog.result(), int(QDialog::Accepted));
    QVERIFY(config.computerExists(QStringLiteral("another")));
  }

  void readonlySettingsDisableEveryEditingEntry()
  {
    ServerConfig config;
    const QString fileName = Settings::settingsFile();
    struct RestorePermissions
    {
      QString path;
      QFileDevice::Permissions permissions;
      ~RestorePermissions()
      {
        QFile::setPermissions(path, permissions);
      }
    } restore{fileName, QFile::permissions(fileName)};
    QVERIFY(QFile::setPermissions(fileName, QFileDevice::ReadOwner | QFileDevice::ReadUser));
    if (Settings::isWritable())
      QSKIP("The current account bypasses file write permissions.");
    ServerConfigDialog dialog(nullptr, config);
    const QStringList names{QStringLiteral("btnAddComputer"),       QStringLiteral("computerSetupView"),
                            QStringLiteral("btnNewHotkey"),         QStringLiteral("cbEnableClipboard"),
                            QStringLiteral("sbClipboardSizeLimit"), QStringLiteral("cbHeartbeat"),
                            QStringLiteral("sbHeartbeat"),          QStringLiteral("cbSwitchDelay"),
                            QStringLiteral("sbSwitchDelay"),        QStringLiteral("rbProtocolBarrier"),
                            QStringLiteral("groupExternalConfig"),  QStringLiteral("lineConfigFile")};
    for (const QString &name : names)
      QVERIFY2(!dialog.findChild<QWidget *>(name)->isEnabled(), qPrintable(name));
    QVERIFY(!dialog.addClient(QStringLiteral("blocked")));
    QVERIFY(dialog.findChild<QPushButton *>(QStringLiteral("btnExportConfig"))->isEnabled());
    QVERIFY(!buttons(dialog)->button(QDialogButtonBox::Save)->isEnabled());
  }

private:
  static QDialogButtonBox *buttons(ServerConfigDialog &dialog)
  {
    return dialog.findChild<QDialogButtonBox *>();
  }

  static QModelIndex computerIndex(ComputerSetupView *view, const QString &name)
  {
    for (int row = 0; row < view->model()->rowCount(); ++row) {
      for (int column = 0; column < view->model()->columnCount(); ++column) {
        const auto index = view->model()->index(row, column);
        if (index.data().toString() == name)
          return index;
      }
    }
    return {};
  }

  QString configurationFile() const
  {
    const QString path = m_profiles.filePath(QStringLiteral("external.conf"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
      return {};
    file.write("section: links\nend\n");
    return path;
  }

  static QByteArray settingsBytes()
  {
    QFile file(Settings::settingsFile());
    if (!file.open(QIODevice::ReadOnly))
      return {};
    return file.readAll();
  }

  QTemporaryDir m_profiles;
  int m_profile = 0;
};

QTEST_MAIN(ServerSettingsDialogTests)
#include "ServerSettingsDialogTests.moc"
