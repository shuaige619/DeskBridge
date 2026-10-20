/*
 * SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "common/Settings.h"
#include "gui/personal/AdvancedPage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextDocumentLayout>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QtTest>

using deskflow::gui::personal::AdvancedPage;

namespace {
// A public certificate fixture only. Its private key is not part of the tests.
const QByteArray kLegacyPublicCertificate = QByteArrayLiteral(
    "-----BEGIN CERTIFICATE-----\n"
    "MIIB4TCCAUqgAwIBAgIUXK7bICCPgUJDtxrDHeOLHIW1cBYwDQYJKoZIhvcNAQEL\n"
    "BQAwEzERMA8GA1UEAwwIRGVza2Zsb3cwHhcNMjYxMDIwMDk1MTQ0WhcNMzYxMDE3\n"
    "MDk1MTQ0WjATMREwDwYDVQQDDAhEZXNrZmxvdzCBnzANBgkqhkiG9w0BAQEFAAOB\n"
    "jQAwgYkCgYEAxtJGUfRnIHk6jjTad3JEf+MV6dMVyokCOv5FStbeixKkFIKzN71F\n"
    "grGkUEKFHDKvqiBLyp/Z/YKmAWW/l6sNN9HrcBcxn92YBd4q4elfcmcjqalLNO19\n"
    "ZgOIbsxTIhIt+w4D+hSuFc3dl4ctAMLAzxYudoA5azZ2lF8q2B+rs7sCAwEAAaMy\n"
    "MDAwHQYDVR0OBBYEFAZwjWQEF+ETiullCxqBB1zjt9YcMA8GA1UdEwEB/wQFMAMB\n"
    "Af8wDQYJKoZIhvcNAQELBQADgYEAbr2aDyifJW8HPLNN+DatgG4Br7ymY3Stjz6M\n"
    "u8IovUL2HlRfZ8oB7oev0v97VF7zjFm3hEfUhX/W+9QefWTCh8amCVmHCyDbbKaY\n"
    "Hqz7lb8nTkTpOIBJpaKvS5bGRIL65qa3ws+Y67smhmkjbFZrO58PWswbsrcEbGr7\n"
    "SHpLyho=\n"
    "-----END CERTIFICATE-----\n");
} // namespace

class AdvancedPageTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase()
  {
    if (QGuiApplication::platformName() != QStringLiteral("offscreen") &&
        QGuiApplication::platformName() != QStringLiteral("minimal"))
      QSKIP("Widget regression tests require QT_QPA_PLATFORM=offscreen or minimal.");
    QVERIFY(m_temp.isValid());
    qputenv("XDG_CONFIG_HOME", m_temp.filePath(QStringLiteral("config")).toUtf8());
    qputenv("XDG_STATE_HOME", m_temp.filePath(QStringLiteral("state")).toUtf8());
  }

  void init()
  {
    const auto path = m_temp.filePath(QStringLiteral("profile-%1").arg(m_profile++));
    QVERIFY(QDir().mkpath(path));
    Settings::setSettingsFile(path + QStringLiteral("/settings.conf"));
    Settings::setStateFile(path + QStringLiteral("/state.conf"));
    Settings::setValue(Settings::Core::CoreMode, Settings::CoreMode::Server);
    Settings::setValue(Settings::Security::TlsEnabled, true);
    Settings::setValue(Settings::Security::CheckPeers, true);
    Settings::setValue(Settings::Security::Certificate, path + QStringLiteral("/identity.pem"));
    Settings::setValue(Settings::Security::KeySize, 2048);
    Settings::setValue(Settings::Core::Port, 24800);
    Settings::setValue(Settings::Core::Interface, QStringLiteral("192.0.2.50"));
    Settings::setValue(Settings::Log::Level, QStringLiteral("INFO"));
    Settings::setValue(Settings::Log::GuiDebug, false);
    Settings::setValue(Settings::Log::ToFile, false);
    Settings::setValue(Settings::Log::File, path + QStringLiteral("/runtime.log"));
    Settings::setValue(Settings::Core::EnableEnterCommand, false);
    Settings::setValue(Settings::Core::EnableExitCommand, false);
    Settings::setValue(Settings::Core::ComputerEnterCommand, QStringLiteral("echo retained-enter"));
    Settings::setValue(Settings::Core::ComputerExitCommand, QStringLiteral("echo retained-exit"));
    Settings::save(false);
    QVERIFY(Settings::isWritable());
  }

  void choosingCertificateParametersAndOrdinaryFieldsNeverWritesSettings()
  {
    const auto bytes = settingsBytes();
    const auto savedPath = Settings::value(Settings::Security::Certificate).toString();
    AdvancedPage page;
    auto *path = child<QLineEdit>(page, "advancedCertificatePath");
    auto *bits = child<QComboBox>(page, "advancedKeySize");
    auto *port = child<QSpinBox>(page, "advancedPort");
    auto *interface = child<QComboBox>(page, "advancedInterface");
    QVERIFY(path && bits && port && interface);
    // A previously saved binding remains available even when this computer no longer has that address.
    QCOMPARE(interface->currentData().toString(), QStringLiteral("192.0.2.50"));
    QSignalSpy changed(Settings::instance(), &Settings::settingsChanged);
    QSignalSpy apply(&page, &AdvancedPage::applyRequested);
    QSignalSpy regenerate(&page, &AdvancedPage::regenerateCertificateRequested);

    path->setText(savedPath + QStringLiteral(".new.pem"));
    bits->setCurrentIndex(bits->findData(4096));
    QVERIFY(!page.isDirty());
    QVERIFY(page.hasPendingEdits());
    port->setValue(24801);
    QVERIFY(page.isDirty());

    QCOMPARE(Settings::value(Settings::Security::Certificate).toString(), savedPath);
    QCOMPARE(Settings::value(Settings::Security::KeySize).toInt(), 2048);
    QCOMPARE(Settings::value(Settings::Core::Port).toInt(), 24800);
    QCOMPARE(changed.count(), 0);
    QCOMPARE(apply.count(), 0);
    QCOMPARE(regenerate.count(), 0);
    QCOMPARE(settingsBytes(), bytes);
  }

  void ordinaryApplyEmitsOnlyChangedFieldsAndExcludesCertificateIdentity()
  {
    const auto bytes = settingsBytes();
    AdvancedPage page;
    auto *tls = child<QCheckBox>(page, "advancedTlsEnabled");
    auto *port = child<QSpinBox>(page, "advancedPort");
    auto *path = child<QLineEdit>(page, "advancedCertificatePath");
    auto *bits = child<QComboBox>(page, "advancedKeySize");
    auto *apply = child<QPushButton>(page, "advancedApply");
    QVERIFY(tls && port && path && bits && apply);
    QSignalSpy requested(&page, &AdvancedPage::applyRequested);
    QSignalSpy changed(Settings::instance(), &Settings::settingsChanged);
    tls->setChecked(false);
    port->setValue(24801);
    port->setValue(24800);
    path->setText(path->text() + QStringLiteral(".new.pem"));
    bits->setCurrentIndex(bits->findData(4096));
    QVERIFY(apply->isEnabled());

    apply->click();

    QCOMPARE(requested.count(), 1);
    const auto changes = requested.first().first().toMap();
    QCOMPARE(changes.size(), 1);
    QVERIFY(changes.contains(Settings::Security::TlsEnabled));
    QCOMPARE(changes.value(Settings::Security::TlsEnabled).toBool(), false);
    QVERIFY(!changes.contains(Settings::Security::Certificate));
    QVERIFY(!changes.contains(Settings::Security::KeySize));
    QVERIFY(page.isDirty()); // The owner has not acknowledged or persisted the request.
    QCOMPARE(changed.count(), 0);
    QCOMPARE(settingsBytes(), bytes);
  }

  void revertClearsBothDraftsWithoutChangingPreviouslySavedSettings()
  {
    const auto bytes = settingsBytes();
    const auto savedPath = Settings::value(Settings::Security::Certificate).toString();
    AdvancedPage page;
    auto *port = child<QSpinBox>(page, "advancedPort");
    auto *path = child<QLineEdit>(page, "advancedCertificatePath");
    auto *bits = child<QComboBox>(page, "advancedKeySize");
    auto *revert = child<QPushButton>(page, "advancedRevert");
    QVERIFY(port && path && bits && revert);
    QSignalSpy changed(Settings::instance(), &Settings::settingsChanged);
    port->setValue(24999);
    path->setText(savedPath + QStringLiteral(".new.pem"));
    bits->setCurrentIndex(bits->findData(4096));
    QVERIFY(revert->isEnabled());

    revert->click();

    QVERIFY(!page.isDirty());
    QVERIFY(!page.hasPendingEdits());
    QVERIFY(!revert->isEnabled());
    QCOMPARE(port->value(), 24800);
    QCOMPARE(path->text(), savedPath);
    QCOMPARE(bits->currentData().toInt(), 2048);
    QCOMPARE(changed.count(), 0);
    QCOMPARE(settingsBytes(), bytes);
  }

  void applyingBlocksEditsApplyAndMaintenanceUntilItFinishes()
  {
    const auto bytes = settingsBytes();
    AdvancedPage page;
    auto *port = child<QSpinBox>(page, "advancedPort");
    auto *path = child<QLineEdit>(page, "advancedCertificatePath");
    auto *bits = child<QComboBox>(page, "advancedKeySize");
    auto *apply = child<QPushButton>(page, "advancedApply");
    auto *regenerate = child<QPushButton>(page, "advancedRegenerateCertificate");
    auto *reset = child<QPushButton>(page, "advancedResetAll");
    auto *revert = child<QPushButton>(page, "advancedRevert");
    QVERIFY(port && path && bits && apply && regenerate && reset && revert);
    QSignalSpy requested(&page, &AdvancedPage::applyRequested);
    QSignalSpy regenerated(&page, &AdvancedPage::regenerateCertificateRequested);
    QSignalSpy cleared(&page, &AdvancedPage::resetAllRequested);
    QSignalSpy changed(Settings::instance(), &Settings::settingsChanged);
    port->setValue(24801);

    page.setApplying(true);

    QVERIFY(!port->isEnabled());
    QVERIFY(!path->isEnabled());
    QVERIFY(!bits->isEnabled());
    QVERIFY(!apply->isEnabled());
    QVERIFY(!regenerate->isEnabled());
    QVERIFY(!reset->isEnabled());
    QVERIFY(!revert->isEnabled());
    port->setValue(24999); // Defensive path: a native accessibility update must not alter a busy draft.
    QCOMPARE(port->value(), 24801);
    apply->click();
    regenerate->click();
    reset->click();
    QCOMPARE(requested.count(), 0);
    QCOMPARE(regenerated.count(), 0);
    QCOMPARE(cleared.count(), 0);
    QCOMPARE(changed.count(), 0);
    QCOMPARE(settingsBytes(), bytes);
    page.setApplying(false);
    QVERIFY(port->isEnabled());
    QVERIFY(apply->isEnabled());
    QVERIFY(reset->isEnabled());
    QVERIFY(page.isDirty());
  }

  void readOnlyProfileDisablesOrdinaryChangesAndDestructiveMaintenance()
  {
    const auto config = Settings::settingsFile();
    const auto permissions = QFile::permissions(config);
    const auto restorePermissions = qScopeGuard([config, permissions] { QFile::setPermissions(config, permissions); });
    QVERIFY(QFile::setPermissions(config, QFileDevice::ReadOwner | QFileDevice::ReadUser |
                                         QFileDevice::ReadGroup | QFileDevice::ReadOther));
    if (Settings::isWritable()) QSKIP("This environment does not enforce read-only file permissions.");
    const auto bytes = settingsBytes();
    AdvancedPage page;
    auto *port = child<QSpinBox>(page, "advancedPort");
    auto *path = child<QLineEdit>(page, "advancedCertificatePath");
    auto *bits = child<QComboBox>(page, "advancedKeySize");
    auto *apply = child<QPushButton>(page, "advancedApply");
    auto *regenerate = child<QPushButton>(page, "advancedRegenerateCertificate");
    auto *reset = child<QPushButton>(page, "advancedResetAll");
    QVERIFY(port && path && bits && apply && regenerate && reset);
    QVERIFY(!port->isEnabled());
    QVERIFY(!path->isEnabled());
    QVERIFY(!bits->isEnabled());
    QVERIFY(!apply->isEnabled());
    QVERIFY(!regenerate->isEnabled());
    QVERIFY(!reset->isEnabled());
    QSignalSpy requested(&page, &AdvancedPage::applyRequested);
    QSignalSpy regenerated(&page, &AdvancedPage::regenerateCertificateRequested);
    QSignalSpy cleared(&page, &AdvancedPage::resetAllRequested);
    QSignalSpy changed(Settings::instance(), &Settings::settingsChanged);
    port->setValue(24999);
    QCOMPARE(port->value(), 24800);
    apply->click();
    regenerate->click();
    reset->click();
    QVERIFY(!page.isDirty());
    QCOMPARE(requested.count(), 0);
    QCOMPARE(regenerated.count(), 0);
    QCOMPARE(cleared.count(), 0);
    QCOMPARE(changed.count(), 0);
    QCOMPARE(settingsBytes(), bytes);
  }

  void externalRefreshPreservesUnsavedOrdinaryAndMaintenanceDrafts()
  {
    AdvancedPage page;
    auto *port = child<QSpinBox>(page, "advancedPort");
    auto *path = child<QLineEdit>(page, "advancedCertificatePath");
    auto *bits = child<QComboBox>(page, "advancedKeySize");
    QVERIFY(port && path && bits);
    const auto draftPath = path->text() + QStringLiteral(".draft.pem");
    port->setValue(24801);
    path->setText(draftPath);
    bits->setCurrentIndex(bits->findData(4096));
    Settings::setValue(Settings::Core::Port, 24802);
    Settings::setValue(Settings::Security::Certificate, m_temp.filePath(QStringLiteral("external.pem")));
    Settings::save(false);
    const auto externalBytes = settingsBytes();

    page.refreshFromSettings();

    QCOMPARE(port->value(), 24801);
    QCOMPARE(path->text(), draftPath);
    QCOMPARE(bits->currentData().toInt(), 4096);
    QVERIFY(page.isDirty());
    QVERIFY(page.hasPendingEdits());
    QCOMPARE(settingsBytes(), externalBytes);
  }

  void maintenanceOnlyDraftSurvivesRefreshAndCleanPageReadsExternalChanges()
  {
    AdvancedPage page;
    auto *path = child<QLineEdit>(page, "advancedCertificatePath");
    auto *port = child<QSpinBox>(page, "advancedPort");
    auto *revert = child<QPushButton>(page, "advancedRevert");
    QVERIFY(path && port && revert);
    const auto draftPath = path->text() + QStringLiteral(".draft.pem");
    path->setText(draftPath);
    QVERIFY(!page.isDirty());
    QVERIFY(page.hasPendingEdits());
    Settings::setValue(Settings::Core::Port, 24802);

    page.refreshFromSettings();

    QCOMPARE(path->text(), draftPath);
    QCOMPARE(port->value(), 24800);
    QVERIFY(page.hasPendingEdits());
    revert->click();
    QVERIFY(!page.hasPendingEdits());
    page.refreshFromSettings();
    QCOMPARE(port->value(), 24802);
    QVERIFY(!page.hasPendingEdits());
  }

  void cancellingCertificateMaintenanceLeavesIdentityAndDraftUntouched()
  {
    const auto certificatePath = Settings::value(Settings::Security::Certificate).toString();
    {
      QFile certificate(certificatePath);
      QVERIFY(certificate.open(QIODevice::WriteOnly));
      QCOMPARE(certificate.write(kLegacyPublicCertificate), kLegacyPublicCertificate.size());
    }
    const auto bytes = settingsBytes();
    AdvancedPage page;
    auto *bits = child<QComboBox>(page, "advancedKeySize");
    auto *regenerate = child<QPushButton>(page, "advancedRegenerateCertificate");
    QVERIFY(bits && regenerate);
    bits->setCurrentIndex(bits->findData(4096));
    QSignalSpy requested(&page, &AdvancedPage::regenerateCertificateRequested);
    QSignalSpy changed(Settings::instance(), &Settings::settingsChanged);
    bool visited = false;
    QTimer::singleShot(0, &page, [&] {
      auto *question = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
      if (!question) return;
      visited = true;
      const auto closeQuestion = qScopeGuard([question] { question->reject(); });
      QVERIFY(question->text().contains(QString::fromUtf8(kAppName)));
      QVERIFY(question->text().contains(QStringLiteral("指纹会改变")));
      QVERIFY(question->text().contains(QStringLiteral("重新信任")));
    });

    regenerate->click();

    QVERIFY(visited);
    QCOMPARE(requested.count(), 0);
    QCOMPARE(changed.count(), 0);
    QCOMPARE(Settings::value(Settings::Security::KeySize).toInt(), 2048);
    QCOMPARE(bits->currentData().toInt(), 4096);
    QVERIFY(page.hasPendingEdits());
    QCOMPARE(settingsBytes(), bytes);
    QFile certificate(certificatePath);
    QVERIFY(certificate.open(QIODevice::ReadOnly));
    QCOMPARE(certificate.readAll(), kLegacyPublicCertificate);
  }

  void certificateNamingExplainsCurrentBrandAndPreservesLegacyIdentity()
  {
    const auto certificatePath = Settings::value(Settings::Security::Certificate).toString();
    {
      QFile certificate(certificatePath);
      QVERIFY(certificate.open(QIODevice::WriteOnly));
      QCOMPARE(certificate.write(kLegacyPublicCertificate), kLegacyPublicCertificate.size());
    }
    const auto bytes = settingsBytes();
    QSignalSpy changed(Settings::instance(), &Settings::settingsChanged);
    AdvancedPage page;
    auto *name = child<QLabel>(page, "advancedCertificateName");
    auto *notice = child<QLabel>(page, "advancedCertificateNamingNotice");
    auto *path = child<QLineEdit>(page, "advancedCertificatePath");
    QVERIFY(name && notice && path);
    QCOMPARE(name->text(), QStringLiteral("当前证书名称：Deskflow"));
    QCOMPARE(name->textFormat(), Qt::PlainText);
    QVERIFY(notice->text().contains(QString::fromUtf8(kAppName)));
    QVERIFY(notice->text().contains(QStringLiteral("已有 Deskflow 证书可继续使用")));
    QVERIFY(notice->text().contains(QStringLiteral("无需仅为名称重建")));
    QVERIFY(!page.hasPendingEdits());

    // Editing the output path never claims that the existing identity has changed.
    path->setText(certificatePath + QStringLiteral(".draft.pem"));
    page.setConnectionSecurity(true);
    QCOMPARE(name->text(), QStringLiteral("当前证书名称：Deskflow"));
    page.reload();
    QCOMPARE(name->text(), QStringLiteral("当前证书名称：Deskflow"));
    QCOMPARE(path->text(), certificatePath);
    QCOMPARE(changed.count(), 0);
    QCOMPARE(settingsBytes(), bytes);
    QFile certificate(certificatePath);
    QVERIFY(certificate.open(QIODevice::ReadOnly));
    QCOMPARE(certificate.readAll(), kLegacyPublicCertificate);
  }

  void reopeningLogViewerSharesTheOriginalBoundedDocumentWithoutApplying()
  {
    const auto bytes = settingsBytes();
    QTextDocument document;
    document.setDocumentLayout(new QPlainTextDocumentLayout(&document));
    document.setMaximumBlockCount(4);
    document.setPlainText(QStringLiteral("one\ntwo\nthree\nfour\nfive"));
    QPointer<QTextDocument> alive(&document);
    {
      AdvancedPage page;
      page.setLogDocument(&document);
      auto *view = child<QPushButton>(page, "advancedViewLog");
      QVERIFY(view && view->isEnabled());
      QSignalSpy requested(&page, &AdvancedPage::applyRequested);
      QSignalSpy regenerated(&page, &AdvancedPage::regenerateCertificateRequested);
      QSignalSpy changed(Settings::instance(), &Settings::settingsChanged);
      for (int opening = 0; opening < 2; ++opening) {
        if (opening == 1) {
          QTextCursor cursor(&document);
          cursor.movePosition(QTextCursor::End);
          cursor.insertText(QStringLiteral("\nsix"));
        }
        bool visited = false;
        QTimer::singleShot(0, &page, [&] {
          auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
          if (!dialog) return;
          const auto closeDialog = qScopeGuard([dialog] { dialog->reject(); });
          auto *log = dialog->findChild<QPlainTextEdit *>(QStringLiteral("runtimeLog"));
          visited = true;
          QVERIFY(log);
          QCOMPARE(log->document(), &document);
          QVERIFY(log->isReadOnly());
          QCOMPARE(log->lineWrapMode(), QPlainTextEdit::NoWrap);
          QCOMPARE(log->horizontalScrollBar()->value(), 0);
          QCOMPARE(log->textCursor().position(), document.characterCount() - 1);
          QCOMPARE(document.maximumBlockCount(), 4);
          QVERIFY(!log->toPlainText().contains(QStringLiteral("one")));
        });
        view->click();
        QVERIFY(visited);
        QVERIFY(alive);
      }
      QCOMPARE(document.blockCount(), 4);
      QVERIFY(document.toPlainText().endsWith(QStringLiteral("six")));
      QVERIFY(!page.hasPendingEdits());
      QCOMPARE(requested.count(), 0);
      QCOMPARE(regenerated.count(), 0);
      QCOMPARE(changed.count(), 0);
    }
    QVERIFY(alive);
    QVERIFY(document.toPlainText().endsWith(QStringLiteral("six")));
    QCOMPARE(settingsBytes(), bytes);
  }

private:
  template<class T> static T *child(AdvancedPage &page, const char *name)
  {
    return page.findChild<T *>(QString::fromLatin1(name));
  }

  static QByteArray settingsBytes()
  {
    QFile file(Settings::settingsFile());
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
  }

  QTemporaryDir m_temp;
  int m_profile = 0;
};

QTEST_MAIN(AdvancedPageTests)
#include "AdvancedPageTests.moc"
