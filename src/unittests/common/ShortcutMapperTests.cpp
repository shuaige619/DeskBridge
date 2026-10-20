/* SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception */
#include "common/ShortcutMapper.h"
#include <QtTest>

namespace sc = deskflow::shortcuts;

class ShortcutMapperTests : public QObject
{
  Q_OBJECT
private:
  static sc::Configuration fullConfiguration(bool allApps = true)
  {
    auto config = sc::defaults();
    config.apps = {QStringLiteral("selected.app")};
    config.rules.clear();
    config.habit = sc::Habit::PC;
    config.fullShortcuts = true;
    config.allApplications = allApps;
    return config;
  }

private Q_SLOTS:
  void fullChords_data()
  {
    QTest::addColumn<KeyID>("key");
    QTest::addColumn<KeyModifierMask>("modifiers");
    QTest::addColumn<KeyModifierMask>("expected");
    for (const char key : {'a', 'c', 'v', 'x', 'z', 's', 'f', 'n', 'q', 't', 'w'})
      QTest::newRow(QByteArray(1, key).constData()) << KeyID(key) << KeyModifierControl << KeyModifierSuper;
    QTest::newRow("shift-uppercase") << KeyID('Z') << (KeyModifierControl | KeyModifierShift)
                                    << (KeyModifierSuper | KeyModifierShift);
    QTest::newRow("alt") << KeyID('s') << (KeyModifierControl | KeyModifierAlt)
                         << (KeyModifierSuper | KeyModifierAlt);
    QTest::newRow("locks") << KeyID('A') << (KeyModifierControl | KeyModifierCapsLock | KeyModifierNumLock)
                           << (KeyModifierSuper | KeyModifierCapsLock | KeyModifierNumLock);
    QTest::newRow("navigation") << KeyID(kKeyLeft) << KeyModifierControl << KeyModifierSuper;
    QTest::newRow("function") << KeyID(kKeyF5) << KeyModifierControl << KeyModifierSuper;
    QTest::newRow("reserved-input") << KeyID(' ') << KeyModifierControl << KeyModifierControl;
    QTest::newRow("reserved-with-lock") << KeyID(' ') << (KeyModifierControl | KeyModifierCapsLock)
                                        << (KeyModifierControl | KeyModifierCapsLock);
    QTest::newRow("plain-key") << KeyID('a') << KeyModifierMask(0) << KeyModifierMask(0);
    QTest::newRow("already-command") << KeyID('a') << KeyModifierSuper << KeyModifierSuper;
    QTest::newRow("both-modifiers") << KeyID('a') << (KeyModifierControl | KeyModifierSuper)
                                    << (KeyModifierControl | KeyModifierSuper);
    QTest::newRow("altgr") << KeyID('@') << (KeyModifierControl | KeyModifierAltGr)
                           << (KeyModifierControl | KeyModifierAltGr);
    QTest::newRow("meta") << KeyID('a') << (KeyModifierControl | KeyModifierMeta)
                          << (KeyModifierControl | KeyModifierMeta);
    QTest::newRow("level5") << KeyID('a') << (KeyModifierControl | KeyModifierLevel5Lock)
                            << (KeyModifierControl | KeyModifierLevel5Lock);
    for (const KeyID key : {kKeyControl_L, kKeyControl_R, kKeyShift_L, kKeyShift_R, kKeyAlt_L, kKeyAlt_R,
                            kKeySuper_L, kKeySuper_R, kKeyCapsLock, kKeyNumLock, kKeyScrollLock,
                            kKeyAltGr, kKeySetModifiers, kKeyClearModifiers, kKeyCompose}) {
      QTest::newRow(qPrintable(QStringLiteral("modifier-or-state-%1").arg(key)))
          << key << KeyModifierControl << KeyModifierControl;
    }
  }
  void fullChords()
  {
    QFETCH(KeyID, key);
    QFETCH(KeyModifierMask, modifiers);
    QFETCH(KeyModifierMask, expected);
    sc::ShortcutMapper mapper;
    mapper.configure(fullConfiguration(), sc::DeviceOS::Linux, sc::DeviceOS::MacOS);
    const auto output = mapper.mapKeyDown({key, modifiers}, 1, {});
    QCOMPARE(output.key, key);
    QCOMPARE(output.modifiers, expected);
  }
  void allApplicationScopeAndPreciseRulesRemainIndependent()
  {
    auto config = fullConfiguration();
    config.rules = {{"custom", "Custom", {'f', KeyModifierControl}, {'g', KeyModifierSuper}, true, config.apps}};
    sc::ShortcutMapper mapper;
    mapper.configure(config, sc::DeviceOS::Linux, sc::DeviceOS::MacOS);
    const sc::Chord input{'f', KeyModifierControl};
    QVERIFY(mapper.requiresApplication(input));
    QCOMPARE(mapper.mapKeyDown(input, 1, "selected.app"), (sc::Chord{'g', KeyModifierSuper}));
    QCOMPARE(mapper.mapKeyDown(input, 2, "other.app"), (sc::Chord{'f', KeyModifierSuper}));
    // A failed lookup cannot bypass a scoped override. Unrelated global chords
    // still work without consulting the foreground process.
    QCOMPARE(mapper.mapKeyDown(input, 3, {}), input);
    QVERIFY(!mapper.requiresApplication({'a', KeyModifierControl}));
    QCOMPARE(mapper.mapKeyDown({'a', KeyModifierControl}, 7, {}), (sc::Chord{'a', KeyModifierSuper}));
    config.allApplications = false;
    mapper.configure(config, sc::DeviceOS::Linux, sc::DeviceOS::MacOS);
    QVERIFY(mapper.requiresApplication({'a', KeyModifierControl}));
    QCOMPARE(mapper.mapKeyDown(input, 4, "other.app"), input);
    QCOMPARE(mapper.mapKeyDown(input, 5, {}), input);
    config.apps.clear();
    mapper.configure(config, sc::DeviceOS::Linux, sc::DeviceOS::MacOS);
    QCOMPARE(mapper.mapKeyDown(input, 6, "selected.app"), input);
  }
  void disabledRuleIsAnExceptionWithinItsOwnScope()
  {
    auto config = fullConfiguration();
    config.rules = {{"native", "Keep native", {'f', KeyModifierControl}, {'f', KeyModifierSuper}, false, config.apps}};
    sc::ShortcutMapper mapper;
    mapper.configure(config, sc::DeviceOS::Linux, sc::DeviceOS::MacOS);
    const sc::Chord input{'f', KeyModifierControl};
    QVERIFY(mapper.requiresApplication(input));
    QCOMPARE(mapper.mapKeyDown(input, 1, "selected.app"), input);
    QCOMPARE(mapper.mapKeyDown(input, 2, "other.app"), (sc::Chord{'f', KeyModifierSuper}));
    QCOMPARE(mapper.mapKeyDown(input, 4, {}), input);
    config.rules.append({"explicit", "Explicit", input, {'g', KeyModifierSuper}, true, config.apps});
    mapper.configure(config, sc::DeviceOS::Linux, sc::DeviceOS::MacOS);
    QCOMPARE(mapper.mapKeyDown(input, 3, "selected.app"), (sc::Chord{'g', KeyModifierSuper}));
  }
  void heldDecisionsSurviveScopeDirectionAndSettingsChanges()
  {
    auto config = fullConfiguration(false);
    sc::ShortcutMapper mapper;
    mapper.configure(config, sc::DeviceOS::Linux, sc::DeviceOS::MacOS);
    const sc::Chord input{'Z', KeyModifierControl | KeyModifierShift};
    const sc::Chord output{'Z', KeyModifierSuper | KeyModifierShift};
    QCOMPARE(mapper.mapKeyDown(input, 1, "selected.app"), output);
    QCOMPARE(mapper.mapKeyDown(input, 2, "other.app"), input);
    config.enabled = false;
    mapper.configure(config, sc::DeviceOS::MacOS, sc::DeviceOS::MacOS);
    QCOMPARE(mapper.mapKeyRepeat(input, 1), output);
    QCOMPARE(mapper.mapKeyDown(input, 1, "other.app"), output);
    QCOMPARE(mapper.mapKeyDown(input, 2, "selected.app"), input);
    mapper.keyUp(1);
    QCOMPARE(mapper.mapKeyDown(input, 1, "selected.app"), input);
    mapper.reset();
    QVERIFY(!mapper.isKeyDown(1));
    QVERIFY(!mapper.isKeyDown(2));
  }
  void automaticDirectionUsesDetectedPeerInsteadOfOldFallback()
  {
    auto config = fullConfiguration();
    config.habit = sc::Habit::Automatic;
    config.sourceOS = sc::DeviceOS::Linux;
    sc::ShortcutMapper mapper;
    mapper.configure(config, sc::DeviceOS::MacOS, sc::DeviceOS::MacOS);
    QCOMPARE(mapper.mapKeyDown({'a', KeyModifierControl}, 1, {}), (sc::Chord{'a', KeyModifierControl}));
    mapper.configure(config, sc::DeviceOS::MacOS, sc::DeviceOS::Windows);
    QCOMPARE(mapper.mapKeyDown({'a', KeyModifierSuper}, 2, {}), (sc::Chord{'a', KeyModifierControl}));
    mapper.configure(config, sc::DeviceOS::Unknown, sc::DeviceOS::MacOS);
    QCOMPARE(mapper.mapKeyDown({'a', KeyModifierControl}, 3, {}), (sc::Chord{'a', KeyModifierSuper}));
    config.sourceOS = sc::DeviceOS::Unknown;
    mapper.configure(config, sc::DeviceOS::Unknown, sc::DeviceOS::MacOS);
    QCOMPARE(mapper.mapKeyDown({'a', KeyModifierControl}, 4, {}), (sc::Chord{'a', KeyModifierControl}));
  }
  void disabledAndInvalidModesDoNotMap()
  {
    auto config = fullConfiguration();
    sc::ShortcutMapper mapper;
    config.enabled = false;
    mapper.configure(config, sc::DeviceOS::Linux, sc::DeviceOS::MacOS);
    const sc::Chord input{'a', KeyModifierControl};
    QCOMPARE(mapper.mapKeyDown(input, 1, {}), input);
    config.enabled = true;
    config.habit = sc::Habit::Custom;
    mapper.configure(config, sc::DeviceOS::Linux, sc::DeviceOS::MacOS);
    QCOMPARE(mapper.mapKeyDown(input, 2, {}), input);
    mapper.configure(fullConfiguration(), sc::DeviceOS::Linux, sc::DeviceOS::MacOS);
    QCOMPARE(mapper.mapKeyDown(input, 0, {}), input);
    QVERIFY(!mapper.isKeyDown(0));
  }
};

QTEST_GUILESS_MAIN(ShortcutMapperTests)
#include "ShortcutMapperTests.moc"
