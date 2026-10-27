/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2008 Volker Lanz <vl@fidra.de>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "KeySequenceWidget.h"

#include <QApplication>
#include <QMouseEvent>
#include <QStyle>

KeySequenceWidget::KeySequenceWidget(QWidget *parent, const KeySequence &seq)
    : QPushButton(parent),
      m_KeySequence(seq),
      m_BackupSequence(seq)
{
  setFocusPolicy(Qt::StrongFocus);
  setAutoDefault(false);
  setDefault(false);
  setProperty("keycap", true);
  setMinimumHeight(48);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  setAccessibleName(tr("按键录入"));
  setToolTip(tr("点击后按下组合键或鼠标按钮；点击其他位置取消录入。"));
  updateOutput();
}

void KeySequenceWidget::setKeySequence(const KeySequence &seq)
{
  const bool wasRecording = status() == Recording;
  setStatus(Stopped);
  if (wasRecording) {
    qApp->removeEventFilter(this);
    releaseKeyboard();
    setDown(false);
  }
  m_KeySequence = seq;
  m_BackupSequence = seq;

  updateOutput();
  Q_EMIT keySequenceChanged();
}

void KeySequenceWidget::mousePressEvent(QMouseEvent *event)
{
  event->accept();

  if (status() == Stopped) {
    startRecording();
    return;
  }

  if (m_KeySequence.appendMouseButton(event->button()))
    stopRecording();

  updateOutput();
}

void KeySequenceWidget::startRecording()
{
  m_BackupSequence = m_KeySequence;
  m_KeySequence = KeySequence();
  setDown(true);
  setFocus(Qt::OtherFocusReason);
  grabKeyboard();
  setStatus(Recording);
  qApp->installEventFilter(this);
  updateOutput();
}

void KeySequenceWidget::stopRecording()
{
  if (status() != Recording)
    return;

  if (!keySequence().valid()) {
    m_KeySequence = backupSequence();
  } else {
    m_BackupSequence = m_KeySequence;
  }

  // Stop before releasing the keyboard or moving focus: either may generate a
  // FocusOut event, which must not recursively finish the same recording.
  setStatus(Stopped);
  qApp->removeEventFilter(this);
  setDown(false);
  releaseKeyboard();
  updateOutput();
  Q_EMIT keySequenceChanged();
}

bool KeySequenceWidget::event(QEvent *event)
{
  if (status() == Recording) {
    switch (event->type()) {
    case QEvent::KeyPress:
      keyPressEvent(static_cast<QKeyEvent *>(event));
      return true;

    case QEvent::KeyRelease:
    case QEvent::MouseButtonRelease:
    case QEvent::ShortcutOverride:
      event->accept();
      return true;

    case QEvent::FocusOut:
    case QEvent::Hide:
    case QEvent::WindowDeactivate:
      stopRecording();
      break;

    default:
      break;
    }
  }

  return QPushButton::event(event);
}

bool KeySequenceWidget::eventFilter(QObject *watched, QEvent *event)
{
  if (status() == Recording && event->type() == QEvent::MouseButtonPress) {
    if (const auto *widget = qobject_cast<QWidget *>(watched); widget && widget != this && !isAncestorOf(widget))
      stopRecording();
  }
  return QPushButton::eventFilter(watched, event);
}

void KeySequenceWidget::keyPressEvent(QKeyEvent *event)
{
  event->accept();

  if (status() == Stopped) {
    if (event->key() == Qt::Key_Space || event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
      startRecording();
    return;
  }

  if (m_KeySequence.appendKey(event->key(), event->modifiers()))
    stopRecording();

  updateOutput();
}

void KeySequenceWidget::updateOutput()
{
  QString sequence = m_KeySequence.toString();
  sequence.replace(QStringLiteral("Control"), QStringLiteral("Ctrl"));
  sequence.replace(QStringLiteral("+"), QStringLiteral(" + "));
  if (status() == Recording) {
    setText(sequence.isEmpty() ? tr("请按下组合键或鼠标按钮…") : sequence + QStringLiteral(" + …"));
    setAccessibleDescription(tr("正在录入，按下组合键或鼠标按钮以完成，点击其他位置取消。"));
  } else if (sequence.isEmpty()) {
    setText(tr("点击录入按键"));
    setAccessibleDescription(tr("尚未设置按键。可按空格键或回车键开始录入。"));
  } else {
    if (m_KeySequence.isMouseButton()) {
      const QStringList buttons = {tr("左键"), tr("右键"), tr("中键"), tr("侧键")};
      const QString raw = m_KeySequence.toString().section(QLatin1Char('+'), -1);
      bool ok = false;
      const int button = raw.toInt(&ok);
      if (ok && button >= 1 && button <= buttons.size())
        sequence.replace(sequence.size() - raw.size(), raw.size(), buttons.at(button - 1));
      setText(mousePrefix() + sequence + mousePostfix());
    } else {
      setText(keyPrefix() + sequence + keyPostfix());
    }
    setAccessibleDescription(tr("当前按键：%1。点击可重新录入。").arg(text()));
  }
  style()->unpolish(this);
  style()->polish(this);
  update();
}
