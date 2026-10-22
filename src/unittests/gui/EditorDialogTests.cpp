/* SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception */
#include "common/Hotkey.h"
#include "common/Settings.h"
#include "gui/config/ComputerList.h"
#include "gui/config/ServerConfig.h"
#include "gui/dialogs/ActionDialog.h"
#include "gui/dialogs/ComputerSettingsDialog.h"
#include "gui/dialogs/HotkeyDialog.h"
#include "gui/widgets/KeySequenceWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

class EditorDialogTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase()
  {
    if (QGuiApplication::platformName() != QStringLiteral("offscreen") &&
        QGuiApplication::platformName() != QStringLiteral("minimal"))
      QSKIP("Editor regression tests require QT_QPA_PLATFORM=offscreen or minimal.");
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
    qputenv("XDG_CONFIG_HOME", m_files.filePath("isolated-config").toUtf8());
    qputenv("XDG_STATE_HOME", m_files.filePath("isolated-state").toUtf8());
    Settings::setStateFile(m_files.filePath("state.conf"));
  }

  void init()
  {
    const QString directory = m_files.filePath(QString::number(++m_profile));
    QVERIFY(QDir().mkpath(directory));
    Settings::setSettingsFile(directory + QStringLiteral("/settings.conf"));
    Settings::setValue(Settings::Core::ComputerName, QStringLiteral("source"));
    Settings::setValue(Settings::Server::GridWidth, 5);
    Settings::setValue(Settings::Server::GridHeight, 3);
    Settings::setValue(Settings::Computer::WeakX11Focus.arg(QStringLiteral("target")), true);
    Settings::save(false);
  }

  void existingHotkeyCanBeSavedWithoutRecordingAgain()
  {
    Hotkey hotkey;
    hotkey.setKeySequence(chord());
    HotkeyDialog dialog(nullptr, hotkey);
    const auto *recorder = dialog.findChild<KeySequenceWidget *>(QStringLiteral("hotkeyRecorder"));
    QVERIFY(recorder);
    QCOMPARE(recorder->keySequence(), chord());
    auto *save = dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
    QVERIFY(save->isEnabled());
    save->click();
    QCOMPARE(dialog.result(), int(QDialog::Accepted));
    QCOMPARE(hotkey.keySequence(), chord());
  }

  void hotkeyCancelDoesNotModifyTrigger()
  {
    Hotkey hotkey;
    hotkey.setKeySequence(chord());
    const Hotkey original = hotkey;
    HotkeyDialog dialog(nullptr, hotkey);
    KeySequence replacement;
    QVERIFY(replacement.appendKey(Qt::Key_F12, Qt::NoModifier));
    dialog.findChild<KeySequenceWidget *>()->setKeySequence(replacement);
    dialog.reject();
    QCOMPARE(hotkey, original);
  }

  void emptyHotkeyCannotBeSaved()
  {
    Hotkey hotkey;
    HotkeyDialog dialog(nullptr, hotkey);
    QVERIFY(!dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(&dialog, "accept", Qt::DirectConnection));
    QCOMPARE(dialog.result(), int(QDialog::Rejected));
    QVERIFY(hotkey.keySequence().toString().isEmpty());
  }

  void actionRoundTrip_data()
  {
    QTest::addColumn<int>("type");
    QTest::addColumn<bool>("release");
    for (int type = 0; type < 8; ++type) {
      QTest::newRow(qPrintable(QStringLiteral("action-%1-press").arg(type))) << type << false;
      QTest::newRow(qPrintable(QStringLiteral("action-%1-release").arg(type))) << type << true;
    }
  }

  void actionRoundTrip()
  {
    QFETCH(int, type);
    QFETCH(bool, release);
    ServerConfig config;
    config.addClient(QStringLiteral("target"));
    Hotkey hotkey;
    Action action = configuredAction(type, release);
    ActionDialog dialog(nullptr, config, hotkey, action);
    auto *types = dialog.findChild<QComboBox *>(QStringLiteral("comboActionType"));
    auto *trigger = dialog.findChild<QComboBox *>(QStringLiteral("comboTriggerOn"));
    auto *direction = dialog.findChild<QComboBox *>(QStringLiteral("comboSwitchInDirection"));
    auto *lock = dialog.findChild<QComboBox *>(QStringLiteral("comboLockCursorToComputer"));
    auto *target = dialog.findChild<QComboBox *>(QStringLiteral("comboSwitchToComputer"));
    auto *recorder = dialog.findChild<KeySequenceWidget *>();
    QVERIFY(types && trigger && direction && lock && target && recorder);
    QCOMPARE(types->count(), 9);
    QCOMPARE(types->currentIndex(), type);
    QCOMPARE(trigger->currentIndex(), int(release));
    QCOMPARE(direction->currentIndex(), 3);
    QCOMPARE(lock->currentIndex(), 2);
    QCOMPARE(target->currentData().toString(), QStringLiteral("target"));
    QCOMPARE(recorder->keySequence(), chord());

    trigger->setCurrentIndex(!release);
    direction->setCurrentIndex(2);
    lock->setCurrentIndex(1);
    auto *save = dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
    QVERIFY(save->isEnabled());
    save->click();
    QCOMPARE(dialog.result(), int(QDialog::Accepted));
    QCOMPARE(action.type(), type);
    QCOMPARE(action.activeOnRelease(), !release);
    QCOMPARE(action.keySequence(), chord());
    QCOMPARE(action.switchComputerName(), QStringLiteral("target"));
    QCOMPARE(action.switchDirection(), 2);
    QCOMPARE(action.lockCursorMode(), 1);
    QCOMPARE(action.restartServer(), type == ActionDialog::ActionTypes::RestartServer);
    QCOMPARE(action.tapAtTrigger(), type == ActionDialog::ActionTypes::ToggleKey);
    QCOMPARE(action.typeComputerNames(), QStringList{QStringLiteral("target")});
    QVERIFY(action.haveComputers());
  }

  void inspectingOrCancellingActionsDoesNotWriteConfiguration()
  {
    ServerConfig config;
    config.addClient(QStringLiteral("target"));
    const auto settings = settingsSnapshot();
    Hotkey hotkey;
    for (int type = 0; type < 8; ++type) {
      Action action = configuredAction(type, false);
      const Action original = action;
      ActionDialog dialog(nullptr, config, hotkey, action);
      dialog.findChild<QComboBox *>(QStringLiteral("comboActionType"))->setCurrentIndex((type + 1) % 8);
      dialog.findChild<QComboBox *>(QStringLiteral("comboTriggerOn"))->setCurrentIndex(1);
      dialog.reject();
      QVERIFY(action == original);
    }
    QCOMPARE(settingsSnapshot(), settings);
  }

  void keyboardActionNeedsAValidChordAndAtLeastOneTarget()
  {
    ServerConfig config;
    config.addClient(QStringLiteral("target"));
    Hotkey hotkey;
    Action action = configuredAction(2, false);
    ActionDialog dialog(nullptr, config, hotkey, action);
    auto *targets = dialog.findChild<QListWidget *>(QStringLiteral("actionTargetComputers"));
    QVERIFY(targets);
    for (int i = 0; i < targets->count(); ++i)
      targets->item(i)->setCheckState(Qt::Unchecked);
    QVERIFY(!dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(&dialog, "accept", Qt::DirectConnection));
    QCOMPARE(dialog.result(), int(QDialog::Rejected));
  }

  void legacyMouseActionCanBeSavedWithoutRecording_data()
  {
    QTest::addColumn<int>("type");
    QTest::newRow("mouseDown") << 8;
    QTest::newRow("mouseUp") << 9;
    QTest::newRow("mousebutton") << 10;
  }

  void legacyMouseActionCanBeSavedWithoutRecording()
  {
    QFETCH(int, type);
    ServerConfig config;
    config.addClient(QStringLiteral("target"));
    Hotkey hotkey;
    Action action = configuredAction(type, true);
    KeySequence mouse;
    QVERIFY(mouse.appendMouseButton(Qt::RightButton));
    action.setKeySequence(mouse);
    action.setTapAtTrigger(false);
    ActionDialog dialog(nullptr, config, hotkey, action);
    QCOMPARE(
        dialog.findChild<QComboBox *>(QStringLiteral("comboActionType"))->currentIndex(), type == 10 ? 8 : type - 8
    );
    QCOMPARE(dialog.findChild<KeySequenceWidget *>()->keySequence(), mouse);
    auto *save = dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
    QVERIFY(save->isEnabled());
    save->click();
    QCOMPARE(dialog.result(), int(QDialog::Accepted));
    QCOMPARE(action.type(), type);
    QCOMPARE(action.keySequence(), mouse);
    QCOMPARE(action.activeOnRelease(), type != 10);
    QVERIFY(!action.tapAtTrigger());
  }

  void existingHoldKeepsItsLifecycleAndCanExplicitlyBecomeATap_data()
  {
    QTest::addColumn<bool>("mouse");
    QTest::newRow("keyboard-hold") << false;
    QTest::newRow("mouse-drag") << true;
  }

  void existingHoldKeepsItsLifecycleAndCanExplicitlyBecomeATap()
  {
    QFETCH(bool, mouse);
    ServerConfig config;
    config.addClient(QStringLiteral("target"));
    Hotkey hotkey;
    Action action = configuredAction(mouse ? 10 : 2, true);
    action.setTapAtTrigger(false);
    if (mouse) {
      KeySequence button;
      QVERIFY(button.appendMouseButton(Qt::LeftButton));
      action.setKeySequence(button);
    }
    ActionDialog dialog(nullptr, config, hotkey, action);
    auto *type = dialog.findChild<QComboBox *>(QStringLiteral("comboActionType"));
    auto *trigger = dialog.findChild<QComboBox *>(QStringLiteral("comboTriggerOn"));
    QCOMPARE(type->currentIndex(), ActionDialog::ActionTypes::HoldWithHotkey);
    QCOMPARE(trigger->currentIndex(), 0);
    QVERIFY(!trigger->isEnabled());
    type->setCurrentIndex(ActionDialog::ActionTypes::ToggleKey);
    QVERIFY(trigger->isEnabled());
    trigger->setCurrentIndex(1);
    dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    QCOMPARE(dialog.result(), int(QDialog::Accepted));
    QCOMPARE(action.type(), mouse ? 10 : 2);
    QVERIFY(action.tapAtTrigger());
    QVERIFY(action.activeOnRelease());
  }

  void deviceEditorLoadsEveryOptionAndCancelIsReadOnly()
  {
    ConfiguredComputer computer;
    const Computer original = computer;
    const auto settings = settingsSnapshot();
    ComputerList computers;
    computers.append(computer);
    ComputerSettingsDialog dialog(nullptr, &computer, &computers);
    QCOMPARE(dialog.findChild<QLineEdit *>(QStringLiteral("lineNameEdit"))->text(), QStringLiteral("target"));
    QCOMPARE(dialog.findChild<QListWidget *>(QStringLiteral("listAliases"))->count(), 1);
    QCOMPARE(dialog.findChild<QSpinBox *>(QStringLiteral("sbSwitchCornerSize"))->value(), 127);
    const QStringList modifierNames = {QStringLiteral("Shift"), QStringLiteral("Ctrl"),  QStringLiteral("Alt"),
                                       QStringLiteral("Meta"),  QStringLiteral("Super"), QStringLiteral("AltGr")};
    for (int index = 0; index < modifierNames.size(); ++index) {
      auto *modifier = dialog.findChild<QComboBox *>(QStringLiteral("combo") + modifierNames.at(index));
      QVERIFY(modifier);
      QCOMPARE(modifier->count(), 7);
      QCOMPARE(modifier->currentIndex(), (index + 1) % 7);
      modifier->setCurrentIndex(index);
    }
    for (const auto &name :
         {"chkDeadTopLeft", "chkDeadTopRight", "chkDeadBottomLeft", "chkDeadBottomRight", "chkFixCapsLock",
          "chkFixNumLock", "chkFixScrollLock", "chkFixXTest", "chkWeakX11Focus"}) {
      auto *check = dialog.findChild<QCheckBox *>(QLatin1String(name));
      QVERIFY(check);
      QVERIFY(check->isChecked());
      check->setChecked(false);
    }
    dialog.findChild<QLineEdit *>(QStringLiteral("lineNameEdit"))->setText(QStringLiteral("renamed"));
    dialog.findChild<QListWidget *>(QStringLiteral("listAliases"))->clear();
    dialog.reject();
    QCOMPARE(static_cast<const Computer &>(computer), original);
    QCOMPARE(settingsSnapshot(), settings);
  }

  void rejectedAliasConflictLeavesDeviceUnchanged()
  {
    ConfiguredComputer computer;
    const Computer original = computer;
    ComputerList computers;
    computers.append(computer);
    ComputerSettingsDialog dialog(nullptr, &computer, &computers);
    dialog.findChild<QLineEdit *>(QStringLiteral("lineNameEdit"))->setText(QStringLiteral("old-target"));
    QTimer::singleShot(0, [] {
      if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
        message->accept();
    });
    dialog.accept();
    QCOMPARE(dialog.result(), int(QDialog::Rejected));
    dialog.reject();
    QCOMPARE(static_cast<const Computer &>(computer), original);
  }

  void captureEditorSurfaces()
  {
    const QString output = qEnvironmentVariable("DESKBRIDGE_UI_CAPTURE_DIR");
    if (output.isEmpty())
      QSKIP("Set DESKBRIDGE_UI_CAPTURE_DIR to capture editor surfaces.");
    QVERIFY(QDir().mkpath(output));
    const auto capture = [&](QDialog &dialog, const QString &name) {
      dialog.show();
      QApplication::processEvents();
      return dialog.grab().save(output + QLatin1Char('/') + name + QStringLiteral(".png"));
    };
    ConfiguredComputer computer;
    ComputerList computers;
    computers.append(computer);
    ComputerSettingsDialog device(nullptr, &computer, &computers);
    QVERIFY(capture(device, QStringLiteral("device-identity")));
    auto *scroll = device.findChild<QScrollArea *>();
    QVERIFY(scroll);
    scroll->ensureWidgetVisible(device.findChild<QComboBox *>(QStringLiteral("comboShift")), 0, 30);
    QVERIFY(capture(device, QStringLiteral("device-modifiers")));
    scroll->ensureWidgetVisible(device.findChild<QCheckBox *>(QStringLiteral("chkDeadTopLeft")), 0, 100);
    QVERIFY(capture(device, QStringLiteral("device-corners")));
    scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
    QVERIFY(capture(device, QStringLiteral("device-compatibility")));
    device.reject();

    Hotkey hotkey;
    hotkey.setKeySequence(chord());
    HotkeyDialog trigger(nullptr, hotkey);
    QVERIFY(capture(trigger, QStringLiteral("hotkey-trigger")));
    trigger.reject();

    ServerConfig config;
    config.addClient(QStringLiteral("target"));
    Action action = configuredAction(2, false);
    ActionDialog actions(nullptr, config, hotkey, action);
    auto *type = actions.findChild<QComboBox *>(QStringLiteral("comboActionType"));
    for (int i = 0; i < 9; ++i) {
      type->setCurrentIndex(i);
      QVERIFY(capture(actions, QStringLiteral("action-%1").arg(i)));
    }
    actions.reject();
  }

