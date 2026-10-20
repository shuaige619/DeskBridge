/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "common/ShortcutRules.h"

#include <map>
#include <string>
#include <vector>

namespace deskflow::shortcuts {

//! App-specific remote shortcuts, before platform key synthesis.
class ShortcutMapper
{
public:
  using Chord = deskflow::shortcuts::Chord;
  void configure(const deskflow::shortcuts::Configuration &config,
                 DeviceOS detected = DeviceOS::Unknown, DeviceOS target = localOS());
  bool isCandidate(const Chord &input) const;
  bool requiresApplication(const Chord &input) const;
  Chord mapKeyDown(const Chord &input, KeyButton button, const QString &app);
  Chord mapKeyRepeat(const Chord &input, KeyButton button) const;

  // Configuration changes apply to the next press, not to keys already held.
  void configure(bool enabled, std::vector<std::string> apps);

  KeyModifierMask keyDown(KeyID id, KeyModifierMask mask, KeyButton button, const std::string &app);
  KeyModifierMask keyRepeat(KeyID id, KeyModifierMask mask, KeyButton button) const;
  void keyUp(KeyButton button);
  void reset();

  bool isKeyDown(KeyButton button) const;
  static bool isClipboardShortcut(KeyID id, KeyModifierMask mask);

private:
  bool isFullShortcut(const Chord &input) const;
  bool hasScopedRule(const Chord &input, bool includeDisabled) const;
  deskflow::shortcuts::Configuration m_config;
  std::optional<ModifierMapping> m_modifiers;
  // Keep negative decisions too: moving focus must not change a held key's rule.
  std::map<KeyButton, std::optional<Chord>> m_pressed;
};

} // namespace deskflow::shortcuts
