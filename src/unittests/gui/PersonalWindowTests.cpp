/* SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception */
#include "common/Settings.h"
#include "gui/personal/LoginStartup.h"
#include "gui/personal/PersonalWindow.h"
#include <QCheckBox>
#include <QEvent>
#include <QFile>
#include <QLabel>
#include <QMutex>
#include <QMutexLocker>
#include <QPushButton>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

using deskflow::gui::personal::PersonalWindow;
using deskflow::gui::personal::LoginStartup;

namespace {
struct FakeStartupState
{
  explicit FakeStartupState(LoginStartup::Status initial) : status(std::move(initial)) {}
  QMutex mutex;
  LoginStartup::Status status;
  bool failChange = false;
  bool requireApproval = false;
  int reads = 0;
  int changes = 0;
  int opened = 0;
};

class FakeStartup
{
public:
  explicit FakeStartup(LoginStartup::Status initial = {true, false, false, {}, {}})
      : state(std::make_shared<FakeStartupState>(std::move(initial))), service(operations(state)) {}

  void setExternalStatus(LoginStartup::Status status)
  {
    QMutexLocker lock(&state->mutex);
    state->status = std::move(status);
  }
  void failChanges()
  {
    QMutexLocker lock(&state->mutex);
    state->failChange = true;
  }
  void requireApproval()
  {
    QMutexLocker lock(&state->mutex);
    state->requireApproval = true;
  }
  int reads() const { QMutexLocker lock(&state->mutex); return state->reads; }
  int changes() const { QMutexLocker lock(&state->mutex); return state->changes; }
  int opened() const { QMutexLocker lock(&state->mutex); return state->opened; }

  std::shared_ptr<FakeStartupState> state;
  LoginStartup service;

private:
  static LoginStartup::Operations operations(const std::shared_ptr<FakeStartupState> &state)
  {
    return {
        [state] {
          QMutexLocker lock(&state->mutex);
          ++state->reads;
          return state->status;
        },
        [state](bool enabled) -> LoginStartup::ChangeResult {
          QMutexLocker lock(&state->mutex);
          ++state->changes;
          if (state->failChange)
            return {false, state->status, QStringLiteral("模拟登录项修改失败")};
          state->status.enabled = enabled && !state->requireApproval;
          state->status.approvalRequired = enabled && state->requireApproval;
          return {true, state->status, {}};
        },
        [state] { QMutexLocker lock(&state->mutex); ++state->opened; }
    };
  }
};
} // namespace

class PersonalWindowTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase()
  {
    if (QGuiApplication::platformName() != QStringLiteral("offscreen") &&
        QGuiApplication::platformName() != QStringLiteral("minimal"))
      QSKIP("Widget regression tests require QT_QPA_PLATFORM=offscreen or minimal.");
    QVERIFY(m_profile.isValid());
    Settings::setSettingsFile(m_profile.filePath("settings.conf"));
    Settings::setStateFile(m_profile.filePath("state.conf"));
    Settings::setValue(Settings::Gui::AutoStartCore, true);
    Settings::save(false);
  }

  void init()
  {
    Settings::setValue(Settings::Gui::AutoStartCore, true);
    Settings::setValue(Settings::Gui::Autohide, false);
    Settings::setValue(Settings::Client::YScrollScale, 1.0);
    Settings::setValue(Settings::Client::XScrollScale, 1.0);
    Settings::save(false);
  }

  void accessibleSelectionChangesTheVisiblePage()
  {
    FakeStartup startup;
    PersonalWindow window(nullptr, &startup.service);
    auto *pages = window.findChild<QStackedWidget *>();
    QVERIFY(pages);
    QList<QPushButton *> navigation;
    for (auto *button : window.findChildren<QPushButton *>())
      if (button->property("personalNav").toBool()) navigation.append(button);
    QCOMPARE(navigation.size(), 4);
    for (int page : {1, 2, 3, 0}) {
      // macOS AX changes a checkable button's state without emitting clicked.
      navigation[page]->setChecked(true);
      QCOMPARE(pages->currentIndex(), page);
      int selected = 0;
      for (auto *button : navigation) selected += button->isChecked();
      QCOMPARE(selected, 1);
    }
  }

  void connectionRefreshKeepsUnsavedPreferences()
  {
    FakeStartup startup;
    PersonalWindow window(nullptr, &startup.service);
    QCheckBox *autoConnect = nullptr;
    for (auto *box : window.findChildren<QCheckBox *>())
      if (box->text().contains(QStringLiteral("打开桌桥"))) autoConnect = box;
    QVERIFY(autoConnect);
    QVERIFY(autoConnect->isChecked());
    autoConnect->setChecked(false);
    QVERIFY(window.hasPendingPreferences());
    window.showPage(PersonalWindow::Connection);
    window.setConnectionStatus(deskflow::core::ConnectionState::Connected, deskflow::core::ProcessState::Started,
                               true, true, true, "192.0.2.1", "test-mac");
    window.reloadPreferences();
    window.showPage(PersonalWindow::General);
    QVERIFY(!autoConnect->isChecked());
    QVERIFY(window.hasPendingPreferences());
    QVERIFY(Settings::value(Settings::Gui::AutoStartCore).toBool());
    autoConnect->setChecked(true);
    QVERIFY(!window.hasPendingPreferences());
  }

  void windowDraftDoesNotRewriteRoundedOrClampedScrollSettings()
  {
    Settings::setValue(Settings::Client::YScrollScale, 1.25);
    Settings::setValue(Settings::Client::XScrollScale, 12.0);
    FakeStartup startup;
    PersonalWindow window(nullptr, &startup.service);
    auto *hide = window.findChild<QCheckBox *>(QStringLiteral("hideMainWindowSwitch"));
    auto *apply = window.findChild<QPushButton *>(QStringLiteral("applyGeneralPreferences"));
    QVERIFY(hide && apply);
    QSignalSpy changes(&window, &PersonalWindow::preferencesApplyRequested);
    hide->setChecked(true);
    QVERIFY(!Settings::value(Settings::Gui::Autohide).toBool());
    apply->click();
    QCOMPARE(changes.count(), 1);
    const auto draft = changes.takeFirst().first().toMap();
    QCOMPARE(draft.size(), 1);
    QCOMPARE(draft.value(Settings::Gui::Autohide).toBool(), true);
    QCOMPARE(Settings::value(Settings::Client::YScrollScale).toDouble(), 1.25);
    QCOMPARE(Settings::value(Settings::Client::XScrollScale).toDouble(), 12.0);
    window.findChild<QPushButton *>(QStringLiteral("revertGeneralPreferences"))->click();
    QVERIFY(!window.hasPendingPreferences());
    QVERIFY(!hide->isChecked());
  }

  void sourceSubtitleRemainsASingleConciseLine()
  {
    FakeStartup startup;
    PersonalWindow window(nullptr, &startup.service);
    auto *subtitle = window.findChild<QLabel *>(QStringLiteral("personalPageSubtitle"));
    QVERIFY(subtitle);
    QVERIFY(!subtitle->wordWrap());
    QVERIFY(subtitle->text().startsWith(QStringLiteral("来源待识别")));
    window.setDeviceContext(deskflow::shortcuts::DeviceOS::Windows, false);
    QVERIFY(subtitle->text().startsWith(QStringLiteral("Windows →")));
    window.setDeviceContext(deskflow::shortcuts::DeviceOS::MacOS, true);
    QVERIFY(subtitle->text().contains(QStringLiteral("键鼠来源")));
    QVERIFY(!subtitle->wordWrap());
  }

  void mappingOverviewCanDescribeFullAdaptationWithoutExactRules()
  {
    FakeStartup startup;
    PersonalWindow window(nullptr, &startup.service);
    auto *detail = window.findChild<QLabel *>(QStringLiteral("personalMappingDetail"));
    QVERIFY(detail);
    const auto status = QStringLiteral("完整快捷键适配 · 全部应用 · Ctrl+空格保留");
    window.setMappingStatus(true, 0, status);
    QCOMPARE(detail->text(), status);
    window.setMappingStatus(false, 0, status);
    QVERIFY(detail->text().contains(QStringLiteral("已关闭")));
    window.setDeviceContext(deskflow::shortcuts::DeviceOS::Windows, true);
    window.setMappingStatus(true, 0, status);
    QVERIFY(detail->text().contains(QStringLiteral("本机物理按键不转换")));
  }

  void loginStartupShowsAnExistingEnabledItemWithoutTakingOwnership()
  {
    FakeStartup startup({true, true, false, QStringLiteral("已读取现有登录项"), {}});
    PersonalWindow window(nullptr, &startup.service);
    auto *toggle = window.findChild<QCheckBox *>(QStringLiteral("loginStartupSwitch"));
    auto *hint = window.findChild<QLabel *>(QStringLiteral("loginStartupHint"));
    QVERIFY(toggle && hint);
    QVERIFY(toggle->isChecked() && toggle->isEnabled());
    QVERIFY(hint->text().contains(QStringLiteral("已读取现有登录项")));
    QCOMPARE(startup.changes(), 0);
    QVERIFY(!window.hasPendingPreferences());
    QVERIFY(startup.service.parent() == nullptr);
  }

  void loginStartupChangesImmediatelyAndPreservesGeneralDrafts()
  {
    FakeStartup startup;
    PersonalWindow window(nullptr, &startup.service);
    auto *toggle = window.findChild<QCheckBox *>(QStringLiteral("loginStartupSwitch"));
    auto *autoConnect = window.findChild<QCheckBox *>(QStringLiteral("automaticSharingSwitch"));
    auto *autoHide = window.findChild<QCheckBox *>(QStringLiteral("hideMainWindowSwitch"));
    QVERIFY(toggle && autoConnect && autoHide);
    QSignalSpy preferences(&window, &PersonalWindow::preferencesApplyRequested);
    QSignalSpy starts(&window, &PersonalWindow::connectRequested);
    QSignalSpy stops(&window, &PersonalWindow::disconnectRequested);
    QSignalSpy settingsChanged(Settings::instance(), &Settings::settingsChanged);
    toggle->setChecked(true);
    QTRY_VERIFY(!startup.service.isApplying());
    QVERIFY(startup.service.status().enabled);
    QVERIFY(toggle->isChecked());
    QVERIFY(!window.hasPendingPreferences());
    autoConnect->setChecked(false);
    autoHide->setChecked(true);
    QVERIFY(window.hasPendingPreferences());

    toggle->setChecked(false);
    QTRY_VERIFY(!startup.service.isApplying());
    QVERIFY(!startup.service.status().enabled && !toggle->isChecked());
    QCOMPARE(startup.changes(), 2);
    QVERIFY(!autoConnect->isChecked() && autoHide->isChecked());
    QVERIFY(window.hasPendingPreferences());
    QVERIFY(Settings::value(Settings::Gui::AutoStartCore).toBool());
    QVERIFY(!Settings::value(Settings::Gui::Autohide).toBool());
    QCOMPARE(preferences.count(), 0);
    QCOMPARE(starts.count(), 0);
    QCOMPARE(stops.count(), 0);
    QCOMPARE(settingsChanged.count(), 0);
  }

  void loginStartupFailureRestoresActualState()
  {
    FakeStartup startup({true, true, false, {}, {}});
    startup.failChanges();
    PersonalWindow window(nullptr, &startup.service);
    auto *toggle = window.findChild<QCheckBox *>(QStringLiteral("loginStartupSwitch"));
    auto *hint = window.findChild<QLabel *>(QStringLiteral("loginStartupHint"));
    QVERIFY(toggle && hint);
    toggle->setChecked(false);
    QTRY_VERIFY(!startup.service.isApplying());
    QVERIFY(startup.service.status().enabled && toggle->isChecked());
    QVERIFY(toggle->isEnabled());
    QVERIFY(hint->text().contains(QStringLiteral("模拟登录项修改失败")));
    QVERIFY(!window.hasPendingPreferences());
  }

  void loginStartupApprovalShowsAnEnabledRequestAndOpensOnlyOnClick()
  {
    FakeStartup startup;
    startup.requireApproval();
    PersonalWindow window(nullptr, &startup.service);
    auto *toggle = window.findChild<QCheckBox *>(QStringLiteral("loginStartupSwitch"));
    auto *hint = window.findChild<QLabel *>(QStringLiteral("loginStartupHint"));
    auto *settings = window.findChild<QPushButton *>(QStringLiteral("loginStartupSystemSettings"));
    QVERIFY(toggle && hint && settings);
    QVERIFY(settings->isHidden());
    toggle->setChecked(true);
    QTRY_VERIFY(!startup.service.isApplying());
    QVERIFY(startup.service.status().approvalRequired);
    QVERIFY(toggle->isChecked());
    QVERIFY(!settings->isHidden());
    QVERIFY(hint->text().contains(QStringLiteral("仍需在系统设置中允许")));
    QCOMPARE(startup.opened(), 0);
    settings->click();
    QCOMPARE(startup.opened(), 1);
  }

  void loginStartupUnsupportedAndBusyStatesCannotBeChanged()
  {
    FakeStartup unsupported({false, false, false, QStringLiteral("当前平台暂不支持"), {}});
    PersonalWindow unsupportedWindow(nullptr, &unsupported.service);
    auto *unavailable = unsupportedWindow.findChild<QCheckBox *>(QStringLiteral("loginStartupSwitch"));
    QVERIFY(unavailable && !unavailable->isEnabled());
    unavailable->setChecked(true);
    QVERIFY(!unavailable->isChecked());
    QCOMPARE(unsupported.changes(), 0);
    QVERIFY(unsupportedWindow.findChild<QPushButton *>(QStringLiteral("loginStartupSystemSettings"))->isHidden());

    FakeStartup startup;
    PersonalWindow window(nullptr, &startup.service);
    auto *toggle = window.findChild<QCheckBox *>(QStringLiteral("loginStartupSwitch"));
    QVERIFY(toggle);
    window.setConfigurationBusy(true);
    QVERIFY(!toggle->isEnabled());
    toggle->setChecked(true);
    QVERIFY(!toggle->isChecked());
    QCOMPARE(startup.changes(), 0);
    window.setConfigurationBusy(false);
    QVERIFY(toggle->isEnabled());
    toggle->setChecked(true);
    QVERIFY(startup.service.isApplying());
    QVERIFY(!toggle->isEnabled());
    toggle->setChecked(true);
    QTRY_VERIFY(!startup.service.isApplying());
    QCOMPARE(startup.changes(), 1);
    QVERIFY(toggle->isChecked() && toggle->isEnabled());
  }

  void loginStartupRefreshesExternalChangesWithoutNetworkReadsOrDraftLoss()
  {
    FakeStartup startup;
    PersonalWindow window(nullptr, &startup.service);
    auto *toggle = window.findChild<QCheckBox *>(QStringLiteral("loginStartupSwitch"));
    auto *autoConnect = window.findChild<QCheckBox *>(QStringLiteral("automaticSharingSwitch"));
    QVERIFY(toggle && autoConnect);
    autoConnect->setChecked(false);
    const int reads = startup.reads();
    window.setConnectionStatus(deskflow::core::ConnectionState::Connected, deskflow::core::ProcessState::Started,
                               true, true, true, "192.0.2.1", "test-client");
    window.reloadPreferences();
    QCOMPARE(startup.reads(), reads);
    startup.setExternalStatus({true, true, false, {}, {}});
    window.showPage(PersonalWindow::General);
    QTRY_VERIFY(toggle->isChecked());
    QVERIFY(startup.reads() > reads);
    QVERIFY(window.hasPendingPreferences() && !autoConnect->isChecked());
    startup.setExternalStatus({true, false, false, {}, {}});
    window.showPage(PersonalWindow::General);
    QTRY_VERIFY(!toggle->isChecked());
    window.showPage(PersonalWindow::Connection);
    startup.setExternalStatus({true, true, false, {}, {}});
    QEvent activation(QEvent::WindowActivate);
    QCoreApplication::sendEvent(&window, &activation);
    QTRY_VERIFY(toggle->isChecked());
    QVERIFY(window.hasPendingPreferences() && !autoConnect->isChecked());
    QCOMPARE(startup.changes(), 0);
  }

  void loginStartupReadErrorDoesNotClaimOffAndRecoversAfterRefresh()
  {
    FakeStartup startup({true, false, false, QStringLiteral("已关闭（旧缓存）"), QStringLiteral("模拟系统状态读取失败")});
    PersonalWindow window(nullptr, &startup.service);
    auto *toggle = window.findChild<QCheckBox *>(QStringLiteral("loginStartupSwitch"));
    auto *hint = window.findChild<QLabel *>(QStringLiteral("loginStartupHint"));
    QVERIFY(toggle && hint);
    QVERIFY(!toggle->isEnabled());
    QVERIFY(hint->text().startsWith(QStringLiteral("暂时无法确认登录启动状态")));
    QVERIFY(hint->text().contains(QStringLiteral("模拟系统状态读取失败")));
    QVERIFY(!hint->text().contains(QStringLiteral("已关闭")));
    toggle->setChecked(true);
    QVERIFY(!toggle->isChecked());
    QCOMPARE(startup.changes(), 0);

    startup.setExternalStatus({true, true, false, {}, {}});
    window.showPage(PersonalWindow::General);
    QTRY_VERIFY(toggle->isEnabled() && toggle->isChecked());
    QVERIFY(hint->text().startsWith(QStringLiteral("已开启")));
    toggle->setChecked(false);
    QTRY_VERIFY(!startup.service.isApplying());
    QVERIFY(toggle->isEnabled() && !toggle->isChecked());
    QCOMPARE(startup.changes(), 1);
    QVERIFY(!window.hasPendingPreferences());
  }

private:
  QTemporaryDir m_profile;
};

QTEST_MAIN(PersonalWindowTests)
#include "PersonalWindowTests.moc"
