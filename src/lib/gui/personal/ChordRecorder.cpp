/*
 * SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "ChordRecorder.h"

#include <QCoreApplication>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QStyle>

namespace deskflow::gui::personal {
namespace {
KeyID keyID(int key)
{
  if (key >= Qt::Key_A && key <= Qt::Key_Z)
    return 'a' + key - Qt::Key_A;
  if (key >= Qt::Key_Space && key <= Qt::Key_AsciiTilde)
    return key;
  if (key >= Qt::Key_F1 && key <= Qt::Key_F24)
    return kKeyF1 + key - Qt::Key_F1;
  switch (key) {
  case Qt::Key_Backspace: return kKeyBackSpace;
  case Qt::Key_Tab: return kKeyTab;
  case Qt::Key_Backtab: return kKeyTab;
  case Qt::Key_Return:
  case Qt::Key_Enter: return kKeyReturn;
  case Qt::Key_Delete: return kKeyDelete;
  case Qt::Key_Insert: return kKeyInsert;
  case Qt::Key_Home: return kKeyHome;
  case Qt::Key_End: return kKeyEnd;
  case Qt::Key_PageUp: return kKeyPageUp;
  case Qt::Key_PageDown: return kKeyPageDown;
  case Qt::Key_Left: return kKeyLeft;
  case Qt::Key_Right: return kKeyRight;
  case Qt::Key_Up: return kKeyUp;
  case Qt::Key_Down: return kKeyDown;
  default: return kKeyNone;
  }
}

KeyModifierMask modifierMask(Qt::KeyboardModifiers modifiers)
{
  auto control = Qt::ControlModifier;
  auto command = Qt::MetaModifier;
#ifdef Q_OS_MACOS
  // Qt's default Mac convention names Command "Control" and physical Control "Meta".
  if (!QCoreApplication::testAttribute(Qt::AA_MacDontSwapCtrlAndMeta)) {
    control = Qt::MetaModifier;
    command = Qt::ControlModifier;
  }
#endif
  KeyModifierMask result = 0;
  if (modifiers.testFlag(control)) result |= KeyModifierControl;
  if (modifiers.testFlag(command)) result |= KeyModifierSuper;
  if (modifiers.testFlag(Qt::ShiftModifier)) result |= KeyModifierShift;
  if (modifiers.testFlag(Qt::AltModifier)) result |= KeyModifierAlt;
  return result;
}
} // namespace

ChordRecorder::ChordRecorder(QWidget *parent) : QPushButton(parent)
{
  setFocusPolicy(Qt::StrongFocus);
  setAutoDefault(false);
  setMinimumHeight(44);
  setMinimumWidth(180);
  setProperty("keycap", true);
  setToolTip(tr("点按，或聚焦后按空格/Return 开始录入；Esc 取消。本框以外不会捕获按键。"));
  connect(this, &QPushButton::clicked, this, &ChordRecorder::startRecording);
  updateText();
}

shortcuts::Chord ChordRecorder::chord() const
{
  return m_chord;
}

void ChordRecorder::setChord(const shortcuts::Chord &chord)
{
  m_chord = chord;
  m_beforeRecording = chord;
  m_recording = false;
  updateText();
}

void ChordRecorder::startRecording()
{
  if (!isEnabled() || m_recording)
    return;
  m_beforeRecording = m_chord;
  m_recording = true;
  setFocus(Qt::MouseFocusReason);
  updateText();
}

void ChordRecorder::setDisplayOperatingSystem(shortcuts::DeviceOS os)
{
  m_displayOS = os;
  updateText();
}

void ChordRecorder::stopRecording(bool restore)
{
  if (restore)
    m_chord = m_beforeRecording;
  m_recording = false;
  updateText();
}

void ChordRecorder::updateText()
{
  setText(m_recording ? tr("按下组合键…") :
                       (m_chord.key == kKeyNone ? tr("点按录入快捷键") : shortcuts::chordText(m_chord, m_displayOS)));
  setAccessibleDescription(m_recording ? tr("正在录入，按组合键；Esc 取消。")
                                     : tr("当前快捷键：%1。点按，或聚焦后按空格/Return 开始录入。")
                                           .arg(m_chord.key == kKeyNone ? tr("尚未设置") : shortcuts::chordText(m_chord, m_displayOS)));
  setProperty("recording", m_recording);
  style()->unpolish(this);
  style()->polish(this);
}

bool ChordRecorder::event(QEvent *event)
{
  if (hasFocus() && !m_recording &&
      (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride)) {
    const auto *key = static_cast<QKeyEvent *>(event);
    if (key->modifiers() == Qt::NoModifier &&
        (key->key() == Qt::Key_Space || key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)) {
      event->accept();
      if (event->type() == QEvent::KeyPress)
        startRecording();
      return true;
    }
  }
  if (hasFocus() && m_recording) {
    if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyRelease) {
      event->accept();
      return true;
    }
    if (event->type() == QEvent::KeyPress) {
      auto *key = static_cast<QKeyEvent *>(event);
      key->accept();
      if (key->key() == Qt::Key_Escape) {
        stopRecording(true);
        clearFocus();
        return true;
      }
      if (key->isAutoRepeat())
        return true;
      const auto id = keyID(key->key());
      if (id != kKeyNone) {
        m_chord = {id, modifierMask(key->modifiers())};
        if (key->key() == Qt::Key_Backtab)
          m_chord.modifiers |= KeyModifierShift;
        stopRecording(false);
        clearFocus();
        Q_EMIT chordChanged();
      }
      return true;
    }
  }
  return QPushButton::event(event);
}

void ChordRecorder::focusOutEvent(QFocusEvent *event)
{
  if (m_recording)
    stopRecording(true);
  QPushButton::focusOutEvent(event);
}

} // namespace deskflow::gui::personal
