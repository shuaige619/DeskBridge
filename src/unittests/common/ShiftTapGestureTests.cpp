/*
 * SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "common/ShiftTapGesture.h"

#include <QtTest>
#include <limits>

using deskflow::shortcuts::ShiftTapGesture;

class ShiftTapGestureTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void isolatedTap_data()
  {
    QTest::addColumn<KeyID>("key");
    QTest::addColumn<qint64>("duration");
    QTest::addColumn<bool>("trigger");
    QTest::newRow("left-immediate") << kKeyShift_L << qint64(0) << true;
    QTest::newRow("right-short") << kKeyShift_R << qint64(100) << true;
    QTest::newRow("inclusive-300") << kKeyShift_L << qint64(300) << true;
    QTest::newRow("long-301") << kKeyShift_R << qint64(301) << false;
    QTest::newRow("clock-regression") << kKeyShift_L << qint64(-1) << false;
  }

  void isolatedTap()
  {
    QFETCH(KeyID, key);
    QFETCH(qint64, duration);
    QFETCH(bool, trigger);
    ShiftTapGesture gesture;
    gesture.setEnabled(true);
    gesture.keyDown(key, KeyModifierShift, 10, 100);
    QCOMPARE(gesture.keyUp(key, 0, 10, 100 + duration), trigger);
    QVERIFY(!gesture.keyUp(key, 0, 10, 200));
  }

  void modifiersAndLocks_data()
  {
    QTest::addColumn<KeyModifierMask>("mask");
    QTest::addColumn<bool>("trigger");
    QTest::newRow("shift-only") << KeyModifierShift << true;
    QTest::newRow("prepress-mask") << KeyModifierMask(0) << true;
    QTest::newRow("caps") << (KeyModifierShift | KeyModifierCapsLock) << true;
    QTest::newRow("all-locks") << (KeyModifierShift | KeyModifierCapsLock | KeyModifierNumLock | KeyModifierScrollLock) << true;
    QTest::newRow("ctrl-shift") << (KeyModifierShift | KeyModifierControl) << false;
    QTest::newRow("alt-shift") << (KeyModifierShift | KeyModifierAlt) << false;
    QTest::newRow("command-shift") << (KeyModifierShift | KeyModifierSuper) << false;
    QTest::newRow("altgr") << (KeyModifierShift | KeyModifierAltGr) << false;
    QTest::newRow("unknown-modifier") << KeyModifierMask(0x8000) << false;
  }

  void modifiersAndLocks()
  {
    QFETCH(KeyModifierMask, mask);
    QFETCH(bool, trigger);
    ShiftTapGesture gesture;
    gesture.setEnabled(true);
    gesture.keyDown(kKeyShift_L, mask, 10, 0);
    QCOMPARE(gesture.keyUp(kKeyShift_L, mask, 10, 100), trigger);
  }

  void otherKeyOrRepeatCancels_data()
  {
    QTest::addColumn<QString>("event");
    for (const auto *name : {"letter-down", "unmatched-up", "same-shift-down", "repeat", "other-shift", "mouse-down", "mouse-up", "scroll"})
      QTest::newRow(name) << QString::fromLatin1(name);
  }

  void otherKeyOrRepeatCancels()
  {
    QFETCH(QString, event);
    ShiftTapGesture gesture;
    gesture.setEnabled(true);
    gesture.keyDown(kKeyShift_L, KeyModifierShift, 10, 0);
    if (event == "letter-down") gesture.keyDown('a', KeyModifierShift, 11, 20);
    else if (event == "unmatched-up") QVERIFY(!gesture.keyUp('a', 0, 11, 20));
    else if (event == "same-shift-down") gesture.keyDown(kKeyShift_L, KeyModifierShift, 10, 20);
    else if (event == "repeat") gesture.keyRepeat(kKeyShift_L, 10);
    else if (event == "other-shift") gesture.keyDown(kKeyShift_R, KeyModifierShift, 12, 20);
    else if (event == "mouse-down") gesture.mouseButton(1, true);
    else if (event == "mouse-up") gesture.mouseButton(1, false);
    else if (event == "scroll") gesture.scroll();
    QVERIFY(!gesture.keyUp(kKeyShift_L, 0, 10, 100));
    QVERIFY(!gesture.keyUp(kKeyShift_R, 0, 12, 110));
  }

  void existingHeldInputBlocksCandidate_data()
  {
    QTest::addColumn<QString>("input");
    QTest::newRow("space") << QStringLiteral("space");
    QTest::newRow("letter") << QStringLiteral("letter");
    QTest::newRow("mouse") << QStringLiteral("mouse");
    QTest::newRow("unknown-repeat") << QStringLiteral("repeat");
  }

  void existingHeldInputBlocksCandidate()
  {
    QFETCH(QString, input);
    ShiftTapGesture gesture;
    // Track held input even while disabled, then enable with it still held.
    if (input == "space") gesture.keyDown(' ', 0, 20, 0);
    else if (input == "letter") gesture.keyDown('a', 0, 20, 0);
    else if (input == "mouse") gesture.mouseButton(1, true);
    else gesture.keyRepeat('a', 20);
    gesture.setEnabled(true);
    gesture.cancel(); // Metadata/config changes must not discard held input.
    gesture.keyDown(kKeyShift_L, KeyModifierShift, 10, 20);
    QVERIFY(!gesture.keyUp(kKeyShift_L, 0, 10, 100));
    if (input == "mouse") gesture.mouseButton(1, false);
    else QVERIFY(!gesture.keyUp(input == "space" ? ' ' : 'a', 0, 20, 120));
    gesture.keyDown(kKeyShift_R, KeyModifierShift, 12, 140);
    QVERIFY(gesture.keyUp(kKeyShift_R, 0, 12, 180));
  }

  void resetAndConfigurationNeverCompleteAnOldTap()
  {
    ShiftTapGesture gesture;
    gesture.setEnabled(true);
    gesture.keyDown(kKeyShift_L, KeyModifierShift, 10, 0);
    gesture.reset();
    QVERIFY(!gesture.keyUp(kKeyShift_L, 0, 10, 100));
    gesture.keyDown(kKeyShift_L, KeyModifierShift, 10, 200);
    gesture.setEnabled(false);
    gesture.setEnabled(true);
    QVERIFY(!gesture.keyUp(kKeyShift_L, 0, 10, 250));
    gesture.keyDown(kKeyShift_L, KeyModifierShift, 10, 300);
    gesture.cancel();
    QVERIFY(!gesture.keyUp(kKeyShift_L, 0, 10, 350));
    gesture.keyDown(kKeyShift_L, KeyModifierShift, 10, 400);
    QVERIFY(gesture.keyUp(kKeyShift_L, 0, 10, 450));
  }

  void invalidAndMismatchedReleaseDoesNotTrigger()
  {
    ShiftTapGesture gesture;
    gesture.setEnabled(true);
    gesture.keyDown(kKeyShift_L, KeyModifierShift, 0, 0);
    QVERIFY(!gesture.keyUp(kKeyShift_L, 0, 0, 10));
    gesture.keyDown(kKeyShift_L, KeyModifierShift, 10, 20);
    QVERIFY(!gesture.keyUp(kKeyShift_R, 0, 10, 30));
    gesture.keyDown(kKeyShift_L, KeyModifierShift, 10, 40);
    QVERIFY(!gesture.keyUp(kKeyShift_L, 0, 11, 50));
    QVERIFY(!gesture.keyUp(kKeyShift_L, 0, 10, 60));
  }

  void extremeClockGapDoesNotOverflowIntoATap()
  {
    ShiftTapGesture gesture;
    gesture.setEnabled(true);
    gesture.keyDown(kKeyShift_L, KeyModifierShift, 10, std::numeric_limits<qint64>::min());
    QVERIFY(!gesture.keyUp(kKeyShift_L, 0, 10, std::numeric_limits<qint64>::max()));
  }
};

QTEST_GUILESS_MAIN(ShiftTapGestureTests)
#include "ShiftTapGestureTests.moc"
