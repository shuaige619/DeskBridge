/* SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception */
#include "../deskflow/MockEventQueue.h"
#include "arch/Arch.h"
#include "base/Log.h"
#include "common/Hotkey.h"
#include "common/Settings.h"
#include "deskflow/KeyTypes.h"
#include "server/Config.h"

#include <QDir>
#include <QTemporaryDir>
#include <QtTest>
#include <sstream>

using deskflow::server::Config;

class HotkeySerializationTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase()
  {
    m_arch.init();
    QVERIFY(m_files.isValid());
    qputenv("XDG_CONFIG_HOME", m_files.filePath("isolated-config").toUtf8());
    qputenv("XDG_STATE_HOME", m_files.filePath("isolated-state").toUtf8());
    Settings::setStateFile(m_files.filePath(QStringLiteral("state.conf")));
    Settings::setSettingsFile(m_files.filePath(QStringLiteral("settings.conf")));
    Settings::setValue(Settings::Core::ComputerName, QStringLiteral("source"));
    Settings::setValue(Settings::Computer::Name.arg(QStringLiteral("source")), QStringLiteral("source"));
    Settings::setValue(Settings::Computer::Name.arg(QStringLiteral("target")), QStringLiteral("target"));
    Settings::setValue(Settings::Core::Interface, QString());
  }

  void everyActionKeepsItsPhaseAndMeaningInTheCore_data()
  {
    QTest::addColumn<int>("type");
    QTest::addColumn<bool>("release");
    QTest::addColumn<bool>("mouseTrigger");
    for (int type = 0; type <= 10; ++type)
      for (bool release : {false, true})
        for (bool mouseTrigger : {false, true})
          QTest::newRow(qPrintable(QStringLiteral("type-%1-%2-%3").arg(type).arg(release).arg(mouseTrigger)))
              << type << release << mouseTrigger;
  }

  void everyActionKeepsItsPhaseAndMeaningInTheCore()
  {
    QFETCH(int, type);
    QFETCH(bool, release);
    QFETCH(bool, mouseTrigger);
    Hotkey hotkey;
    KeySequence trigger;
    if (mouseTrigger)
      QVERIFY(trigger.appendMouseButton(Qt::LeftButton));
    else
      QVERIFY(trigger.appendKey(Qt::Key_F9, Qt::NoModifier));
    hotkey.setKeySequence(trigger);
    Action action;
    action.setType(type);
    action.setActiveOnRelease(release);
    action.setTapAtTrigger(type == 2 || type == 10);
    action.setRestartServer(type == 7);
    action.setSwitchComputerName(QStringLiteral("target"));
    action.setSwitchDirection(2);
    action.setLockCursorMode(1);
    action.setHaveComputers(true);
    action.addComputer(QStringLiteral("target"));
    KeySequence output;
    output.appendKey(Qt::Key_Control, Qt::ControlModifier);
    if (type >= 8)
      QVERIFY(output.appendMouseButton(Qt::RightButton));
    else
      QVERIFY(output.appendKey(Qt::Key_F10, Qt::ControlModifier));
    action.setKeySequence(output);
    hotkey.addAction(action);
    Config config(&m_events);
    QString exported;
    QTextStream text(&exported);
    text << "section: options\n" << hotkey << "end\n";
    std::istringstream input(exported.toStdString());
    try {
      input >> config;
    } catch (const std::exception &error) {
      QFAIL(error.what());
    }
    auto *filter = config.getInputFilter();
    QCOMPARE(filter->getNumRules(), uint32_t(1));
    const auto &rule = filter->getRule(0);
    if (mouseTrigger) {
      const auto *condition = dynamic_cast<const InputFilter::MouseButtonCondition *>(rule.getCondition());
      QVERIFY(condition);
      QCOMPARE(condition->getButton(), ButtonID(1));
    } else {
      const auto *condition = dynamic_cast<const InputFilter::KeystrokeCondition *>(rule.getCondition());
      QVERIFY(condition);
      QCOMPARE(condition->getKey(), KeyID(kKeyF9));
    }
    const bool tap = type == 2 || type == 10;
    QCOMPARE(rule.getNumActions(!release), uint32_t(tap ? 2 : 1));
    QCOMPARE(rule.getNumActions(release), uint32_t(0));
    const auto &first = rule.getAction(!release, 0);
    if (type <= 2) {
      const auto *key = dynamic_cast<const InputFilter::KeystrokeAction *>(&first);
      QVERIFY(key);
      QCOMPARE(key->isOnPress(), type != 1);
      QCOMPARE(key->getInfo()->m_key, KeyID(kKeyF10));
      QCOMPARE(key->getInfo()->m_mask, KeyModifierMask(KeyModifierControl));
      QVERIFY(key->getInfo()->m_computers.find("target") != std::string::npos);
      if (tap) {
        const auto *up = dynamic_cast<const InputFilter::KeystrokeAction *>(&rule.getAction(!release, 1));
        QVERIFY(up);
        QVERIFY(!up->isOnPress());
        QCOMPARE(up->getInfo()->m_key, KeyID(kKeyF10));
      }
    } else if (type >= 8) {
      const auto *mouse = dynamic_cast<const InputFilter::MouseButtonAction *>(&first);
      QVERIFY(mouse);
      QCOMPARE(mouse->isOnPress(), type != 9);
      QCOMPARE(mouse->getInfo().m_button, ButtonID(2));
      QCOMPARE(mouse->getInfo().m_mask, KeyModifierMask(KeyModifierControl));
      if (tap) {
        const auto *up = dynamic_cast<const InputFilter::MouseButtonAction *>(&rule.getAction(!release, 1));
        QVERIFY(up);
        QVERIFY(!up->isOnPress());
        QCOMPARE(up->getInfo().m_button, ButtonID(2));
      }
    } else if (type == 3) {
      const auto *switchTo = dynamic_cast<const InputFilter::SwitchToComputerAction *>(&first);
      QVERIFY(switchTo);
      QCOMPARE(switchTo->getComputer(), std::string("target"));
    } else if (type == 4) {
      const auto *direction = dynamic_cast<const InputFilter::SwitchInDirectionAction *>(&first);
      QVERIFY(direction);
      QCOMPARE(direction->getDirection(), Direction::Top);
    } else if (type == 5) {
      QVERIFY(dynamic_cast<const InputFilter::SwitchToNextComputerAction *>(&first));
    } else if (type == 6) {
      const auto *lock = dynamic_cast<const InputFilter::LockCursorToComputerAction *>(&first);
      QVERIFY(lock);
      QCOMPARE(lock->getMode(), InputFilter::LockCursorToComputerAction::kOn);
    } else {
      QVERIFY(dynamic_cast<const InputFilter::RestartServer *>(&first));
    }
    // Never perform a parsed action: this test only inspects the core's rules.
  }

  void legacyKeyboardIndexedMouseOutputIsParsedAsAMouse_data()
  {
    QTest::addColumn<int>("type");
    QTest::newRow("mouse-down") << 0;
    QTest::newRow("mouse-up") << 1;
    QTest::newRow("mouse-tap") << 2;
  }

  void legacyKeyboardIndexedMouseOutputIsParsedAsAMouse()
  {
    QFETCH(int, type);
    Hotkey hotkey;
    KeySequence trigger;
    QVERIFY(trigger.appendKey(Qt::Key_F9, Qt::NoModifier));
    hotkey.setKeySequence(trigger);
    KeySequence button;
    QVERIFY(button.appendMouseButton(Qt::MiddleButton));
    Action action;
    action.setType(type);
    action.setTapAtTrigger(type == 2);
    action.setKeySequence(button);
    action.setRestartServer(false);
    hotkey.addAction(action);
    QString exported;
    QTextStream text(&exported);
    text << "section: options\n" << hotkey << "end\n";
    std::istringstream input(exported.toStdString());
    Config config(&m_events);
    input >> config;
    QCOMPARE(config.getInputFilter()->getNumRules(), uint32_t(1));
    const auto &rule = config.getInputFilter()->getRule(0);
    QCOMPARE(rule.getNumActions(true), uint32_t(type == 2 ? 2 : 1));
    QCOMPARE(rule.getNumActions(false), uint32_t(0));
    for (uint32_t i = 0; i < rule.getNumActions(true); ++i) {
      const auto *mouse = dynamic_cast<const InputFilter::MouseButtonAction *>(&rule.getAction(true, i));
      QVERIFY(mouse);
      QCOMPARE(mouse->getInfo().m_button, ButtonID(3));
      QCOMPARE(mouse->isOnPress(), type == 1 ? false : i == 0);
    }
  }

  void mixedPhaseOrderIsPreservedWithinEachPhase()
  {
    Hotkey hotkey;
    KeySequence trigger;
    QVERIFY(trigger.appendKey(Qt::Key_F9, Qt::NoModifier));
    hotkey.setKeySequence(trigger);
    for (int i = 0; i < 4; ++i) {
      Action action;
      action.setType(4);
      action.setSwitchDirection(i);
      action.setActiveOnRelease(i % 2 == 0);
      action.setRestartServer(false);
      hotkey.addAction(action);
    }
    QString exported;
    QTextStream text(&exported);
    text << "section: options\n" << hotkey << "end\n";
    std::istringstream input(exported.toStdString());
    Config config(&m_events);
    input >> config;
    const auto &rule = config.getInputFilter()->getRule(0);
    QCOMPARE(rule.getNumActions(true), uint32_t(2));
    QCOMPARE(rule.getNumActions(false), uint32_t(2));
    const Direction expectedPress[]{Direction::Right, Direction::Bottom};
    const Direction expectedRelease[]{Direction::Left, Direction::Top};
    for (uint32_t i = 0; i < 2; ++i) {
      const auto *press = dynamic_cast<const InputFilter::SwitchInDirectionAction *>(&rule.getAction(true, i));
      const auto *release = dynamic_cast<const InputFilter::SwitchInDirectionAction *>(&rule.getAction(false, i));
      QVERIFY(press && release);
      QCOMPARE(press->getDirection(), expectedPress[i]);
      QCOMPARE(release->getDirection(), expectedRelease[i]);
    }
  }

  void nativeExternalKeystrokeKeepsItsOriginalLifecycle()
  {
    std::istringstream input("section: options\nkeystroke(F9) = keystroke(F10,target)\nend\n");
    Config config(&m_events);
    input >> config;
    const auto &rule = config.getInputFilter()->getRule(0);
    QCOMPARE(rule.getNumActions(true), uint32_t(1));
    QCOMPARE(rule.getNumActions(false), uint32_t(1));
    const auto *down = dynamic_cast<const InputFilter::KeystrokeAction *>(&rule.getAction(true, 0));
    const auto *up = dynamic_cast<const InputFilter::KeystrokeAction *>(&rule.getAction(false, 0));
    QVERIFY(down && up);
    QVERIFY(down->isOnPress());
    QVERIFY(!up->isOnPress());
  }

  void oldPairedActionsRemainHeldUntilTriggerRelease_data()
  {
    QTest::addColumn<bool>("mouse");
    QTest::addColumn<bool>("oldReleaseFlag");
    QTest::newRow("old-keyboard-press") << false << false;
    QTest::newRow("old-keyboard-release") << false << true;
    QTest::newRow("old-mouse-press") << true << false;
    QTest::newRow("old-mouse-release") << true << true;
  }

  void oldPairedActionsRemainHeldUntilTriggerRelease()
  {
    QFETCH(bool, mouse);
    QFETCH(bool, oldReleaseFlag);
    Hotkey hotkey;
    KeySequence trigger;
    QVERIFY(trigger.appendKey(Qt::Key_F9, Qt::NoModifier));
    hotkey.setKeySequence(trigger);
    Action hold;
    hold.setType(mouse ? 10 : 2);
    hold.setActiveOnRelease(oldReleaseFlag);
    QVERIFY(!hold.tapAtTrigger());
    KeySequence output;
    if (mouse)
      QVERIFY(output.appendMouseButton(Qt::RightButton));
    else
      QVERIFY(output.appendKey(Qt::Key_F10, Qt::NoModifier));
    hold.setKeySequence(output);
    hotkey.addAction(hold);
    QString exported;
    QTextStream stream(&exported);
    stream << "section: options\n" << hotkey << "end\n";
    std::istringstream input(exported.toStdString());
    Config config(&m_events);
    input >> config;
    const auto &rule = config.getInputFilter()->getRule(0);
    QCOMPARE(rule.getNumActions(true), uint32_t(1));
    QCOMPARE(rule.getNumActions(false), uint32_t(1));
    if (mouse) {
      const auto *down = dynamic_cast<const InputFilter::MouseButtonAction *>(&rule.getAction(true, 0));
      const auto *up = dynamic_cast<const InputFilter::MouseButtonAction *>(&rule.getAction(false, 0));
      QVERIFY(down && up);
      QVERIFY(down->isOnPress());
      QVERIFY(!up->isOnPress());
      QCOMPARE(down->getInfo().m_button, ButtonID(2));
      QCOMPARE(up->getInfo().m_button, ButtonID(2));
    } else {
      const auto *down = dynamic_cast<const InputFilter::KeystrokeAction *>(&rule.getAction(true, 0));
      const auto *up = dynamic_cast<const InputFilter::KeystrokeAction *>(&rule.getAction(false, 0));
      QVERIFY(down && up);
      QVERIFY(down->isOnPress());
      QVERIFY(!up->isOnPress());
      QCOMPARE(down->getInfo()->m_key, KeyID(kKeyF10));
      QCOMPARE(up->getInfo()->m_key, KeyID(kKeyF10));
    }
  }

  void nativeMouseConditionAndHoldKeepTheirLifecycle()
  {
    std::istringstream input("section: options\nmousebutton(1) = mousebutton(Control+2)\nend\n");
    Config config(&m_events);
    input >> config;
    QCOMPARE(config.getInputFilter()->getNumRules(), uint32_t(1));
    const auto &rule = config.getInputFilter()->getRule(0);
    QVERIFY(dynamic_cast<const InputFilter::MouseButtonCondition *>(rule.getCondition()));
    QCOMPARE(rule.getNumActions(true), uint32_t(1));
    QCOMPARE(rule.getNumActions(false), uint32_t(1));
    const auto *down = dynamic_cast<const InputFilter::MouseButtonAction *>(&rule.getAction(true, 0));
    const auto *up = dynamic_cast<const InputFilter::MouseButtonAction *>(&rule.getAction(false, 0));
    QVERIFY(down && up);
    QVERIFY(down->isOnPress());
    QVERIFY(!up->isOnPress());
    QCOMPARE(down->getInfo().m_button, ButtonID(2));
    QCOMPARE(down->getInfo().m_mask, KeyModifierMask(KeyModifierControl));
  }

  void tapFlagPersistsAndAbsentFlagDefaultsToLegacyHold()
  {
    QSettings settings(m_files.filePath(QStringLiteral("action.conf")), QSettings::IniFormat);
    settings.clear();
    Action tap;
    KeySequence key;
    QVERIFY(key.appendKey(Qt::Key_F10, Qt::NoModifier));
    tap.setKeySequence(key);
    tap.setTapAtTrigger(true);
    tap.saveSettings(settings);
    Action reloaded;
    reloaded.loadSettings(settings);
    QVERIFY(reloaded.tapAtTrigger());
    QVERIFY(reloaded == tap);
    settings.remove(SettingsKeys::TapAtTrigger);
    Action legacy;
    legacy.loadSettings(settings);
    QVERIFY(!legacy.tapAtTrigger());
    QVERIFY(!(legacy == tap));
  }

private:
  Arch m_arch;
  Log m_log;
  MockEventQueue m_events;
  QTemporaryDir m_files;
};

QTEST_MAIN(HotkeySerializationTests)
#include "HotkeySerializationTests.moc"
