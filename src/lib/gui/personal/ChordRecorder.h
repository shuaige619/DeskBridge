/*
 * SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "common/ShortcutRules.h"

#include <QPushButton>

namespace deskflow::gui::personal {

class ChordRecorder : public QPushButton
{
  Q_OBJECT
public:
  explicit ChordRecorder(QWidget *parent = nullptr);
  shortcuts::Chord chord() const;
  void setChord(const shortcuts::Chord &chord);
  void setDisplayOperatingSystem(shortcuts::DeviceOS os);

Q_SIGNALS:
  void chordChanged();

protected:
  bool event(QEvent *event) override;
  void focusOutEvent(QFocusEvent *event) override;

private:
  void startRecording();
  void stopRecording(bool restore);
  void updateText();
  shortcuts::Chord m_chord;
  shortcuts::Chord m_beforeRecording;
  bool m_recording = false;
  shortcuts::DeviceOS m_displayOS = shortcuts::localOS();
};

} // namespace deskflow::gui::personal
