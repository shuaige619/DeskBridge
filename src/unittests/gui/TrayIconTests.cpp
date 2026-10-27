/*
 * SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "gui/StyleUtils.h"

#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QtTest>

using namespace deskflow::gui;

class TrayIconTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase()
  {
    QVERIFY(m_files.isValid());
    m_originalTheme = QIcon::themeName();
    m_originalFallbackTheme = QIcon::fallbackThemeName();
    m_originalPaths = QIcon::themeSearchPaths();
    m_originalFallbackPaths = QIcon::fallbackSearchPaths();
    QVERIFY(!appIcon().isNull());
  }

  void cleanupTestCase()
  {
    QIcon::setThemeSearchPaths(m_originalPaths);
    QIcon::setFallbackSearchPaths(m_originalFallbackPaths);
    QIcon::setFallbackThemeName(m_originalFallbackTheme);
    QIcon::setThemeName(m_originalTheme);
  }

  void symbolicUsesIndependentThemeNameAndInstalledArtwork()
  {
    QVERIFY(createTheme(true));
    const auto icon = linuxPersonalTrayIcon(true);
    QCOMPARE(icon.name(), QStringLiteral("%1-symbolic").arg(kRevFqdnName));
    QVERIFY(icon.isMask());
    const auto actual = rendered(icon);
    QVERIFY(!actual.isNull());
    QCOMPARE(actual, rendered(QIcon(QStringLiteral(":/icons/deskbridge-symbolic.svg"))));

    int ink = 0;
    for (int y = 0; y < actual.height(); ++y) {
      for (int x = 0; x < actual.width(); ++x) {
        if (qAlpha(actual.pixel(x, y)) > 0)
          ++ink;
      }
    }
    QVERIFY(ink > 0);
    QVERIFY(ink < actual.width() * actual.height() / 2);
    QCOMPARE(qAlpha(actual.pixel(0, 0)), 0);
    QCOMPARE(qAlpha(actual.pixel(actual.width() - 1, actual.height() - 1)), 0);
  }

  void missingSymbolicIgnoresUpstreamThemeAndKeepsColorFallback()
  {
    // The host supplies the old Deskflow name, but no DeskBridge icon.
    QVERIFY(createTheme(false));
    QVERIFY(QIcon::hasThemeIcon(QStringLiteral("org.deskflow.deskflow-symbolic")));
    const auto icon = linuxPersonalTrayIcon(true);
    QVERIFY(icon.name().isEmpty());
    QVERIFY(!icon.isMask());
    QCOMPARE(rendered(icon), rendered(appIcon()));
    QVERIFY(rendered(icon) != rendered(QIcon::fromTheme(QStringLiteral("org.deskflow.deskflow-symbolic"))));
  }

  void nonSymbolicAlwaysUsesColorApplicationIcon()
  {
    QVERIFY(createTheme(true));
    const auto icon = linuxPersonalTrayIcon(false);
    QVERIFY(icon.name().isEmpty());
    QVERIFY(!icon.isMask());
    QCOMPARE(rendered(icon), rendered(appIcon()));
  }

  void changedThemeResolvesNewSymbolicWithoutMaskingPreviousFallback()
  {
    QVERIFY(createTheme(false));
    const auto fallback = linuxPersonalTrayIcon(true);
    QVERIFY(!fallback.isMask());
    QVERIFY(createTheme(true));
    const auto symbolic = linuxPersonalTrayIcon(true);
    QVERIFY(symbolic.isMask());
    QCOMPARE(symbolic.name(), QStringLiteral("%1-symbolic").arg(kRevFqdnName));
    QVERIFY(!fallback.isMask());
    QCOMPARE(rendered(fallback), rendered(appIcon()));
  }

private:
  static QImage rendered(const QIcon &icon)
  {
    return icon.pixmap(QSize(64, 64)).toImage().convertToFormat(QImage::Format_ARGB32);
  }

  bool createTheme(bool withPersonalSymbolic)
  {
    const auto name = QStringLiteral("deskbridge-icon-test-%1").arg(++m_themeNumber);
    const auto directory = m_files.filePath(name);
    const auto apps = directory + QStringLiteral("/64x64/apps/");
    if (!QDir().mkpath(apps))
      return false;
    QFile index(directory + QStringLiteral("/index.theme"));
    if (!index.open(QIODevice::WriteOnly))
      return false;
    index.write("[Icon Theme]\nName=DeskBridge test\nDirectories=64x64/apps\n\n"
                "[64x64/apps]\nSize=64\nType=Fixed\nContext=Applications\n");
    index.close();
    QImage upstream(64, 64, QImage::Format_ARGB32);
    upstream.fill(Qt::red);
    if (!upstream.save(apps + QStringLiteral("org.deskflow.deskflow-symbolic.png")))
      return false;
    if (withPersonalSymbolic &&
        !QFile::copy(QStringLiteral(":/icons/deskbridge-symbolic.svg"),
                     apps + QStringLiteral("%1-symbolic.svg").arg(kRevFqdnName)))
      return false;
    QIcon::setThemeSearchPaths({m_files.path()});
    QIcon::setFallbackSearchPaths({});
    QIcon::setFallbackThemeName(QString());
    QIcon::setThemeName(name);
    return true;
  }

  QTemporaryDir m_files;
  QString m_originalTheme;
  QString m_originalFallbackTheme;
  QStringList m_originalPaths;
  QStringList m_originalFallbackPaths;
  int m_themeNumber = 0;
};

QTEST_MAIN(TrayIconTests)
#include "TrayIconTests.moc"
