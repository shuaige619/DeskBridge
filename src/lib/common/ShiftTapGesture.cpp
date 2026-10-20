/*
 * SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "ShiftTapGesture.h"

namespace deskflow::shortcuts {
namespace {
bool shift(KeyID key)
{
  return key == kKeyShift_L || key == kKeyShift_R;
}

bool isolatedModifiers(KeyModifierMask modifiers)
{
  constexpr auto allowed = KeyModifierShift | KeyModifierCapsLock | KeyModifierNumLock | KeyModifierScrollLock;
  return !(modifiers & ~allowed);
}
} // namespace

void ShiftTapGesture::setEnabled(bool enabled)
{
  if (m_enabled != enabled)
    cancel();
  m_enabled = enabled;
}

void ShiftTapGesture::keyDown(KeyID key, KeyModifierMask modifiers, KeyButton button, Timestamp now)
{
  const bool isolated = m_keys.empty() && m_mouseButtons.empty() && !m_candidate;
  m_candidate.reset();
  if (m_enabled && isolated && shift(key) && button != 0 && isolatedModifiers(modifiers))
    m_candidate = Candidate{key, button, now};
  m_keys[button] = key;
}

bool ShiftTapGesture::keyUp(KeyID key, KeyModifierMask modifiers, KeyButton button, Timestamp now)
{
  const bool trigger = m_enabled && m_candidate && m_candidate->button == button && m_candidate->key == key &&
      m_keys.size() == 1 && m_mouseButtons.empty() && isolatedModifiers(modifiers) &&
      now >= m_candidate->started &&
      static_cast<std::uint64_t>(now) - static_cast<std::uint64_t>(m_candidate->started) <= std::uint64_t(maximumTapMs);
  m_candidate.reset();
  m_keys.erase(button);
  return trigger;
}

void ShiftTapGesture::keyRepeat(KeyID key, KeyButton button)
{
  m_candidate.reset();
  m_keys[button] = key;
}

void ShiftTapGesture::mouseButton(ButtonID button, bool down)
{
  m_candidate.reset();
  if (down)
    m_mouseButtons.insert(button);
  else
    m_mouseButtons.erase(button);
}

void ShiftTapGesture::scroll()
{
  m_candidate.reset();
}

void ShiftTapGesture::cancel()
{
  m_candidate.reset();
}

void ShiftTapGesture::reset()
{
  m_candidate.reset();
  m_keys.clear();
  m_mouseButtons.clear();
}

} // namespace deskflow::shortcuts
