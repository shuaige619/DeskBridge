/*
 * SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "deskflow/KeyTypes.h"
#include "deskflow/MouseTypes.h"
#include <cstdint>
#include <map>
#include <optional>
#include <set>

namespace deskflow::shortcuts {

// Recognises an isolated remote Shift tap without consuming its native events.
// The caller supplies monotonic milliseconds; this class has no timer or I/O.
class ShiftTapGesture
{
public:
  using Timestamp = std::int64_t;
  static constexpr Timestamp maximumTapMs = 300;

  void setEnabled(bool enabled);
  void keyDown(KeyID key, KeyModifierMask modifiers, KeyButton button, Timestamp now);
  bool keyUp(KeyID key, KeyModifierMask modifiers, KeyButton button, Timestamp now);
  void keyRepeat(KeyID key, KeyButton button);
  void mouseButton(ButtonID button, bool down);
  void scroll();
  void cancel();
  void reset();

private:
  struct Candidate
  {
    KeyID key;
    KeyButton button;
    Timestamp started;
  };
  bool m_enabled = false;
  std::optional<Candidate> m_candidate;
  std::map<KeyButton, KeyID> m_keys;
  std::set<ButtonID> m_mouseButtons;
};

} // namespace deskflow::shortcuts
