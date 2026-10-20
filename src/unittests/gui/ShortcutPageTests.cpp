/*
 * SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "common/Settings.h"
#include "common/ShortcutRules.h"
#include "gui/personal/ApplicationPicker.h"
#include "gui/personal/ShortcutPage.h"
#include "gui/personal/RuleEditor.h"
#include "gui/personal/PersonalStyle.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using deskflow::gui::personal::ShortcutPage;
namespace shortcuts = deskflow::shortcuts;

class ShortcutPageTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase()
  {
    if (QGuiApplication::platformName() != QStringLiteral("offscreen") &&
        QGuiApplication::platformName() != QStringLiteral("minimal"))
      QSKIP("Widget regression tests require QT_QPA_PLATFORM=offscreen or minimal.");
    QVERIFY(m_temp.isValid());
    // Isolate Settings' initial singleton profile as well as the explicit test profile.
    qputenv("XDG_CONFIG_HOME", m_temp.filePath(QStringLiteral("config")).toUtf8());
    qputenv("XDG_STATE_HOME", m_temp.filePath(QStringLiteral("state")).toUtf8());
  }

  void init()
  {
    const auto path = m_temp.filePath(QStringLiteral("profile-%1").arg(m_profile++));
    QVERIFY(QDir().mkpath(path));
    Settings::setSettingsFile(path + QStringLiteral("/test.conf"));
    Settings::setStateFile(path + QStringLiteral("/test.state"));
    Settings::setValue(Settings::Core::CoreMode, Settings::CoreMode::Client);
    Settings::setValue(Settings::Gui::AutoStartCore, true);
    QString error;
    QVERIFY2(shortcuts::save(shortcuts::defaults(), &error), qPrintable(error));
  }

  void masterOffIsOnlyADraftAndLeavesSharingSettingsAlone()
  {
    const auto saved = shortcuts::load();
    const auto bytes = settingsBytes();
    QVERIFY(!bytes.isEmpty());
    ShortcutPage page;
    auto *master = page.findChild<QCheckBox *>(QStringLiteral("shortcutMasterSwitch"));
    auto *notice = page.findChild<QLabel *>(QStringLiteral("shortcutDisabledNotice"));
    auto *rules = page.findChild<QWidget *>(QStringLiteral("shortcutRuleList"));
    QVERIFY(master);
    QVERIFY(notice);
    QVERIFY(rules);
    QVERIFY(!page.isDirty());
    QVERIFY(notice->isHidden());
    QSignalSpy settingsChanged(Settings::instance(), &Settings::settingsChanged);
    QSignalSpy dirty(&page, &ShortcutPage::dirtyChanged);

    master->setChecked(false);

    QVERIFY(page.isDirty());
    QVERIFY(!page.configuration().enabled);
    QVERIFY(!notice->isHidden());
    QVERIFY(notice->text().contains(QStringLiteral("键鼠共享仍正常工作")));
    QVERIFY(!rules->isEnabled());
    QVERIFY(shortcuts::load() == saved);
    QCOMPARE(settingsBytes(), bytes);
    QCOMPARE(settingsChanged.count(), 0);
    QCOMPARE(Settings::value(Settings::Core::CoreMode).value<Settings::CoreMode>(), Settings::CoreMode::Client);
    QVERIFY(Settings::value(Settings::Gui::AutoStartCore).toBool());
    QCOMPARE(dirty.count(), 1);
    QCOMPARE(dirty.first().first().toBool(), true);
  }

  void individualRuleAndRevert()
  {
    const auto saved = shortcuts::load();
    const auto bytes = settingsBytes();
    QVERIFY(!bytes.isEmpty());
    ShortcutPage page;
    auto *copy = page.findChild<QCheckBox *>(QStringLiteral("enableRule_copy"));
    auto *paste = page.findChild<QCheckBox *>(QStringLiteral("enableRule_paste"));
    auto *apply = page.findChild<QPushButton *>(QStringLiteral("applyShortcutChanges"));
    auto *revert = page.findChild<QPushButton *>(QStringLiteral("revertShortcutChanges"));
    QVERIFY(copy);
    QVERIFY(paste);
    QVERIFY(apply);
    QVERIFY(revert);
    QVERIFY(!apply->isEnabled());
    QVERIFY(!revert->isEnabled());
    QSignalSpy dirty(&page, &ShortcutPage::dirtyChanged);

    copy->setChecked(false);

    QVERIFY(page.configuration().enabled);
    QVERIFY(!page.configuration().rules.at(0).enabled);
    QVERIFY(page.configuration().rules.at(1).enabled);
    QVERIFY(paste->isChecked());
    QVERIFY(apply->isEnabled());
    QVERIFY(revert->isEnabled());
    QVERIFY(shortcuts::load() == saved);
    QCOMPARE(settingsBytes(), bytes);

    // Revert uses the ordinary in-process button action, without a modal confirmation.
    revert->click();

    QVERIFY(!page.isDirty());
    QVERIFY(page.configuration() == saved);
    QVERIFY(!apply->isEnabled());
    QVERIFY(!revert->isEnabled());
    QCOMPARE(settingsBytes(), bytes);
    QCOMPARE(dirty.count(), 2);
    QCOMPARE(dirty.at(0).first().toBool(), true);
    QCOMPARE(dirty.at(1).first().toBool(), false);
  }

  void failedApplyPreservesDraftAndRecoveryActions()
  {
    const auto saved = shortcuts::load();
    const auto bytes = settingsBytes();
    QVERIFY(!bytes.isEmpty());
    ShortcutPage page;
    auto *copy = page.findChild<QCheckBox *>(QStringLiteral("enableRule_copy"));
    auto *master = page.findChild<QCheckBox *>(QStringLiteral("shortcutMasterSwitch"));
    auto *apply = page.findChild<QPushButton *>(QStringLiteral("applyShortcutChanges"));
    auto *revert = page.findChild<QPushButton *>(QStringLiteral("revertShortcutChanges"));
    auto *status = page.findChild<QLabel *>(QStringLiteral("shortcutApplyStatus"));
    QVERIFY(copy && master && apply && revert && status);
    copy->setChecked(false);
    const auto draft = page.configuration();
    page.setApplying(true);
    QVERIFY(!master->isEnabled());
    QVERIFY(!apply->isEnabled());
    QVERIFY(!revert->isEnabled());

    const auto failure = QStringLiteral("连接超时，已恢复上次共享设置。草稿已保留。");
    page.applied(false, failure);

    QVERIFY(page.isDirty());
    QVERIFY(page.configuration() == draft);
    QVERIFY(master->isEnabled());
    QVERIFY(apply->isEnabled());
    QVERIFY(revert->isEnabled());
    QCOMPARE(status->text(), failure);
    QVERIFY(shortcuts::load() == saved);
    QCOMPARE(settingsBytes(), bytes);
  }

  void successfulApplyReloadsPersistedConfiguration()
  {
    ShortcutPage page;
    auto *copy = page.findChild<QCheckBox *>(QStringLiteral("enableRule_copy"));
    auto *full = page.findChild<QCheckBox *>(QStringLiteral("fullShortcutSwitch"));
    auto *all = page.findChild<QCheckBox *>(QStringLiteral("fullShortcutAllApplications"));
    auto *apply = page.findChild<QPushButton *>(QStringLiteral("applyShortcutChanges"));
    auto *revert = page.findChild<QPushButton *>(QStringLiteral("revertShortcutChanges"));
    auto *status = page.findChild<QLabel *>(QStringLiteral("shortcutApplyStatus"));
    QVERIFY(copy && full && all && apply && revert && status);
    copy->setChecked(false);
    full->setChecked(true);
    all->setChecked(true);
    const auto draft = page.configuration();
    page.setApplying(true);

    // The controller owns persistence. This substitutes only its successful save,
    // without starting a core process, opening a connection or posting input events.
    QString error;
    QVERIFY2(shortcuts::save(draft, &error), qPrintable(error));
    const auto success = QStringLiteral("映射已应用，共享已连接。");
    page.applied(true, success);

    QVERIFY(!page.isDirty());
    QVERIFY(page.configuration() == draft);
    QVERIFY(shortcuts::load() == draft);
    QVERIFY(full->isChecked() && all->isChecked() && all->isEnabled());
    QVERIFY(!apply->isEnabled());
    QVERIFY(!revert->isEnabled());
    QCOMPARE(status->text(), success);
  }

  void openingApplicationPickerKeepsOriginalSelectionOrder()
  {
    const QStringList selected{QStringLiteral("com.electron.lark"), QStringLiteral("com.openai.codex")};
    deskflow::gui::personal::ApplicationPicker picker(selected);
    QCOMPARE(picker.selectedApplications(), selected);
  }

  void habitButtonsSubmitTheDraftWithoutWritingSettings()
  {
    auto config = shortcuts::defaults();
    config.rules.append({QStringLiteral("navigation"), QStringLiteral("导航"), {kKeyF2, KeyModifierAlt},
                         {kKeyHome, KeyModifierControl}, true, config.apps});
    QString error;
    QVERIFY2(shortcuts::save(config, &error), qPrintable(error));
    const auto bytes = settingsBytes();
    ShortcutPage page;
    auto *copy = page.findChild<QCheckBox *>(QStringLiteral("enableRule_copy"));
    auto *source = page.findChild<QComboBox *>(QStringLiteral("sourceOperatingSystem"));
    auto *pc = page.findChild<QPushButton *>(QStringLiteral("habitPC"));
    QVERIFY(copy && source && pc);
    copy->setChecked(false);
    source->setCurrentIndex(source->findData(static_cast<int>(shortcuts::DeviceOS::Windows)));
    int requests = 0;
    shortcuts::Configuration requested;
    connect(&page, &ShortcutPage::applyRequested, this, [&](const auto &draft) { ++requests; requested = draft; });
    pc->click();
    QCOMPARE(requests, 1);
    QCOMPARE(requested.habit, shortcuts::Habit::PC);
    QCOMPARE(requested.sourceOS, shortcuts::DeviceOS::Windows);
    QCOMPARE(requested.apps, config.apps);
    QVERIFY(!requested.rules.first().enabled);
    QVERIFY(requested.rules.last() == config.rules.last());
    QVERIFY(page.isDirty());
    QCOMPARE(settingsBytes(), bytes);
  }

  void automaticRequiresFallbackAndDetectionNeverOverwritesDraft()
  {
    ShortcutPage page;
    int requests = 0;
    connect(&page, &ShortcutPage::applyRequested, this, [&](const auto &) { ++requests; });
    auto *automatic = page.findChild<QPushButton *>(QStringLiteral("habitAutomatic"));
    auto *source = page.findChild<QComboBox *>(QStringLiteral("sourceOperatingSystem"));
    QVERIFY(automatic && source);
    automatic->click();
    QCOMPARE(requests, 0);
    QCOMPARE(page.configuration().habit, shortcuts::Habit::Custom);
    source->setCurrentIndex(source->findData(static_cast<int>(shortcuts::DeviceOS::Linux)));
    automatic->click();
    QCOMPARE(requests, 1);
    QCOMPARE(page.configuration().habit, shortcuts::Habit::Automatic);
    const auto draft = page.configuration();
    page.setSourceOperatingSystem(shortcuts::DeviceOS::MacOS);
    QVERIFY(page.configuration() == draft);
    QVERIFY(page.isDirty());
    page.setServerModifierMapping(true);
    QVERIFY(!automatic->isEnabled());
    QVERIFY(page.configuration() == draft);
    page.setClientMode(false);
    QVERIFY(!page.findChild<QCheckBox *>(QStringLiteral("shortcutMasterSwitch"))->isEnabled());
  }

  void accessibleHabitSelectionMatchesTheDraftAndCannotBeDeselected()
  {
    ShortcutPage page;
    page.setSourceOperatingSystem(shortcuts::DeviceOS::Linux);
    auto *automatic = page.findChild<QPushButton *>(QStringLiteral("habitAutomatic"));
    auto *mac = page.findChild<QPushButton *>(QStringLiteral("habitMac"));
    auto *pc = page.findChild<QPushButton *>(QStringLiteral("habitPC"));
    QVERIFY(automatic && mac && pc);
    int requests = 0;
    connect(&page, &ShortcutPage::applyRequested, this, [&](const auto &) { ++requests; });
    const auto bytes = settingsBytes();

    // AX writes the checked value directly, rather than invoking clicked().
    automatic->setChecked(true);
    QCOMPARE(requests, 1);
    QCOMPARE(page.configuration().habit, shortcuts::Habit::Automatic);
    QVERIFY(automatic->isChecked());
    QVERIFY(!mac->isChecked() && !pc->isChecked());
    automatic->setChecked(false);
    QVERIFY(automatic->isChecked());
    QCOMPARE(requests, 1);

    pc->setChecked(true);
    QCOMPARE(requests, 2);
    QCOMPARE(page.configuration().habit, shortcuts::Habit::PC);
    QVERIFY(pc->isChecked());
    QVERIFY(!automatic->isChecked() && !mac->isChecked());
    const auto draft = page.configuration();
    page.setApplying(true);
    mac->setChecked(true);
    QCOMPARE(requests, 2);
    QVERIFY(page.configuration() == draft);
    QVERIFY(pc->isChecked() && !mac->isChecked());
    QCOMPARE(settingsBytes(), bytes);
  }

  void sourceScopeBadgeFitsASingleTextLine()
  {
    ShortcutPage page;
    deskflow::gui::personal::applyPersonalStyle(&page);
    page.resize(760, 640);
    page.show();
    QApplication::processEvents();
    auto *scope = page.findChild<QLabel *>(QStringLiteral("shortcutScope"));
    QVERIFY(scope);
    QVERIFY(!scope->wordWrap());
    QVERIFY(scope->height() >= scope->sizeHint().height());
    QVERIFY(scope->contentsRect().height() >= scope->fontMetrics().height());
    QVERIFY(scope->width() >= scope->fontMetrics().horizontalAdvance(scope->text()));
  }

  void fullAdaptationIsAnExplicitDraftAndKeepsExactRuleScopes()
  {
    auto config = shortcuts::defaults();
    config.rules.append({QStringLiteral("navigation"), QStringLiteral("导航"), {kKeyF2, KeyModifierAlt},
                         {kKeyHome, KeyModifierControl}, false, config.apps});
    QString error;
    QVERIFY2(shortcuts::save(config, &error), qPrintable(error));
    const auto bytes = settingsBytes();
    ShortcutPage page;
    auto *full = page.findChild<QCheckBox *>(QStringLiteral("fullShortcutSwitch"));
    auto *all = page.findChild<QCheckBox *>(QStringLiteral("fullShortcutAllApplications"));
    auto *warning = page.findChild<QLabel *>(QStringLiteral("fullShortcutTerminalNotice"));
    auto *apply = page.findChild<QPushButton *>(QStringLiteral("applyShortcutChanges"));
    QVERIFY(full && all && warning && apply);
    QVERIFY(!full->isChecked());
    QVERIFY(!all->isChecked() && !all->isEnabled());
    QVERIFY(warning->isHidden());
    QVERIFY(!page.isDirty());
    int requests = 0;
    shortcuts::Configuration requested;
    connect(&page, &ShortcutPage::applyRequested, this, [&](const auto &draft) { ++requests; requested = draft; });

    full->setChecked(true);
    QVERIFY(page.configuration().fullShortcuts);
    QCOMPARE(page.configuration().habit, shortcuts::localOS() == shortcuts::DeviceOS::MacOS
        ? shortcuts::Habit::PC : shortcuts::Habit::Mac);
    QVERIFY(page.configuration().rules == config.rules);
    QCOMPARE(page.configuration().apps, config.apps);
    QVERIFY(all->isEnabled());
    QVERIFY(!warning->isHidden());
    QVERIFY(warning->text().contains(QStringLiteral("终端 Ctrl+C")));
    all->setChecked(true);
    QVERIFY(page.configuration().allApplications);
    QVERIFY(page.configuration().rules == config.rules);
    QCOMPARE(page.configuration().apps, config.apps);
    QCOMPARE(requests, 0);
    QCOMPARE(settingsBytes(), bytes);
    apply->click();
    QCOMPARE(requests, 1);
    QVERIFY(requested.fullShortcuts && requested.allApplications);
    QVERIFY(requested.rules == config.rules);
    QCOMPARE(settingsBytes(), bytes);

    page.applied(false, QStringLiteral("测试失败，草稿保留"));
    QVERIFY(page.isDirty());
    QVERIFY(full->isChecked() && all->isChecked());
    page.findChild<QPushButton *>(QStringLiteral("revertShortcutChanges"))->click();
    QVERIFY(!page.isDirty());
    QVERIFY(page.configuration() == config);
    QVERIFY(!full->isChecked() && !all->isChecked() && !all->isEnabled());
    QCOMPARE(settingsBytes(), bytes);
  }

  void fullAdaptationChoosesAutomaticWhenTheSourceIsKnown_data()
  {
    QTest::addColumn<bool>("detected");
    QTest::newRow("reported source") << true;
    QTest::newRow("legacy fallback") << false;
  }

  void fullAdaptationChoosesAutomaticWhenTheSourceIsKnown()
  {
    QFETCH(bool, detected);
    auto config = shortcuts::defaults();
    config.sourceOS = detected ? shortcuts::DeviceOS::Unknown : shortcuts::DeviceOS::Linux;
    QString error;
    QVERIFY2(shortcuts::save(config, &error), qPrintable(error));
    ShortcutPage page;
    if (detected) page.setSourceOperatingSystem(shortcuts::DeviceOS::Linux);
    page.findChild<QCheckBox *>(QStringLiteral("fullShortcutSwitch"))->setChecked(true);
    QCOMPARE(page.configuration().habit, shortcuts::Habit::Automatic);
    QVERIFY(page.configuration().fullShortcuts);
    QVERIFY(page.configuration().rules == config.rules);
    QCOMPARE(page.configuration().sourceOS, config.sourceOS);
    QVERIFY(shortcuts::validate(page.configuration()).isEmpty());
  }

  void fullAdaptationScopeRemembersItsValueAndRespectsBusyAndMaster()
  {
    ShortcutPage page;
    auto *full = page.findChild<QCheckBox *>(QStringLiteral("fullShortcutSwitch"));
    auto *all = page.findChild<QCheckBox *>(QStringLiteral("fullShortcutAllApplications"));
    auto *master = page.findChild<QCheckBox *>(QStringLiteral("shortcutMasterSwitch"));
    QVERIFY(full && all && master);
    full->setChecked(true);
    all->setChecked(true);
    full->setChecked(false);
    QVERIFY(!all->isEnabled() && all->isChecked());
    QVERIFY(!page.configuration().fullShortcuts && page.configuration().allApplications);
    const auto draft = page.configuration();
    page.setApplying(true);
    QVERIFY(!full->isEnabled());
    // An accessibility write must not mutate disabled controls' draft values.
    full->setChecked(true);
    all->setChecked(false);
    QVERIFY(page.configuration() == draft);
    QVERIFY(!full->isChecked() && all->isChecked());
    page.applied(false);
    master->setChecked(false);
    QVERIFY(!full->isEnabled() && !all->isEnabled());
    full->setChecked(true);
    QVERIFY(!page.configuration().fullShortcuts);
    QVERIFY(page.configuration().allApplications);
  }

  void fullAdaptationExplainsLegacyModifierSuspension()
  {
    ShortcutPage page;
    page.findChild<QCheckBox *>(QStringLiteral("fullShortcutSwitch"))->setChecked(true);
    page.setServerModifierMapping(true);
    auto *hint = page.findChild<QLabel *>(QStringLiteral("fullShortcutHint"));
    QVERIFY(hint);
    QVERIFY(hint->text().contains(QStringLiteral("已暂停")));
    QVERIFY(page.configuration().fullShortcuts);
    QVERIFY(page.findChild<QPushButton *>(QStringLiteral("applyShortcutChanges"))->isEnabled());
  }

  void shiftTapInputMethodUsesTheDraftApplyAndRevertTransaction()
  {
    const auto saved = shortcuts::load();
    const auto bytes = settingsBytes();
    ShortcutPage page;
    auto *toggle = page.findChild<QCheckBox *>(QStringLiteral("shiftTapInputMethodSwitch"));
    auto *hint = page.findChild<QLabel *>(QStringLiteral("shiftTapInputMethodHint"));
    auto *apply = page.findChild<QPushButton *>(QStringLiteral("applyShortcutChanges"));
    auto *revert = page.findChild<QPushButton *>(QStringLiteral("revertShortcutChanges"));
    QVERIFY(toggle && hint && apply && revert);
    QVERIFY(!toggle->isChecked());
    QVERIFY(!page.isDirty());
    if (shortcuts::localOS() != shortcuts::DeviceOS::MacOS) {
      QVERIFY(!toggle->isEnabled());
      QVERIFY(hint->text().contains(QStringLiteral("保留原生")));
      toggle->setChecked(true);
      QVERIFY(!toggle->isChecked());
      QVERIFY(page.configuration() == saved);
      QCOMPARE(settingsBytes(), bytes);
      return;
    }
    QVERIFY(toggle->isEnabled());
    int requests = 0;
    shortcuts::Configuration requested;
    connect(&page, &ShortcutPage::applyRequested, this, [&](const auto &draft) { ++requests; requested = draft; });
    QSignalSpy settingsChanged(Settings::instance(), &Settings::settingsChanged);
    toggle->setChecked(true);
    QVERIFY(page.configuration().shiftTapInputMethod);
    QCOMPARE(page.configuration().habit, saved.habit);
    QVERIFY(page.configuration().rules == saved.rules);
    QVERIFY(page.isDirty());
    QCOMPARE(requests, 0);
    QCOMPARE(settingsChanged.count(), 0);
    QCOMPARE(settingsBytes(), bytes);
    revert->click();
    QVERIFY(!toggle->isChecked() && !page.isDirty());

    toggle->setChecked(true);
    apply->click();
    QCOMPARE(requests, 1);
    QVERIFY(requested.shiftTapInputMethod);
    QCOMPARE(settingsBytes(), bytes);
    page.setApplying(true);
    QVERIFY(!toggle->isEnabled());
    toggle->setChecked(false);
    QVERIFY(toggle->isChecked());
    page.applied(false, QStringLiteral("测试应用失败"));
    QVERIFY(toggle->isChecked() && page.isDirty());
    QVERIFY(apply->isEnabled() && revert->isEnabled());
    QString error;
    QVERIFY2(shortcuts::save(requested, &error), qPrintable(error));
    page.applied(true);
    QVERIFY(toggle->isChecked() && !page.isDirty());
    QVERIFY(shortcuts::load().shiftTapInputMethod);
    toggle->setChecked(false);
    QVERIFY(page.isDirty());
    revert->click();
    QVERIFY(toggle->isChecked() && !page.isDirty());
  }

  void shiftTapInputMethodRespectsDisabledStates_data()
  {
    QTest::addColumn<QString>("state");
    QTest::newRow("master off") << QStringLiteral("master");
    QTest::newRow("applying") << QStringLiteral("applying");
    QTest::newRow("server role") << QStringLiteral("server");
    QTest::newRow("legacy modifier replacement") << QStringLiteral("legacy");
  }

  void shiftTapInputMethodRespectsDisabledStates()
  {
    QFETCH(QString, state);
    auto config = shortcuts::defaults();
    config.shiftTapInputMethod = true;
    QString error;
    QVERIFY2(shortcuts::save(config, &error), qPrintable(error));
    const auto bytes = settingsBytes();
    ShortcutPage page;
    if (state == QStringLiteral("master"))
      page.findChild<QCheckBox *>(QStringLiteral("shortcutMasterSwitch"))->setChecked(false);
    else if (state == QStringLiteral("applying")) page.setApplying(true);
    else if (state == QStringLiteral("server")) page.setClientMode(false);
    else page.setServerModifierMapping(true);
    auto *toggle = page.findChild<QCheckBox *>(QStringLiteral("shiftTapInputMethodSwitch"));
    QVERIFY(toggle && toggle->isChecked() && !toggle->isEnabled());
    const auto draft = page.configuration();
    toggle->setChecked(false);
    QVERIFY(toggle->isChecked());
    QVERIFY(page.configuration() == draft);
    QCOMPARE(settingsBytes(), bytes);
    if (state == QStringLiteral("legacy") && shortcuts::localOS() == shortcuts::DeviceOS::MacOS)
      QVERIFY(page.findChild<QLabel *>(QStringLiteral("shiftTapInputMethodHint"))->text().contains(QStringLiteral("已暂停")));
  }

  void shiftTapInputMethodRespectsAReadOnlyProfile()
  {
    auto config = shortcuts::defaults();
    config.shiftTapInputMethod = true;
    QString error;
    QVERIFY2(shortcuts::save(config, &error), qPrintable(error));
    const auto path = Settings::settingsFile();
    const auto permissions = QFile::permissions(path);
    const auto restorePermissions = qScopeGuard([path, permissions] { QFile::setPermissions(path, permissions); });
    QVERIFY(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::ReadUser | QFileDevice::ReadGroup | QFileDevice::ReadOther));
    if (Settings::isWritable()) QSKIP("This environment does not enforce read-only file permissions.");
    const auto bytes = settingsBytes();
    ShortcutPage page;
    auto *toggle = page.findChild<QCheckBox *>(QStringLiteral("shiftTapInputMethodSwitch"));
    QVERIFY(toggle && toggle->isChecked() && !toggle->isEnabled());
    toggle->setChecked(false);
    QVERIFY(toggle->isChecked());
    QVERIFY(page.configuration() == config);
    QVERIFY(!page.isDirty());
    QVERIFY(!page.findChild<QPushButton *>(QStringLiteral("applyShortcutChanges"))->isEnabled());
    QCOMPARE(settingsBytes(), bytes);
  }

  void schemesAndRestoringRulesPreserveTheShiftTapPreference()
  {
    auto config = shortcuts::defaults();
    config.shiftTapInputMethod = true;
    config.rules.append({QStringLiteral("navigation"), QStringLiteral("导航"), {kKeyF2, KeyModifierAlt},
                         {kKeyHome, KeyModifierControl}, true, config.apps});
    QString error;
    QVERIFY2(shortcuts::save(config, &error), qPrintable(error));
    const auto bytes = settingsBytes();
    ShortcutPage page;
    int requests = 0;
    connect(&page, &ShortcutPage::applyRequested, this, [&](const auto &draft) {
      ++requests;
      QVERIFY(draft.shiftTapInputMethod);
    });
    page.findChild<QPushButton *>(QStringLiteral("habitPC"))->setChecked(true);
    QCOMPARE(requests, 1);
    QVERIFY(page.configuration().shiftTapInputMethod);
    bool confirmed = false;
    QTimer::singleShot(0, &page, [&] {
      auto *question = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
      if (!question) return;
      confirmed = true;
      question->button(QMessageBox::Yes)->click();
    });
    page.findChild<QPushButton *>(QStringLiteral("restoreDefaultShortcutRules"))->click();
    QVERIFY(confirmed);
    QVERIFY(page.configuration().shiftTapInputMethod);
    QVERIFY(page.configuration().rules == shortcuts::defaults().rules);
    QCOMPARE(requests, 1);
    QCOMPARE(settingsBytes(), bytes);
  }

  void endpointChangeClearsOnlyTheOldSourceHint()
  {
    auto config = shortcuts::defaults();
    config.sourceOS = shortcuts::DeviceOS::Linux;
    QString error;
    QVERIFY2(shortcuts::save(config, &error), qPrintable(error));
    ShortcutPage page;
    page.findChild<QCheckBox *>(QStringLiteral("enableRule_copy"))->setChecked(false);
    auto expected = page.configuration();
    expected.sourceOS = shortcuts::DeviceOS::Unknown;
    config.sourceOS = shortcuts::DeviceOS::Unknown;
    QVERIFY2(shortcuts::save(config, &error), qPrintable(error));
    page.clearSourceOperatingSystemFallback();
    QVERIFY(page.configuration() == expected);
    QVERIFY(page.isDirty());
    page.findChild<QPushButton *>(QStringLiteral("revertShortcutChanges"))->click();
    QVERIFY(!page.isDirty());
    QVERIFY(page.configuration() == config);
  }

  void editingManagedRuleUsesResolvedChordsAndOnlySaveSelectsCustom()
  {
    auto config = shortcuts::defaults();
    config.apps = {QStringLiteral("com.test.editor")};
    for (auto &rule : config.rules) rule.apps = config.apps;
    config.sourceOS = shortcuts::DeviceOS::MacOS;
    shortcuts::setHabit(config, shortcuts::Habit::Mac);
    config.fullShortcuts = true;
    config.allApplications = true;
    QString error;
    QVERIFY2(shortcuts::save(config, &error), qPrintable(error));
    ShortcutPage page;
    bool cancelled = false;
    QTimer::singleShot(0, &page, [&] {
      auto *editor = qobject_cast<deskflow::gui::personal::RuleEditor *>(QApplication::activeModalWidget());
      if (!editor) return;
      cancelled = true;
      editor->reject();
    });
    page.findChild<QPushButton *>(QStringLiteral("editRule_copy"))->click();
    QVERIFY(cancelled);
    QVERIFY(page.configuration() == config);
    bool saved = false;
    QTimer::singleShot(0, &page, [&] {
      auto *editor = qobject_cast<deskflow::gui::personal::RuleEditor *>(QApplication::activeModalWidget());
      if (!editor) return;
      auto *name = editor->findChild<QLineEdit *>(QStringLiteral("ruleName"));
      auto *save = editor->findChild<QPushButton *>(QStringLiteral("saveRule"));
      if (!name || !save) { editor->reject(); return; }
      name->setText(QStringLiteral("自定义复制"));
      saved = save->isEnabled();
      if (saved) save->click(); else editor->reject();
    });
    page.findChild<QPushButton *>(QStringLiteral("editRule_copy"))->click();
    QVERIFY(saved);
    QCOMPARE(page.configuration().habit, shortcuts::Habit::Custom);
    QVERIFY(!page.configuration().fullShortcuts);
    QVERIFY(page.configuration().allApplications);
    const auto target = shortcuts::localOS() == shortcuts::DeviceOS::MacOS ? KeyModifierSuper : KeyModifierControl;
    for (const auto &rule : page.configuration().rules) {
      QCOMPARE(rule.input.modifiers, KeyModifierSuper);
      QCOMPARE(rule.output.modifiers, target);
    }
    QVERIFY(page.isDirty());
    QVERIFY(shortcuts::load() == config);
  }

  void choosingAnApplicationRowImmediatelyShowsTheChipAndSurvivesReload()
  {
    auto saved = shortcuts::defaults();
    saved.apps.clear();
    shortcuts::setHabit(saved, shortcuts::Habit::Automatic);
    saved.sourceOS = shortcuts::DeviceOS::Linux;
    saved.fullShortcuts = true;
    saved.allApplications = true;
    saved.shiftTapInputMethod = true;
    QVERIFY(shortcuts::save(saved));
    const auto bytes = settingsBytes();
    const QString id = QStringLiteral("com.test.picker-row");
    ShortcutPage page;
    bool selected = false;
    QTimer::singleShot(0, &page, [&] {
      auto *picker = qobject_cast<deskflow::gui::personal::ApplicationPicker *>(QApplication::activeModalWidget());
      if (!picker) return;
      auto *list = picker->findChild<QListWidget *>(QStringLiteral("applicationChoices"));
      auto *confirm = picker->findChild<QPushButton *>(QStringLiteral("confirmApplications"));
      if (!list || !confirm) { picker->reject(); return; }
      auto *item = new QListWidgetItem(QStringLiteral("Picker row fixture"), list);
      item->setData(Qt::UserRole, id);
      item->setSizeHint(QSize(0, 40));
      list->scrollToItem(item);
      QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualItemRect(item).center());
      selected = item->isSelected();
      if (confirm->isEnabled()) confirm->click(); else picker->reject();
    });
    page.findChild<QPushButton *>(QStringLiteral("addGlobalApplication"))->click();
    QVERIFY(selected);
    QCOMPARE(page.configuration().apps, QStringList{id});
    QVERIFY(page.findChild<QWidget *>(QStringLiteral("appChip_") + id));
    for (const auto &rule : page.configuration().rules) QVERIFY(rule.apps.contains(id));
    QVERIFY(page.isDirty());
    QCOMPARE(settingsBytes(), bytes); // Returning from the picker updates the draft only.
    QVERIFY(page.configuration().shiftTapInputMethod);
    QVERIFY(page.configuration().fullShortcuts && page.configuration().allApplications);

    // Substitute only the controller's save; no sharing or native input starts.
    const auto draft = page.configuration();
    QVERIFY(shortcuts::save(draft));
    page.applied(true, QStringLiteral("saved"));
    page.reload();
    QVERIFY(!page.isDirty());
    QCOMPARE(page.configuration(), draft);
    QVERIFY(page.findChild<QWidget *>(QStringLiteral("appChip_") + id));
  }

  void cancellingApplicationSelectionPreservesTheDraftAndCommittedBytes()
  {
    const auto bytes = settingsBytes();
    ShortcutPage page;
    page.findChild<QCheckBox *>(QStringLiteral("enableRule_copy"))->setChecked(false);
    const auto draft = page.configuration();
    bool visited = false;
    QTimer::singleShot(0, &page, [&] {
      auto *picker = qobject_cast<deskflow::gui::personal::ApplicationPicker *>(QApplication::activeModalWidget());
      if (!picker) return;
      visited = true;
      auto *list = picker->findChild<QListWidget *>(QStringLiteral("applicationChoices"));
      if (list) {
        auto *item = new QListWidgetItem(QStringLiteral("Cancelled fixture"), list);
        item->setData(Qt::UserRole, QStringLiteral("com.test.cancelled"));
        item->setSelected(true);
      }
      picker->reject();
    });
    page.findChild<QPushButton *>(QStringLiteral("addGlobalApplication"))->click();
    QVERIFY(visited);
    QCOMPARE(page.configuration(), draft);
    QVERIFY(page.isDirty());
    QCOMPARE(settingsBytes(), bytes);
  }

  void acceptingWithoutAnAdditionDoesNotClaimUnsavedChanges()
  {
    const auto bytes = settingsBytes();
    ShortcutPage page;
    const auto saved = page.configuration();
    bool visited = false;
    QTimer::singleShot(0, &page, [&] {
      auto *picker = qobject_cast<deskflow::gui::personal::ApplicationPicker *>(QApplication::activeModalWidget());
      if (!picker) return;
      visited = true;
      picker->accept(); // Defensive caller path; normal UI disables this action.
    });
    page.findChild<QPushButton *>(QStringLiteral("addGlobalApplication"))->click();
    QVERIFY(visited);
    QCOMPARE(page.configuration(), saved);
    QVERIFY(!page.isDirty());
    QCOMPARE(settingsBytes(), bytes);
    QVERIFY(page.findChild<QLabel *>(QStringLiteral("shortcutApplyStatus"))->text().contains(QStringLiteral("未新增")));
  }

private:
  QByteArray settingsBytes() const
  {
    QFile file(Settings::settingsFile());
    if (!file.open(QIODevice::ReadOnly))
      return {};
    return file.readAll();
  }
  QTemporaryDir m_temp;
  int m_profile = 0;
};

QTEST_MAIN(ShortcutPageTests)
#include "ShortcutPageTests.moc"
