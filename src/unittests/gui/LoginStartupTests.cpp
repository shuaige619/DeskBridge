/*
 * SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "gui/personal/LoginStartup.h"
#include "gui/personal/LoginStartup_p.h"

#include <QSemaphore>
#include <QSignalSpy>
#include <QTest>
#include <QThread>
#include <atomic>
#include <memory>
#include <stdexcept>

using deskflow::gui::personal::LoginStartup;

namespace {
LoginStartup::Status supported(bool enabled = false)
{
  LoginStartup::Status status;
  status.supported = true;
  status.enabled = enabled;
  status.message = enabled ? QStringLiteral("enabled") : QStringLiteral("disabled");
  return status;
}
} // namespace

class LoginStartupTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void modernRegistrationAndLegacyState_data()
  {
    using State = deskflow::gui::personal::detail::LoginServiceState;
    QTest::addColumn<int>("modern");
    QTest::addColumn<bool>("hasLegacy");
    QTest::addColumn<bool>("legacyKnown");
    QTest::addColumn<bool>("legacyEnabled");
    QTest::addColumn<bool>("legacyApproval");
    QTest::addColumn<bool>("known");
    QTest::addColumn<bool>("enabled");
    QTest::addColumn<bool>("approval");
    QTest::newRow("first-modern-discovery")
        << int(State::NotFound) << false << false << false << false << true << false << false;
    QTest::newRow("first-modern-with-verified-legacy-enabled")
        << int(State::NotFound) << true << true << true << false << true << true << false;
    QTest::newRow("first-modern-with-legacy-approval")
        << int(State::NotFound) << true << true << true << true << true << true << true;
    QTest::newRow("first-modern-with-disabled-legacy")
        << int(State::NotFound) << true << true << false << false << true << false << false;
    QTest::newRow("first-modern-with-unknown-legacy")
        << int(State::NotFound) << true << false << false << false << false << false << false;
    QTest::newRow("modern-enabled-independent-of-legacy-override")
        << int(State::Enabled) << true << true << false << false << true << true << false;
    QTest::newRow("modern-approval-is-not-masked-by-enabled-legacy")
        << int(State::RequiresApproval) << true << true << true << false << true << true << true;
    QTest::newRow("modern-unregistered-falls-back-to-legacy")
        << int(State::NotRegistered) << true << true << true << false << true << true << false;
    QTest::newRow("genuinely-unknown-modern-is-not-hidden")
        << int(State::Unknown) << true << true << true << false << false << false << false;
  }

  void modernRegistrationAndLegacyState()
  {
    using namespace deskflow::gui::personal::detail;
    QFETCH(int, modern);
    QFETCH(bool, hasLegacy);
    QFETCH(bool, legacyKnown);
    QFETCH(bool, legacyEnabled);
    QFETCH(bool, legacyApproval);
    QFETCH(bool, known);
    QFETCH(bool, enabled);
    QFETCH(bool, approval);
    std::optional<LoginRegistration> legacy;
    if (hasLegacy)
      legacy = LoginRegistration{legacyKnown, legacyEnabled, legacyApproval};
    const auto result = effectiveLoginRegistration(static_cast<LoginServiceState>(modern), legacy);
    QCOMPARE(result.known, known);
    QCOMPARE(result.enabled, enabled);
    QCOMPARE(result.approvalRequired, approval);
  }

  void modernChangeAction_data()
  {
    using State = deskflow::gui::personal::detail::LoginServiceState;
    using Action = deskflow::gui::personal::detail::LoginServiceAction;
    QTest::addColumn<int>("state");
    QTest::addColumn<bool>("enabled");
    QTest::addColumn<int>("action");
    QTest::newRow("first-enable-registers") << int(State::NotFound) << true << int(Action::Register);
    QTest::newRow("legacy-only-disable-does-not-unregister-unseen-modern")
        << int(State::NotFound) << false << int(Action::None);
    QTest::newRow("unregistered-enable") << int(State::NotRegistered) << true << int(Action::Register);
    QTest::newRow("unregistered-disable") << int(State::NotRegistered) << false << int(Action::None);
    QTest::newRow("enabled-enable") << int(State::Enabled) << true << int(Action::None);
    QTest::newRow("enabled-disable") << int(State::Enabled) << false << int(Action::Unregister);
    QTest::newRow("approval-never-reregisters-to-bypass-consent")
        << int(State::RequiresApproval) << true << int(Action::None);
    QTest::newRow("approval-disable") << int(State::RequiresApproval) << false << int(Action::Unregister);
    QTest::newRow("unknown-enable-rejected") << int(State::Unknown) << true << int(Action::Reject);
    QTest::newRow("unknown-disable-rejected") << int(State::Unknown) << false << int(Action::Reject);
  }

  void modernChangeAction()
  {
    using namespace deskflow::gui::personal::detail;
    QFETCH(int, state);
    QFETCH(bool, enabled);
    QFETCH(int, action);
    QCOMPARE(int(modernLoginAction(static_cast<LoginServiceState>(state), enabled)), action);
  }

  void legacyDisabledOutput_data()
  {
    QTest::addColumn<QByteArray>("output");
    QTest::addColumn<bool>("known");
    QTest::addColumn<bool>("disabled");
    const QByteArray label = "local.shuai.deskbridge.login";
    const auto dictionary = [](const QByteArray &entries) { return "disabled services = {\n" + entries + "\n}\n"; };
    const auto entry = [&](const QByteArray &token) { return "\t\"" + label + "\" => " + token; };
    QTest::newRow("macos27-enabled") << dictionary(entry("enabled")) << true << false;
    QTest::newRow("macos27-disabled") << dictionary(entry("disabled")) << true << true;
    QTest::newRow("legacy-true") << dictionary(entry("true")) << true << true;
    QTest::newRow("legacy-false") << dictionary(entry("false")) << true << false;
    QTest::newRow("mixed-token-versions")
        << dictionary("\t\"other.service\" => true\n" + entry("enabled")) << true << false;
    QTest::newRow("no-exact-label") << dictionary("\t\"other.service\" => disabled") << true << false;
    QTest::newRow("empty-overrides") << dictionary("") << true << false;
    QTest::newRow("prefix-and-suffix-impostors")
        << dictionary("\t\"prefix." + label + "\" => disabled\n\t\"" + label + ".other\" => true") << true << false;
    QTest::newRow("unknown-token") << dictionary(entry("pending")) << false << false;
    QTest::newRow("unknown-unrelated-token")
        << dictionary(entry("enabled") + "\n\t\"other.service\" => pending") << false << false;
    QTest::newRow("duplicate-exact-label") << dictionary(entry("enabled") + "\n" + entry("disabled")) << false << false;
    QTest::newRow("duplicate-same-value") << dictionary(entry("true") + "\n" + entry("true")) << false << false;
    QTest::newRow("duplicate-unrelated-label")
        << dictionary("\t\"other.service\" => true\n\t\"other.service\" => false") << false << false;
    QTest::newRow("bad-header") << QByteArray("enabled services = {\n" + entry("true") + "\n}\n") << false << false;
    QTest::newRow("missing-closing-brace") << QByteArray("disabled services = {\n" + entry("true")) << false << false;
    QTest::newRow("trailing-output") << QByteArray(dictionary(entry("true")) + "unexpected") << false << false;
    QTest::newRow("empty-output") << QByteArray{} << false << false;
    QTest::newRow("oversized-output") << QByteArray(1024 * 1024 + 1, ' ') << false << false;
    QTest::newRow("crlf-whitespace")
        << QByteArray(" \r\ndisabled services = {\r\n  \"" + label + "\" => enabled\r\n}\r\n") << true << false;
  }

  void legacyDisabledOutput()
  {
    QFETCH(QByteArray, output);
    QFETCH(bool, known);
    QFETCH(bool, disabled);
    const auto result = deskflow::gui::personal::detail::parseLegacyDisabledOutput(
        output, QStringLiteral("local.shuai.deskbridge.login"));
    QCOMPARE(result.known, known);
    QCOMPARE(result.disabled, disabled);
  }

  void initialAndRefreshedStateComeFromOperations()
  {
    auto actual = supported();
    int reads = 0;
    int changes = 0;
    LoginStartup service({[&] { ++reads; return actual; },
                          [&](bool) { ++changes; return LoginStartup::ChangeResult{}; }, {}});
    QCOMPARE(reads, 1);
    QVERIFY(!service.status().enabled);
    QSignalSpy statusSpy(&service, &LoginStartup::statusChanged);
    service.refresh();
    QCOMPARE(statusSpy.count(), 0);
    actual = supported(true);
    service.refresh();
    QCOMPARE(statusSpy.count(), 1);
    QVERIFY(service.status() == actual);
    QCOMPARE(changes, 0);
  }

  void unsupportedNeverChangesOrOpensSettings()
  {
    int changes = 0;
    int opens = 0;
    LoginStartup service({[] { return LoginStartup::Status{}; },
                          [&](bool) { ++changes; return LoginStartup::ChangeResult{}; }, [&] { ++opens; }});
    QSignalSpy finished(&service, &LoginStartup::finished);
    service.setEnabled(true);
    service.openSystemSettings();
    QCOMPARE(changes, 0);
    QCOMPARE(opens, 0);
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.first().first().toBool(), false);
    QVERIFY(!service.isApplying());
  }

  void readErrorNeverInvokesChange()
  {
    int changes = 0;
    auto unknown = supported();
    unknown.error = QStringLiteral("state query failed");
    LoginStartup service({[unknown] { return unknown; },
                          [&](bool) { ++changes; return LoginStartup::ChangeResult{}; }, {}});
    QSignalSpy finished(&service, &LoginStartup::finished);
    service.setEnabled(true);
    service.setEnabled(false);
    QCOMPARE(changes, 0);
    QCOMPARE(finished.count(), 2);
    QCOMPARE(finished.first().at(1).toString(), unknown.error);
    QVERIFY(!service.isApplying());
  }

  void changeRunsOutsideGuiAndReturnsThroughGuiThread()
  {
    auto workerThread = std::make_shared<std::atomic<QThread *>>(nullptr);
    auto initial = supported();
    const auto target = supported(true);
    LoginStartup service({[initial] { return initial; },
                          [workerThread, target](bool enabled) {
                            workerThread->store(QThread::currentThread());
                            return LoginStartup::ChangeResult{enabled, target, {}};
                          }, {}});
    auto *guiThread = QThread::currentThread();
    QThread *finishedThread = nullptr;
    connect(&service, &LoginStartup::finished, &service, [&] { finishedThread = QThread::currentThread(); });
    QSignalSpy applying(&service, &LoginStartup::applyingChanged);
    QSignalSpy finished(&service, &LoginStartup::finished);
    service.setEnabled(true);
    QVERIFY(service.isApplying());
    QTRY_COMPARE(finished.count(), 1);
    QVERIFY(workerThread->load() != guiThread);
    QCOMPARE(finishedThread, guiThread);
    QVERIFY(service.status() == target);
    QVERIFY(!service.isApplying());
    QCOMPARE(applying.count(), 2);
    QCOMPARE(applying.at(0).at(0).toBool(), true);
    QCOMPARE(applying.at(1).at(0).toBool(), false);
    QCOMPARE(finished.first().first().toBool(), true);
  }

  void pendingApprovalIsNotReportedAsEligible()
  {
    auto pending = supported(true);
    pending.approvalRequired = true;
    pending.message = QStringLiteral("approval required");
    LoginStartup service({[] { return supported(); },
                          [pending](bool) { return LoginStartup::ChangeResult{true, pending, {}}; }, {}});
    QSignalSpy finished(&service, &LoginStartup::finished);
    service.setEnabled(true);
    QTRY_COMPARE(finished.count(), 1);
    QVERIFY(service.status().enabled);
    QVERIFY(service.status().approvalRequired);
    QCOMPARE(finished.first().at(1).toString(), pending.message);
  }

  void failedChangeStillPublishesActualStateAndError()
  {
    const auto actual = supported(true);
    LoginStartup service({[] { return supported(); },
                          [actual](bool) { return LoginStartup::ChangeResult{false, actual, QStringLiteral("native domain:3")}; }, {}});
    QSignalSpy finished(&service, &LoginStartup::finished);
    service.setEnabled(true);
    QTRY_COMPARE(finished.count(), 1);
    QVERIFY(service.status() == actual);
    QCOMPARE(finished.first().first().toBool(), false);
    QCOMPARE(finished.first().at(1).toString(), QStringLiteral("native domain:3"));
  }

  void duplicateChangesAndRefreshAreIgnoredWhileApplying()
  {
    auto entered = std::make_shared<QSemaphore>();
    auto release = std::make_shared<QSemaphore>();
    auto changes = std::make_shared<std::atomic<int>>(0);
    int reads = 0;
    LoginStartup service({[&] { ++reads; return supported(); },
                          [entered, release, changes](bool enabled) {
                            ++(*changes);
                            entered->release();
                            release->acquire();
                            return LoginStartup::ChangeResult{true, supported(enabled), {}};
                          }, {}});
    QSignalSpy finished(&service, &LoginStartup::finished);
    service.setEnabled(true);
    const bool workerEntered = entered->tryAcquire(1, 3000);
    // Always release a potentially delayed worker, even if the assertion fails.
    service.setEnabled(false);
    service.refresh();
    const auto readsWhileBusy = reads;
    release->release();
    QVERIFY(workerEntered);
    QCOMPARE(readsWhileBusy, 1);
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(changes->load(), 1);
    QVERIFY(service.status().enabled);
  }

  void exceptionsReturnFailureAndFreshReadState()
  {
    const auto actual = supported(true);
    LoginStartup service({[actual] { return actual; },
                          [](bool) -> LoginStartup::ChangeResult { throw std::runtime_error("fake"); }, {}});
    QSignalSpy finished(&service, &LoginStartup::finished);
    service.setEnabled(false);
    QTRY_COMPARE(finished.count(), 1);
    QVERIFY(service.status() == actual);
    QVERIFY(!finished.first().first().toBool());
    QVERIFY(!finished.first().at(1).toString().isEmpty());
  }

  void settingsOnlyOpenOnExplicitRequest()
  {
    int opens = 0;
    LoginStartup service({[] { return supported(); }, {}, [&] { ++opens; }});
    service.refresh();
    QCOMPARE(opens, 0);
    service.openSystemSettings();
    QCOMPARE(opens, 1);
  }

  void destroyingUiDoesNotInvalidateWorkerOperations()
  {
    auto entered = std::make_shared<QSemaphore>();
    auto release = std::make_shared<QSemaphore>();
    auto completed = std::make_shared<QSemaphore>();
    auto *service = new LoginStartup({[] { return supported(); },
                                     [entered, release, completed](bool enabled) {
                                       entered->release();
                                       release->acquire();
                                       completed->release();
                                       return LoginStartup::ChangeResult{true, supported(enabled), {}};
                                     }, {}});
    service->setEnabled(true);
    const bool workerEntered = entered->tryAcquire(1, 3000);
    delete service;
    release->release();
    QVERIFY(workerEntered);
    QVERIFY(completed->tryAcquire(1, 3000));
    QCoreApplication::processEvents();
  }
};

QTEST_GUILESS_MAIN(LoginStartupTests)
#include "LoginStartupTests.moc"
