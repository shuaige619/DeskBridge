/*
 * SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "ShortcutRulesTests.h"

#include "common/Settings.h"
#include "common/ShortcutRules.h"
#include "common/SourceDeviceOptions.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTest>

using namespace deskflow::shortcuts;

void ShortcutRulesTests::initTestCase()
{
  QVERIFY(m_temp.isValid());
  // Settings creates its default instance before selecting an explicit profile.
  // Keep that initial profile and the state file isolated too.
  qputenv("XDG_CONFIG_HOME", (m_temp.path() + "/config").toUtf8());
  qputenv("XDG_STATE_HOME", (m_temp.path() + "/state").toUtf8());
}

void ShortcutRulesTests::init()
{
  const auto directory = m_temp.filePath(QStringLiteral("profile-%1").arg(m_profile++));
  QVERIFY(QDir().mkpath(directory));
  Settings::setSettingsFile(directory + QStringLiteral("/test.conf"));
  Settings::setStateFile(directory + QStringLiteral("/test.state"));
}

void ShortcutRulesTests::roundTrip()
{
  auto config = defaults();
  config.rules.push_back({QStringLiteral("custom"), QStringLiteral("自定义导航"), {kKeyF24, KeyModifierAlt},
                          {kKeyHome, KeyModifierSuper | KeyModifierShift}, false, config.apps});
  QString error = QStringLiteral("old error");
  const auto parsed = fromJson(toJson(config), &error);
  QVERIFY2(parsed.has_value(), qPrintable(error));
  QVERIFY(error.isEmpty());
  QVERIFY(*parsed == config);
  QVERIFY2(save(config, &error), qPrintable(error));
  QVERIFY(load(&error) == config);
  QVERIFY(error.isEmpty());
}

void ShortcutRulesTests::invalidJson_data()
{
  QTest::addColumn<QString>("json");
  const auto base = QJsonDocument::fromJson(toJson(defaults()).toUtf8()).object();
  const auto add = [](const char *name, const QJsonObject &object) {
    QTest::newRow(name) << QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
  };
  QTest::newRow("malformed") << QStringLiteral("{");
  QTest::newRow("empty-string") << QString();
  QTest::newRow("empty-object") << QStringLiteral("{}");
  QTest::newRow("array") << QStringLiteral("[]");
  auto object = base;
  object.remove("apps"); add("missing", object);
  object = base; object.insert("extra", true); add("unknown", object);
  object = base; object.insert("schema", 5); add("schema-version", object);
  object = base; object.insert("schema", "1"); add("schema-type", object);
  object = base; object.insert("enabled", "true"); add("enabled-type", object);
  object = base; object.insert("habit", "future"); add("habit-unknown", object);
  object = base; object.insert("habit", 1); add("habit-type", object);
  object = base; object.insert("sourceOS", "darwin"); add("source-unknown", object);
  object = base; object.insert("sourceOS", false); add("source-type", object);
  object = base; object.remove("fullShortcuts"); add("missing-full-shortcuts", object);
  object = base; object.remove("allApplications"); add("missing-all-applications", object);
  object = base; object.insert("fullShortcuts", "true"); add("full-shortcuts-string", object);
  object = base; object.insert("fullShortcuts", 1); add("full-shortcuts-number", object);
  object = base; object.insert("allApplications", "false"); add("all-applications-string", object);
  object = base; object.insert("allApplications", QJsonArray{}); add("all-applications-array", object);
  object = base; object.remove("shiftTapInputMethod"); add("missing-shift-tap", object);
  object = base; object.insert("shiftTapInputMethod", "true"); add("shift-tap-string", object);
  object = base; object.insert("shiftTapInputMethod", 1); add("shift-tap-number", object);
  object = base; object.insert("fullShortcuts", true); add("custom-full-shortcuts", object);
  object = base; object.insert("apps", "com.test.app"); add("apps-type", object);
  object = base; object.insert("apps", QJsonArray{42}); add("app-element-type", object);
  object = base; object.insert("rules", QJsonArray{false}); add("rule-type", object);
  const auto rule = base.value("rules").toArray().first().toObject();
  const auto addRule = [&base, &add](const char *name, const QJsonObject &badRule) {
    auto object = base;
    object.insert("rules", QJsonArray{badRule});
    add(name, object);
  };
  auto badRule = rule; badRule.remove("name"); addRule("rule-missing", badRule);
  badRule = rule; badRule.insert("enabled", 1); addRule("rule-enabled-type", badRule);
  badRule = rule; badRule.insert("apps", QJsonArray{false}); addRule("rule-apps-type", badRule);
  for (const auto [name, key] : {std::pair{"key-zero", 0.0}, std::pair{"key-negative", -1.0},
                                std::pair{"key-fractional", 99.5}, std::pair{"key-unsupported", 0x10ffff * 1.0},
                                std::pair{"key-overflow", 4294967296.0}}) {
    badRule = rule;
    auto input = rule.value("input").toObject(); input.insert("key", key); badRule.insert("input", input);
    addRule(name, badRule);
  }
  badRule = rule;
  auto input = rule.value("input").toObject(); input.insert("modifiers", 2.5); badRule.insert("input", input);
  addRule("modifier-fractional", badRule);
  input.insert("modifiers", static_cast<int>(KeyModifierMeta)); badRule.insert("input", input); addRule("modifier-unsupported", badRule);
  input.insert("modifiers", static_cast<int>(KeyModifierCapsLock)); badRule.insert("input", input); addRule("lock-in-rule", badRule);
  input = rule.value("input").toObject(); input.insert("extra", true); badRule.insert("input", input);
  addRule("chord-extra", badRule);
  badRule = rule; badRule.insert("output", QJsonObject{{"key", "c"}, {"modifiers", 16}});
  addRule("output-key-type", badRule);
}

void ShortcutRulesTests::invalidJson()
{
  QFETCH(QString, json);
  QString error;
  QVERIFY(!fromJson(json, &error));
  QVERIFY(!error.isEmpty());
}

void ShortcutRulesTests::emptyListsRemainEmpty()
{
  Configuration config;
  config.apps.clear();
  config.rules.clear();
  QString error;
  QVERIFY(save(config, &error));
  QVERIFY(load(&error) == config);
  QVERIFY(error.isEmpty());
  QVERIFY(Settings::value(Settings::Client::ShortcutConfig).toString().contains(QStringLiteral("\"rules\":[]")));
}

void ShortcutRulesTests::migrationPreservesLegacyPreferences()
{
  Settings::setValue(Settings::Client::ClipboardShortcuts, false);
  Settings::setValue(Settings::Client::ClipboardShortcutApps, QStringList{QStringLiteral("com.test.custom")});
  auto config = load();
  QVERIFY(!config.enabled);
  QCOMPARE(config.apps, QStringList{QStringLiteral("com.test.custom")});
  QCOMPARE(config.rules.size(), 2);
  QCOMPARE(config.rules.first().apps, config.apps);
  QVERIFY(Settings::value(Settings::Client::ShortcutConfig).toString().isEmpty());
  Settings::setValue(Settings::Client::ClipboardShortcuts, true);
  Settings::setValue(Settings::Client::ClipboardShortcutApps, QStringList{});
  config = load();
  QVERIFY(config.enabled);
  QVERIFY(config.apps.isEmpty());
  QVERIFY(config.rules.first().apps.isEmpty());
}

void ShortcutRulesTests::malformedConfigurationFailsClosed()
{
  Settings::setValue(Settings::Client::ShortcutConfig, QStringLiteral("{}"));
  QString error;
  const auto config = load(&error);
  QVERIFY(!error.isEmpty());
  QVERIFY(!config.enabled);
  QVERIFY(config.apps.isEmpty());
  QVERIFY(config.rules.isEmpty());
  QCOMPARE(Settings::value(Settings::Client::ShortcutConfig).toString(), QStringLiteral("{}"));
}

void ShortcutRulesTests::saveRejectsInvalidWithoutChangingCommittedValue()
{
  QVERIFY(save(defaults()));
  const auto previous = Settings::value(Settings::Client::ShortcutConfig);
  auto config = defaults();
  config.rules.first().input = {' ', KeyModifierControl};
  QSignalSpy changes(Settings::instance(), &Settings::settingsChanged);
  QString error;
  QVERIFY(!save(config, &error));
  QVERIFY(!error.isEmpty());
  QCOMPARE(Settings::value(Settings::Client::ShortcutConfig), previous);
  QCOMPARE(changes.count(), 0);
}

void ShortcutRulesTests::failedDiskWriteRestoresCommittedValue()
{
  QVERIFY(save(defaults()));
  const auto previous = Settings::value(Settings::Client::ShortcutConfig);
  const auto directory = QFileInfo(Settings::settingsFile()).absolutePath();
  const auto permissions = QFileInfo(directory).permissions();
  QVERIFY(QFile::setPermissions(directory, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
  const auto restore = qScopeGuard([directory, permissions] { QFile::setPermissions(directory, permissions); });
  auto config = defaults(); config.enabled = false;
  QString error;
  QSignalSpy changes(Settings::instance(), &Settings::settingsChanged);
  QVERIFY(!save(config, &error));
  QVERIFY(!error.isEmpty());
  QCOMPARE(Settings::value(Settings::Client::ShortcutConfig), previous);
  QCOMPARE(changes.count(), 0);
  QVERIFY(QFile::setPermissions(directory, permissions));
  QVERIFY2(save(config, &error), qPrintable(error));
  QVERIFY(load() == config);
}

void ShortcutRulesTests::conflictsUseEffectiveAppIntersection()
{
  auto config = defaults();
  config.apps = {QStringLiteral("com.openai.codex")};
  auto duplicate = config.rules.first();
  duplicate.id = QStringLiteral("duplicate");
  duplicate.input.key = 'C';
  duplicate.apps = {QStringLiteral("com.electron.lark")};
  config.rules.push_back(duplicate);
  QVERIFY(validate(config).isEmpty()); // Lark isn't currently in the global boundary.
  config.apps.push_back(QStringLiteral("com.electron.lark"));
  QVERIFY(!validate(config).isEmpty());
  config.rules.last().enabled = false;
  QVERIFY(validate(config).isEmpty());
  config.rules.last().enabled = true;
  config.rules.last().apps.clear();
  QVERIFY(validate(config).isEmpty());
  config.rules.last().id = config.rules.first().id;
  QVERIFY(!validate(config).isEmpty());
}

void ShortcutRulesTests::reservedInputIsAlwaysProtected()
{
  QVERIFY(isReservedInput({' ', KeyModifierControl}));
  QVERIFY(isReservedInput({' ', KeyModifierControl | KeyModifierCapsLock}));
  QVERIFY(!isReservedInput({' ', KeyModifierControl | KeyModifierShift}));
  auto config = defaults();
  config.enabled = false;
  config.rules.first().enabled = false;
  config.rules.first().input = {' ', KeyModifierControl};
  QVERIFY(!validate(config).isEmpty());
}

void ShortcutRulesTests::macOutputFunctionKeyLimit()
{
  auto config = defaults();
  config.rules.first().input = {kKeyF24, KeyModifierControl};
  config.rules.first().output = {kKeyF20, KeyModifierSuper};
  QVERIFY(validate(config).isEmpty());
  QVERIFY(save(config));
  const auto committed = Settings::value(Settings::Client::ShortcutConfig);
  for (auto key = kKeyF21; key <= kKeyF24; ++key) {
    config.rules.first().output.key = key;
#ifdef Q_OS_MACOS
    const auto error = validate(config);
    QVERIFY(error.contains(QStringLiteral("F1–F20")));
    QVERIFY(!fromJson(toJson(config)));
    QVERIFY(!save(config));
    QCOMPARE(Settings::value(Settings::Client::ShortcutConfig), committed);
#else
    QVERIFY(validate(config).isEmpty());
#endif
  }
}

void ShortcutRulesTests::displayKeys_data()
{
  QTest::addColumn<KeyID>("key");
  QTest::addColumn<KeyModifierMask>("modifiers");
  QTest::addColumn<QString>("expected");
  QTest::newRow("all-modifiers") << KeyID('c') << (KeyModifierControl | KeyModifierShift | KeyModifierAlt | KeyModifierSuper)
                                 << QStringLiteral("Ctrl+Shift+Alt+⌘+C");
  QTest::newRow("space") << KeyID(' ') << KeyModifierControl << QStringLiteral("Ctrl+Space");
  QTest::newRow("F1") << kKeyF1 << KeyModifierMask(0) << QStringLiteral("F1");
  QTest::newRow("F24") << kKeyF24 << KeyModifierAlt << QStringLiteral("Alt+F24");
  QTest::newRow("left") << kKeyLeft << KeyModifierSuper << QStringLiteral("⌘+←");
  QTest::newRow("page-down") << kKeyPageDown << KeyModifierMask(0) << QStringLiteral("Page Down");
}

void ShortcutRulesTests::displayKeys()
{
  QFETCH(KeyID, key); QFETCH(KeyModifierMask, modifiers); QFETCH(QString, expected);
  QCOMPARE(chordText({key, modifiers}), expected);
}

void ShortcutRulesTests::schemaOneMigratesWithoutWriting()
{
  auto original = defaults();
  original.enabled = false;
  auto object = QJsonDocument::fromJson(toJson(original).toUtf8()).object();
  QCOMPARE(object.value("schema").toInt(), 4);
  object.insert("schema", 1);
  object.remove("habit");
  object.remove("sourceOS");
  object.remove("fullShortcuts");
  object.remove("allApplications");
  object.remove("shiftTapInputMethod");
  const auto raw = QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
  Settings::setValue(Settings::Client::ShortcutConfig, raw);
  QString error;
  const auto migrated = load(&error);
  QVERIFY2(error.isEmpty(), qPrintable(error));
  QCOMPARE(migrated, original);
  QCOMPARE(migrated.habit, Habit::Custom);
  QCOMPARE(migrated.sourceOS, DeviceOS::Unknown);
  QCOMPARE(Settings::value(Settings::Client::ShortcutConfig).toString(), raw);
  for (const auto habit : {Habit::Custom, Habit::Automatic, Habit::Mac, Habit::PC}) {
    auto current = original;
    current.habit = habit;
    current.sourceOS = DeviceOS::Windows;
    const auto decoded = fromJson(toJson(current), &error);
    QVERIFY2(decoded.has_value(), qPrintable(error));
    QCOMPARE(*decoded, current);
  }
}

void ShortcutRulesTests::habitDirectionMatrix_data()
{
  QTest::addColumn<Habit>("habit");
  QTest::addColumn<DeviceOS>("source");
  QTest::addColumn<DeviceOS>("target");
  for (const auto habit : {Habit::Custom, Habit::Automatic, Habit::Mac, Habit::PC}) {
    for (const auto source : {DeviceOS::MacOS, DeviceOS::Windows, DeviceOS::Linux}) {
      for (const auto target : {DeviceOS::MacOS, DeviceOS::Windows, DeviceOS::Linux}) {
        const auto row = habitName(habit) + '-' + deviceOSName(source) + '-' + deviceOSName(target);
        QTest::newRow(qPrintable(row)) << habit << source << target;
      }
    }
  }
}

void ShortcutRulesTests::habitDirectionMatrix()
{
  QFETCH(Habit, habit);
  QFETCH(DeviceOS, source);
  QFETCH(DeviceOS, target);
  Configuration config;
  config.apps = {QStringLiteral("com.test.editor")};
  config.rules = defaultRules(config.apps);
  config.rules.push_back({"navigation", "导航", {kKeyF9, KeyModifierAlt}, {kKeyHome, 0}, true, config.apps});
  config.habit = habit;
  const auto before = config;
  const auto effective = effectiveConfiguration(config, source, target);
  QCOMPARE(config, before); // Resolution is pure and never rewrites preferences.
  if (habit == Habit::Custom) {
    QCOMPARE(effective, config);
    return;
  }
  const bool macHabit = habit == Habit::Mac || (habit == Habit::Automatic && source == DeviceOS::MacOS);
  const bool macTarget = target == DeviceOS::MacOS;
  QCOMPARE(effective.rules.last(), config.rules.last());
  QCOMPARE(effective.apps, config.apps);
  QCOMPARE(effective.enabled, config.enabled);
  if (macHabit == macTarget) {
    QCOMPARE(effective.rules.size(), 1);
  } else {
    QCOMPARE(effective.rules.size(), 3);
    for (int i = 0; i < 2; ++i) {
      const KeyID key = i ? 'v' : 'c';
      QCOMPARE(effective.rules[i].input, (Chord{key, macHabit ? KeyModifierSuper : KeyModifierControl}));
      QCOMPARE(effective.rules[i].output, (Chord{key, macTarget ? KeyModifierSuper : KeyModifierControl}));
    }
    QVERIFY2(validate(effective).isEmpty(), qPrintable(validate(effective)));
  }
}

void ShortcutRulesTests::sourceFallbackAndUnknown()
{
  auto config = defaults();
  config.habit = Habit::Automatic;
  QCOMPARE(effectiveSourceOS(config, DeviceOS::Unknown), DeviceOS::Unknown);
  QVERIFY(effectiveConfiguration(config, DeviceOS::Unknown, DeviceOS::MacOS).rules.isEmpty());
  config.sourceOS = DeviceOS::Windows;
  QCOMPARE(effectiveSourceOS(config, DeviceOS::Unknown), DeviceOS::Windows);
  QCOMPARE(effectiveConfiguration(config, DeviceOS::Unknown, DeviceOS::MacOS).rules.size(), 2);
  QCOMPARE(effectiveSourceOS(config, DeviceOS::MacOS), DeviceOS::MacOS);
  QVERIFY(effectiveConfiguration(config, DeviceOS::MacOS, DeviceOS::MacOS).rules.isEmpty());
  QVERIFY(effectiveConfiguration(config, DeviceOS::Windows, DeviceOS::Unknown).rules.isEmpty());
  QVERIFY(!habitDescription(config, DeviceOS::Unknown, DeviceOS::MacOS).isEmpty());
  config.sourceOS = DeviceOS::Unknown;
  QVERIFY(habitDescription(config).contains(QStringLiteral("未知")));
  config.habit = Habit::PC;
  QCOMPARE(effectiveConfiguration(config, DeviceOS::Unknown, DeviceOS::MacOS).rules.size(), 2);
  config.habit = Habit::Mac;
  QCOMPARE(effectiveConfiguration(config, DeviceOS::Unknown, DeviceOS::Windows).rules.size(), 2);
}

void ShortcutRulesTests::habitPreservesRulePreferences()
{
  Configuration config;
  config.apps = {"com.test.editor"};
  config.rules = defaultRules(config.apps);
  config.rules.removeLast();
  config.rules[0].enabled = false;
  config.rules[0].name = QStringLiteral("个人复制");
  config.rules[0].apps = {"removed.application"};
  config.rules.push_back({"custom", "自定义", {kKeyF1, KeyModifierControl}, {kKeyF2, KeyModifierShift}, true, config.apps});
  const auto copy = config.rules[0];
  const auto custom = config.rules[1];
  setHabit(config, Habit::Mac);
  QCOMPARE(config.rules.size(), 3);
  QCOMPARE(config.rules[0], copy);
  QCOMPARE(config.rules[1], custom);
  QCOMPARE(config.rules.last().id, QStringLiteral("paste"));
  const auto effective = effectiveConfiguration(config, DeviceOS::Windows, DeviceOS::Windows);
  QVERIFY(!effective.rules[0].enabled);
  QCOMPARE(effective.rules[0].apps, copy.apps);
  QCOMPARE(effective.rules[0].name, copy.name);
  QCOMPARE(effective.rules[1], custom);
  setHabit(config, Habit::Custom);
  QCOMPARE(config.rules[0], copy);
  QCOMPARE(config.rules[1], custom);
}

void ShortcutRulesTests::sourceMetadata_data()
{
  QTest::addColumn<OptionsList>("options");
  QTest::addColumn<DeviceOS>("expected");
  for (const auto os : {DeviceOS::Unknown, DeviceOS::MacOS, DeviceOS::Windows, DeviceOS::Linux})
    QTest::newRow(qPrintable(deviceOSName(os)))
        << OptionsList{kOptionDeskBridgeVersion, 1, kOptionDeskBridgeSourceOS, static_cast<uint32_t>(os)} << os;
  QTest::newRow("legacy") << OptionsList{kOptionClipboardSharing, 1} << DeviceOS::Unknown;
  QTest::newRow("no-version") << OptionsList{kOptionDeskBridgeSourceOS, 2} << DeviceOS::Unknown;
  QTest::newRow("no-os") << OptionsList{kOptionDeskBridgeVersion, 1} << DeviceOS::Unknown;
  QTest::newRow("future") << OptionsList{kOptionDeskBridgeVersion, 2, kOptionDeskBridgeSourceOS, 1} << DeviceOS::Unknown;
  QTest::newRow("bad-os") << OptionsList{kOptionDeskBridgeVersion, 1, kOptionDeskBridgeSourceOS, 999} << DeviceOS::Unknown;
  QTest::newRow("odd") << OptionsList{kOptionDeskBridgeVersion, 1, kOptionDeskBridgeSourceOS} << DeviceOS::Unknown;
  QTest::newRow("duplicate-version") << OptionsList{kOptionDeskBridgeVersion, 1, kOptionDeskBridgeVersion, 1, kOptionDeskBridgeSourceOS, 1} << DeviceOS::Unknown;
  QTest::newRow("duplicate-os") << OptionsList{kOptionDeskBridgeVersion, 1, kOptionDeskBridgeSourceOS, 1, kOptionDeskBridgeSourceOS, 2} << DeviceOS::Unknown;
  QTest::newRow("reordered-with-other-options") << OptionsList{kOptionDeskBridgeSourceOS, 3, kOptionHeartbeat, 5000, kOptionDeskBridgeVersion, 1} << DeviceOS::Linux;
  QTest::newRow("value-is-not-an-id") << OptionsList{kOptionHeartbeat, kOptionDeskBridgeVersion, kOptionDeskBridgeSourceOS, 1} << DeviceOS::Unknown;
}

void ShortcutRulesTests::sourceMetadata()
{
  QFETCH(OptionsList, options);
  QFETCH(DeviceOS, expected);
  QCOMPARE(sourceOSFromOptions(options), expected);
}

void ShortcutRulesTests::platformModifierLabels()
{
  QCOMPARE(chordText({'c', KeyModifierSuper}, DeviceOS::MacOS), QStringLiteral("⌘+C"));
  QCOMPARE(chordText({'c', KeyModifierSuper}, DeviceOS::Windows), QStringLiteral("Win+C"));
  QCOMPARE(chordText({'c', KeyModifierSuper}, DeviceOS::Linux), QStringLiteral("Super+C"));
  QCOMPARE(chordText({'c', KeyModifierControl}, DeviceOS::MacOS), QStringLiteral("Ctrl+C"));
  QCOMPARE(chordText({'c', KeyModifierSuper}), chordText({'c', KeyModifierSuper}, localOS()));
}

void ShortcutRulesTests::presetConflictsUseEffectiveChords()
{
  Configuration config;
  config.apps = {"test.editor"};
  config.rules = defaultRules(config.apps);
  const auto nativeModifier = localOS() == DeviceOS::MacOS ? KeyModifierSuper : KeyModifierControl;
  const auto foreignModifier = nativeModifier == KeyModifierSuper ? KeyModifierControl : KeyModifierSuper;
  config.rules[0].input = {'c', KeyModifierAlt}; // No conflict in the stored chords.
  config.rules.push_back({"custom", "自定义复制", {'c', foreignModifier}, {kKeyHome, 0}, true, config.apps});
  config.habit = foreignModifier == KeyModifierControl ? Habit::PC : Habit::Mac;
  QVERIFY(!validate(config).isEmpty());
  config.habit = Habit::Automatic;
  QVERIFY(!validate(config).isEmpty());
  config.habit = nativeModifier == KeyModifierControl ? Habit::PC : Habit::Mac;
  QVERIFY(validate(config).isEmpty()); // Native-habit presets are absent at runtime.
  config.habit = Habit::Custom;
  QVERIFY(validate(config).isEmpty()); // Legacy/custom behavior remains unchanged.
  config.habit = Habit::Automatic;
  config.rules.last().enabled = false;
  QVERIFY(validate(config).isEmpty());
  config.rules.last().enabled = true;
  config.rules.last().apps = {"outside.global.boundary"};
  QVERIFY(validate(config).isEmpty());
}

void ShortcutRulesTests::schemaTwoMigratesWithoutWriting()
{
  auto original = defaults();
  original.habit = Habit::Automatic;
  original.sourceOS = DeviceOS::Windows;
  original.enabled = false;
  original.rules.first().enabled = false;
  const auto beforeApps = original.apps;
  const auto beforeRules = original.rules;
  auto object = QJsonDocument::fromJson(toJson(original).toUtf8()).object();
  object.insert("schema", 2);
  object.remove("fullShortcuts");
  object.remove("allApplications");
  object.remove("shiftTapInputMethod");
  const auto raw = QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
  Settings::setValue(Settings::Client::ShortcutConfig, raw);
  QString error;
  const auto migrated = load(&error);
  QVERIFY2(error.isEmpty(), qPrintable(error));
  QCOMPARE(migrated, original);
  QVERIFY(!migrated.fullShortcuts);
  QVERIFY(!migrated.allApplications);
  QCOMPARE(migrated.apps, beforeApps);
  QCOMPARE(migrated.rules, beforeRules);
  QCOMPARE(Settings::value(Settings::Client::ShortcutConfig).toString(), raw);

  // Older schemas accept their exact historical fields, not partial v3 data.
  object.insert("fullShortcuts", false);
  QVERIFY(!fromJson(QString::fromUtf8(QJsonDocument(object).toJson()), &error));
  QVERIFY(!error.isEmpty());
}

void ShortcutRulesTests::modifierMappingMatrix_data()
{
  QTest::addColumn<Habit>("habit");
  QTest::addColumn<DeviceOS>("source");
  QTest::addColumn<DeviceOS>("target");
  for (const auto habit : {Habit::Custom, Habit::Automatic, Habit::Mac, Habit::PC}) {
    for (const auto source : {DeviceOS::Unknown, DeviceOS::MacOS, DeviceOS::Windows, DeviceOS::Linux}) {
      for (const auto target : {DeviceOS::Unknown, DeviceOS::MacOS, DeviceOS::Windows, DeviceOS::Linux}) {
        const auto row = habitName(habit) + '-' + deviceOSName(source) + '-' + deviceOSName(target);
        QTest::newRow(qPrintable(row)) << habit << source << target;
      }
    }
  }
}

void ShortcutRulesTests::modifierMappingMatrix()
{
  QFETCH(Habit, habit);
  QFETCH(DeviceOS, source);
  QFETCH(DeviceOS, target);
  Configuration config;
  config.habit = habit;
  const bool macHabit = habit == Habit::Mac || (habit == Habit::Automatic && source == DeviceOS::MacOS);
  const bool mappingExpected = habit != Habit::Custom && target != DeviceOS::Unknown &&
                               (habit != Habit::Automatic || source != DeviceOS::Unknown) &&
                               macHabit != (target == DeviceOS::MacOS);
  const auto mapping = shortcutModifierMapping(config, source, target);
  QCOMPARE(mapping.has_value(), mappingExpected);
  if (mapping) {
    QCOMPARE(mapping->source, macHabit ? KeyModifierSuper : KeyModifierControl);
    QCOMPARE(mapping->target, target == DeviceOS::MacOS ? KeyModifierSuper : KeyModifierControl);
  }
  // This helper is direction only; runtime feature/scope gates are separate.
  config.enabled = false;
  config.fullShortcuts = true;
  config.allApplications = true;
  QCOMPARE(shortcutModifierMapping(config, source, target), mapping);
}

void ShortcutRulesTests::fullShortcutPreferences()
{
  auto config = defaults();
  const auto original = config;
  QVERIFY(!config.fullShortcuts);
  QVERIFY(!config.allApplications);
  config.fullShortcuts = true;
  QString error;
  QVERIFY(!validate(config).isEmpty());
  QVERIFY(!save(config, &error));
  QVERIFY(error.contains(QStringLiteral("选择")));

  setHabit(config, Habit::PC);
  config.allApplications = true;
  QVERIFY2(validate(config).isEmpty(), qPrintable(validate(config)));
  const auto encoded = toJson(config);
  const auto object = QJsonDocument::fromJson(encoded.toUtf8()).object();
  QCOMPARE(object.value("schema").toInt(), 4);
  QVERIFY(object.value("fullShortcuts").isBool());
  QVERIFY(object.value("allApplications").isBool());
  QVERIFY(object.value("shiftTapInputMethod").isBool());
  QCOMPARE(fromJson(encoded, &error).value(), config);
  QVERIFY(save(config, &error));
  QCOMPARE(load(&error), config);
  QCOMPARE(config.apps, original.apps);
  QCOMPARE(config.rules, original.rules);

  config.habit = Habit::Automatic;
  config.sourceOS = DeviceOS::Linux;
  QCOMPARE(shortcutModifierMapping(config, DeviceOS::Unknown, DeviceOS::MacOS),
           (std::optional<ModifierMapping>{ModifierMapping{KeyModifierControl, KeyModifierSuper}}));
  QVERIFY(!shortcutModifierMapping(config, DeviceOS::MacOS, DeviceOS::MacOS));
  const auto effective = effectiveConfiguration(config, DeviceOS::Unknown, DeviceOS::MacOS);
  QVERIFY(effective.fullShortcuts);
  QVERIFY(effective.allApplications);
  QCOMPARE(effective.apps, original.apps);
  for (qsizetype i = 0; i < effective.rules.size(); ++i)
    QCOMPARE(effective.rules[i].apps, original.rules[i].apps);
  QVERIFY(habitDescription(config, DeviceOS::Unknown, DeviceOS::MacOS).contains(QStringLiteral("所有远程应用")));
  QVERIFY(habitDescription(config, DeviceOS::Unknown, DeviceOS::MacOS).contains(QStringLiteral("Ctrl+Space")));
  config.allApplications = false;
  QVERIFY(habitDescription(config, DeviceOS::Unknown, DeviceOS::MacOS).contains(QStringLiteral("所选应用")));
  config.fullShortcuts = false;
  config.allApplications = true; // Stored for the next full-mode use, not exact-rule scope.
  QVERIFY(validate(config).isEmpty());
  QVERIFY(habitDescription(config, DeviceOS::Unknown, DeviceOS::MacOS).contains(QStringLiteral("复制")));
  QVERIFY(!habitDescription(config, DeviceOS::Unknown, DeviceOS::MacOS).contains(QStringLiteral("所有远程应用")));
}

void ShortcutRulesTests::schemaThreePreservesPreferencesWithoutEnablingGesture()
{
  auto original = defaults();
  original.habit = Habit::Automatic;
  original.sourceOS = DeviceOS::Linux;
  original.fullShortcuts = true;
  original.allApplications = true;
  original.rules.first().enabled = false;
  auto object = QJsonDocument::fromJson(toJson(original).toUtf8()).object();
  object.insert("schema", 3);
  object.remove("shiftTapInputMethod");
  const auto raw = QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
  Settings::setValue(Settings::Client::ShortcutConfig, raw);
  QString error;
  QCOMPARE(load(&error), original);
  QVERIFY2(error.isEmpty(), qPrintable(error));
  QVERIFY(!load().shiftTapInputMethod);
  QCOMPARE(Settings::value(Settings::Client::ShortcutConfig).toString(), raw);
  object.insert("shiftTapInputMethod", true);
  QVERIFY(!fromJson(QString::fromUtf8(QJsonDocument(object).toJson()), &error));
  QVERIFY(!error.isEmpty());
}

void ShortcutRulesTests::shiftTapPreferenceIsExplicitAndIndependent()
{
  auto config = defaults();
  QVERIFY(!config.shiftTapInputMethod);
  config.shiftTapInputMethod = true;
  config.enabled = false;
  QString error;
  QVERIFY2(save(config, &error), qPrintable(error));
  QCOMPARE(load(&error), config);
  for (const auto habit : {Habit::Automatic, Habit::Mac, Habit::PC, Habit::Custom}) {
    setHabit(config, habit);
    QVERIFY(config.shiftTapInputMethod);
    QCOMPARE(fromJson(toJson(config), &error).value(), config);
    for (const auto target : {DeviceOS::MacOS, DeviceOS::Linux, DeviceOS::Windows})
      QVERIFY(effectiveConfiguration(config, DeviceOS::Linux, target).shiftTapInputMethod);
  }
}

QTEST_GUILESS_MAIN(ShortcutRulesTests)