private:
  class ConfiguredComputer : public Computer
  {
  public:
    ConfiguredComputer() : Computer(QStringLiteral("target"))
    {
      addAlias(QStringLiteral("old-target"));
      setSwitchCornerSize(127);
      for (int i = 0; i < 6; ++i)
        setModifier(static_cast<KeyboardModifier>(i), (i + 1) % 7);
      for (int i = 0; i < 4; ++i) {
        setSwitchCorner(static_cast<ComputerConfig::SwitchCorner>(i), true);
        setFix(static_cast<ComputerConfig::Fix>(i), true);
      }
    }
  };

  KeySequence chord() const
  {
    KeySequence value;
    value.appendKey(Qt::Key_Control, Qt::ControlModifier);
    value.appendKey(Qt::Key_F9, Qt::ControlModifier);
    return value;
  }

  Action configuredAction(int type, bool release) const
  {
    Action value;
    value.setType(type);
    value.setTapAtTrigger(type == 2 || type == 10);
    value.setActiveOnRelease(release);
    value.setKeySequence(chord());
    value.setSwitchComputerName(QStringLiteral("target"));
    value.setSwitchDirection(3);
    value.setLockCursorMode(2);
    value.setHaveComputers(true);
    value.addComputer(QStringLiteral("target"));
    value.setRestartServer(type == ActionDialog::ActionTypes::RestartServer);
    return value;
  }

  QMap<QString, QVariant> settingsSnapshot() const
  {
    QMap<QString, QVariant> result;
    for (const auto &key : Settings::proxy().get().allKeys())
      result.insert(key, Settings::proxy().value(key));
    return result;
  }

  QTemporaryDir m_files;
  int m_profile = 0;
};

QTEST_MAIN(EditorDialogTests)
#include "EditorDialogTests.moc"
