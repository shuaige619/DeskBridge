/*
 * SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "gui/personal/ChordRecorder.h"

#include <QApplication>
#include <QKeyEvent>
#include <QSignalSpy>
#include <QTest>
#include <QVBoxLayout>

#include <utility>

using deskflow::gui::personal::ChordRecorder;

namespace {
Qt::KeyboardModifier physicalControl()
{
#ifdef Q_OS_MACOS
  if (!QCoreApplication::testAttribute(Qt::AA_MacDontSwapCtrlAndMeta))
    return Qt::MetaModifier;
#endif
  return Qt::ControlModifier;
}

Qt::KeyboardModifier physicalCommand()
{
#ifdef Q_OS_MACOS
  if (!QCoreApplication::testAttribute(Qt::AA_MacDontSwapCtrlAndMeta))
    return Qt::ControlModifier;
#endif
  return Qt::MetaModifier;
}

void deliverKey(ChordRecorder &recorder, int key, Qt::KeyboardModifiers modifiers)
{
  QKeyEvent event(QEvent::KeyPress, key, modifiers);
  QApplication::sendEvent(&recorder, &event);
}
} // namespace

class ChordRecorderTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase()
  {
    if (QGuiApplication::platformName() != QStringLiteral("offscreen") &&
        QGuiApplication::platformName() != QStringLiteral("minimal"))
      QSKIP("Recorder tests require QT_QPA_PLATFORM=offscreen or minimal; no real windows or system key events.");
  }

  void physicalModifierSemantics()
  {
    QWidget host;
    QVBoxLayout layout(&host);
    ChordRecorder recorder;
    layout.addWidget(&recorder);
    host.show();
    host.activateWindow();
    recorder.setFocus();
    QApplication::processEvents();
    QVERIFY(recorder.hasFocus());
    recorder.click();
    deliverKey(recorder, Qt::Key_C, physicalControl());
    QCOMPARE(recorder.chord().key, KeyID('c'));
    QCOMPARE(recorder.chord().modifiers, KeyModifierControl);

    recorder.setFocus();
    QApplication::processEvents();
    recorder.click();
    deliverKey(recorder, Qt::Key_V, physicalCommand());
    QCOMPARE(recorder.chord().key, KeyID('v'));
    QCOMPARE(recorder.chord().modifiers, KeyModifierSuper);
  }

  void shortcutOverrideAndEscape()
  {
    QWidget host;
    QVBoxLayout layout(&host);
    ChordRecorder recorder;
    recorder.setChord({'c', KeyModifierControl});
    layout.addWidget(&recorder);
    host.show();
    host.activateWindow();
    recorder.setFocus();
    QApplication::processEvents();
    recorder.click();
    QSignalSpy changed(&recorder, &ChordRecorder::chordChanged);
    QKeyEvent shortcut(QEvent::ShortcutOverride, Qt::Key_Q, physicalCommand());
    shortcut.setAccepted(false);
    QApplication::sendEvent(&recorder, &shortcut);
    QVERIFY(shortcut.isAccepted());
    deliverKey(recorder, Qt::Key_Shift, Qt::ShiftModifier);
    deliverKey(recorder, Qt::Key_Escape, Qt::NoModifier);
    QCOMPARE(recorder.chord().key, KeyID('c'));
    QCOMPARE(recorder.chord().modifiers, KeyModifierControl);
    QCOMPARE(changed.count(), 0);
  }

  void navigationAndFunctionKeys()
  {
    QWidget host;
    QVBoxLayout layout(&host);
    ChordRecorder recorder;
    layout.addWidget(&recorder);
    host.show();
    host.activateWindow();
    for (const auto [qtKey, key] : {std::pair{Qt::Key_Home, kKeyHome}, std::pair{Qt::Key_F12, kKeyF12},
                                    std::pair{Qt::Key_Left, kKeyLeft}, std::pair{Qt::Key_Tab, kKeyTab}}) {
      recorder.setFocus();
      QApplication::processEvents();
      recorder.click();
      deliverKey(recorder, qtKey, physicalControl() | Qt::ShiftModifier);
      QCOMPARE(recorder.chord().key, key);
      QCOMPARE(recorder.chord().modifiers, KeyModifierControl | KeyModifierShift);
    }
  }

  void focusNavigationDoesNotRecord()
  {
    QWidget host;
    QVBoxLayout layout(&host);
    ChordRecorder recorder;
    recorder.setChord({'c', KeyModifierControl});
    QPushButton next(QStringLiteral("Next"));
    layout.addWidget(&recorder);
    layout.addWidget(&next);
    host.show();
    host.activateWindow();
    recorder.setFocus();
    QApplication::processEvents();
    QVERIFY(recorder.hasFocus());
    QSignalSpy changed(&recorder, &ChordRecorder::chordChanged);

    deliverKey(recorder, Qt::Key_Tab, Qt::NoModifier);

    QVERIFY(!recorder.hasFocus());
    QCOMPARE(recorder.chord().key, KeyID('c'));
    QCOMPARE(recorder.chord().modifiers, KeyModifierControl);
    QCOMPARE(changed.count(), 0);

    recorder.setFocus();
    QApplication::processEvents();
    deliverKey(recorder, Qt::Key_Space, Qt::NoModifier);
    deliverKey(recorder, Qt::Key_X, physicalControl());
    QCOMPARE(recorder.chord().key, KeyID('x'));
    QCOMPARE(recorder.chord().modifiers, KeyModifierControl);
    QCOMPARE(changed.count(), 1);
  }
};

QTEST_MAIN(ChordRecorderTests)
#include "ChordRecorderTests.moc"
