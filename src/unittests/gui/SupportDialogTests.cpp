/* SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception */
#include "common/Settings.h"
#include "common/VersionInfo.h"
#include "gui/dialogs/AboutDialog.h"
#include "gui/dialogs/FingerprintDialog.h"
#include "gui/dialogs/HelpDialog.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QSysInfo>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QtTest>

class SupportUrlRecorder : public QObject
{
  Q_OBJECT
public:
  QList<QUrl> opened;
public Q_SLOTS:
  void record(const QUrl &url)
  {
    opened.append(url);
  }
};

class SupportDialogTests : public QObject
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
    Settings::setSettingsFile(m_files.filePath("support.conf"));
    Settings::setStateFile(m_files.filePath("support.state"));
    QDesktopServices::setUrlHandler("https", &m_urls, "record");
    QDesktopServices::setUrlHandler("http", &m_urls, "record");
  }

  void cleanupTestCase()
  {
    QDesktopServices::unsetUrlHandler("https");
    QDesktopServices::unsetUrlHandler("http");
  }

  void init()
  {
    m_urls.opened.clear();
  }

  void trustRequiresAnExplicitDecision_data()
  {
    QTest::addColumn<int>("mode");
    QTest::newRow("receiving-source") << int(FingerprintDialogMode::Client);
    QTest::newRow("source-receiving-client") << int(FingerprintDialogMode::Server);
  }

  void trustRequiresAnExplicitDecision()
  {
    QFETCH(int, mode);
    const auto dialogMode = static_cast<FingerprintDialogMode>(mode);
    FingerprintDialog enterDialog(nullptr, fingerprint('a'), dialogMode, fingerprint('b'));
    auto *buttons = enterDialog.findChild<QDialogButtonBox *>();
    QVERIFY(buttons);
    QVERIFY(buttons->button(QDialogButtonBox::Yes)->isEnabled());
    QVERIFY(buttons->button(QDialogButtonBox::Yes)->property("primaryButton").toBool());
    QVERIFY(!buttons->button(QDialogButtonBox::Yes)->isDefault());
    QVERIFY(buttons->button(QDialogButtonBox::No)->isDefault());
    QSignalSpy enterFinished(&enterDialog, &QDialog::finished);
    enterDialog.show();
    QApplication::processEvents();
    QTest::keyClick(&enterDialog, Qt::Key_Return);
    QCOMPARE(enterFinished.count(), 1);
    QCOMPARE(enterDialog.result(), int(QDialog::Rejected));

    FingerprintDialog trustDialog(nullptr, fingerprint('a'), dialogMode, fingerprint('b'));
    QSignalSpy trustFinished(&trustDialog, &QDialog::finished);
    trustDialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Yes)->click();
    QCOMPARE(trustFinished.count(), 1);
    QCOMPARE(trustDialog.result(), int(QDialog::Accepted));

    FingerprintDialog cancelDialog(nullptr, fingerprint('a'), dialogMode, fingerprint('b'));
    QSignalSpy cancelFinished(&cancelDialog, &QDialog::finished);
    cancelDialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::No)->click();
    QCOMPARE(cancelFinished.count(), 1);
    QCOMPARE(cancelDialog.result(), int(QDialog::Rejected));
  }

  void invalidFingerprintsCannotBeTrusted()
  {
    FingerprintDialog remoteInvalid(nullptr, fingerprint('a'), FingerprintDialogMode::Client, {});
    QVERIFY(!remoteInvalid.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Yes)->isEnabled());
    FingerprintDialog localInvalid(nullptr, {}, FingerprintDialogMode::Server, fingerprint('b'));
    QVERIFY(!localInvalid.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Yes)->isEnabled());
    auto unsupported = fingerprint('b');
    unsupported.type = QCryptographicHash::Sha1;
    unsupported.data = QByteArray(20, 'b');
    FingerprintDialog unsupportedHash(nullptr, fingerprint('a'), FingerprintDialogMode::Client, unsupported);
    QVERIFY(!unsupportedHash.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Yes)->isEnabled());
  }

  void copiedFingerprintContainsTheCompleteSha256()
  {
    const auto local = fingerprint('a');
    FingerprintDialog dialog(nullptr, local);
    QVERIFY(dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->property("primaryButton").toBool());
    QApplication::clipboard()->setText(QStringLiteral("sentinel"));
    auto *copy = buttonWithText(dialog, QStringLiteral("复制指纹"));
    QVERIFY(copy);
    copy->click();
    QCOMPARE(QApplication::clipboard()->text(), QString::fromLatin1(local.data.toHex(':').toUpper()));
  }

  void diagnosticsUseTheActualBuildAndPreserveLicenses()
  {
    AboutDialog dialog;
    auto *done = buttonWithText(dialog, QStringLiteral("完成"));
    QVERIFY(done);
    QVERIFY(done->property("primaryButton").toBool());
    auto *copy = buttonWithText(dialog, QStringLiteral("复制诊断信息"));
    QVERIFY(copy);
    copy->click();
    const auto copied = QApplication::clipboard()->text();
    QVERIFY(copied.contains(QString::fromUtf8(kVersion)));
    QVERIFY(copied.contains(QString::fromUtf8(kVersionGitSha)));
    QVERIFY(copied.contains(QString::fromUtf8(qVersion())));
    QVERIFY(copied.contains(QSysInfo::prettyProductName()));
    QVERIFY(copied.contains(QStringLiteral("桌桥")));
    auto *tabs = dialog.findChild<QTabWidget *>();
    QVERIFY(tabs);
    QCOMPARE(tabs->count(), 3);
    auto *license = qobject_cast<QTextBrowser *>(tabs->widget(2));
    QVERIFY(license);
    QVERIFY(license->toPlainText().contains(QStringLiteral("GNU GENERAL PUBLIC LICENSE")));
    QVERIFY(license->toPlainText().contains(QStringLiteral("LICENSE EXCEPTION FOR OPENSSL")));
    auto *updates = buttonWithText(dialog, QStringLiteral("查看更新…"));
    QVERIFY(updates);
    updates->click();
    QCOMPARE(m_urls.opened, QList<QUrl>{QUrl(QStringLiteral("https://github.com/shuaige619/DeskBridge/releases"))});
  }

  void helpReplacesLegacySourceAndNavigatesLocally()
  {
    HelpDialog dialog(nullptr, QUrl(QStringLiteral("https://deskflow.org/wiki")));
    auto *done = buttonWithText(dialog, QStringLiteral("完成"));
    QVERIFY(done);
    QVERIFY(done->property("primaryButton").toBool());
    QCOMPARE(m_urls.opened.size(), 0);
    auto *topics = dialog.findChild<QListWidget *>();
    auto *browser = dialog.findChild<QTextBrowser *>();
    QVERIFY(topics);
    QVERIFY(browser);
    QCOMPARE(topics->count(), 6);
    QVERIFY(browser->toPlainText().contains(QStringLiteral("连接你的电脑")));
    QVERIFY(
        QMetaObject::invokeMethod(
            browser, "anchorClicked", Qt::DirectConnection, Q_ARG(QUrl, QUrl(QStringLiteral("deskbridge-help:/1")))
        )
    );
    QCOMPARE(topics->currentRow(), 1);
    QVERIFY(browser->toPlainText().contains(QStringLiteral("导出服务端配置")));
    topics->setCurrentRow(2);
    QVERIFY(browser->toPlainText().contains(QStringLiteral("接收端")));
    auto *back = buttonWithText(dialog, QStringLiteral("返回"));
    QVERIFY(back);
    QVERIFY(back->isEnabled());
    back->click();
    QCOMPARE(topics->currentRow(), 1);
    auto *home = buttonWithText(dialog, QStringLiteral("开始使用"));
    QVERIFY(home);
    home->click();
    QCOMPARE(topics->currentRow(), 0);
    QVERIFY(!home->isEnabled());
    QCOMPARE(m_urls.opened.size(), 0);
    QVERIFY(
        QMetaObject::invokeMethod(
            browser, "anchorClicked", Qt::DirectConnection,
            Q_ARG(QUrl, QUrl(QStringLiteral("https://deskflow.org/wiki")))
        )
    );
    QCOMPARE(m_urls.opened.size(), 0);
  }

  void captureRedesignedSupportViews()
  {
    const auto directory = qEnvironmentVariable("DESKBRIDGE_UI_CAPTURE_DIR");
    if (directory.isEmpty())
      return;
    QVERIFY(QDir().mkpath(directory));
    const auto capture = [&directory](QDialog &dialog, const QString &name) {
      dialog.show();
      QApplication::processEvents();
      const bool saved = dialog.grab().save(directory + "/" + name + ".png");
      dialog.hide();
      return saved;
    };
    AboutDialog about;
    QVERIFY(capture(about, "about"));
    auto *tabs = about.findChild<QTabWidget *>();
    tabs->setCurrentIndex(1);
    QVERIFY(capture(about, "about-credits"));
    tabs->setCurrentIndex(2);
    QVERIFY(capture(about, "about-license"));
    HelpDialog help;
    QVERIFY(capture(help, "help-connect"));
    auto *topics = help.findChild<QListWidget *>();
    for (int i = 1; i < topics->count(); ++i) {
      topics->setCurrentRow(i);
      QVERIFY(capture(help, QStringLiteral("help-%1").arg(i)));
    }
    FingerprintDialog local(nullptr, fingerprint('a'));
    QVERIFY(capture(local, "certificate-local"));
    FingerprintDialog peer(nullptr, fingerprint('a'), FingerprintDialogMode::Server, fingerprint('b'));
    QVERIFY(capture(peer, "certificate-peer"));
    peer.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Help)->click();
    QVERIFY(capture(peer, "certificate-peer-art"));
  }

private:
  static Fingerprint fingerprint(char value)
  {
    Fingerprint result;
    result.type = QCryptographicHash::Sha256;
    result.data = QByteArray(32, value);
    return result;
  }
  static QPushButton *buttonWithText(QWidget &widget, const QString &text)
  {
    for (auto *button : widget.findChildren<QPushButton *>())
      if (button->text() == text)
        return button;
    return nullptr;
  }
  QTemporaryDir m_files;
  SupportUrlRecorder m_urls;
};

QTEST_MAIN(SupportDialogTests)
#include "SupportDialogTests.moc"
