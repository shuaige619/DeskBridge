/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "common/ShortcutMapper.h"

#include <algorithm>
#include <utility>

namespace deskflow::shortcuts {
namespace {
constexpr auto kLocks = KeyModifierCapsLock | KeyModifierNumLock | KeyModifierScrollLock;

bool matches(const ShortcutMapper::Chord &a, const ShortcutMapper::Chord &b)
{
  return deskflow::shortcuts::normalizedKey(a.key) == deskflow::shortcuts::normalizedKey(b.key) &&
         (a.modifiers & ~kLocks) == b.modifiers;
}

ShortcutMapper::Chord outputChord(const ShortcutMapper::Chord &output, KeyModifierMask locks)
{
  auto result = output;
  result.key = deskflow::shortcuts::normalizedKey(result.key);
  // Protocol KeyIDs describe characters, whereas the recorder stores letter
  // keys case-insensitively. KeyMap needs the shifted character to select a
  // Shift+letter command; navigation/function keys are already unambiguous.
  if ((result.modifiers & KeyModifierShift) && result.key >= 'a' && result.key <= 'z')
    result.key -= 'a' - 'A';
  result.modifiers |= locks & kLocks;
  return result;
}

bool isModifierOrStateKey(KeyID key)
{
  return key == kKeyNone || (key >= kKeyShift_L && key <= kKeyHyper_R) || key == kKeyAltGr ||
         key == kKeyNumLock || key == kKeyScrollLock || key == kKeySetModifiers || key == kKeyClearModifiers ||
         key == kKeyNextGroup || key == kKeyPrevGroup || key == kKeyCompose;
}
} // namespace

void ShortcutMapper::configure(const Configuration &config, DeviceOS detected, DeviceOS target)
{
  m_config = config;
  m_modifiers.reset();
  if (!deskflow::shortcuts::validate(config).isEmpty()) {
    m_config.enabled = false;
    m_config.rules.clear();
  } else if (config.fullShortcuts) {
    m_modifiers = shortcutModifierMapping(config, detected, target);
  }
}

void ShortcutMapper::configure(bool enabled, std::vector<std::string> apps)
{
  deskflow::shortcuts::Configuration config;
  config.enabled = enabled;
  for (const auto &app : apps)
    config.apps.push_back(QString::fromStdString(app));
  config.rules = deskflow::shortcuts::defaultRules(config.apps);
  configure(config);
}

bool ShortcutMapper::isCandidate(const Chord &input) const
{
  if (!m_config.enabled || deskflow::shortcuts::isReservedInput(input))
    return false;
  return isFullShortcut(input) || hasScopedRule(input, false);
}

bool ShortcutMapper::hasScopedRule(const Chord &input, bool includeDisabled) const
{
  return std::any_of(m_config.rules.begin(), m_config.rules.end(), [this, &input, includeDisabled](const auto &rule) {
    return (rule.enabled || includeDisabled) && matches(input, rule.input) &&
           std::any_of(rule.apps.begin(), rule.apps.end(), [this](const auto &app) { return m_config.apps.contains(app); });
  });
}

bool ShortcutMapper::isFullShortcut(const Chord &input) const
{
  // Adapt shortcut chords, not held modifier keys or character-composition
  // state. Preserve an explicit source+target chord rather than collapsing it.
  constexpr auto composition = KeyModifierAltGr | KeyModifierMeta | KeyModifierLevel5Lock;
  return m_config.enabled && m_modifiers && (m_config.allApplications || !m_config.apps.isEmpty()) &&
         !isModifierOrStateKey(input.key) &&
         !deskflow::shortcuts::isReservedInput(input) && (input.modifiers & m_modifiers->source) &&
         !(input.modifiers & (m_modifiers->target | composition));
}

bool ShortcutMapper::requiresApplication(const Chord &input) const
{
  if (!isCandidate(input))
    return false;
  // A global fallback needs no foreground-process lookup unless a scoped
  // exact rule (including a disabled exception) could take precedence.
  return hasScopedRule(input, isFullShortcut(input)) || !m_config.allApplications;
}

ShortcutMapper::Chord ShortcutMapper::mapKeyDown(const Chord &input, KeyButton button, const QString &app)
{
  if (button == 0)
    return input;
  if (isKeyDown(button))
    return mapKeyRepeat(input, button);
  std::optional<Chord> output;
  const bool scopedApp = !app.isEmpty() && m_config.apps.contains(app);
  const bool enabled = m_config.enabled && !deskflow::shortcuts::isReservedInput(input);
  if (enabled && scopedApp) {
    for (const auto &rule : m_config.rules) {
      if (rule.enabled && rule.apps.contains(app) && matches(input, rule.input)) {
        output = outputChord(rule.output, input.modifiers);
        break;
      }
    }
  }
  const bool unknownScopedRule = app.isEmpty() && hasScopedRule(input, true);
  if (enabled && !output && !unknownScopedRule && isFullShortcut(input) && (m_config.allApplications || scopedApp)) {
    const bool disabledException = scopedApp && std::any_of(
        m_config.rules.begin(), m_config.rules.end(), [&input, &app](const auto &rule) {
          return !rule.enabled && rule.apps.contains(app) && matches(input, rule.input);
        });
    if (!disabledException)
      output = Chord{input.key, (input.modifiers & ~m_modifiers->source) | m_modifiers->target};
  }
  m_pressed.emplace(button, output);
  return output.value_or(input);
}

ShortcutMapper::Chord ShortcutMapper::mapKeyRepeat(const Chord &input, KeyButton button) const
{
  const auto entry = m_pressed.find(button);
  // A mapped press keeps its output identity/modifiers until release. Native
  // (unmapped) keys keep the server's current repeat ID and modifier semantics.
  return entry != m_pressed.end() ? entry->second.value_or(input) : input;
}

KeyModifierMask
ShortcutMapper::keyDown(KeyID id, KeyModifierMask mask, KeyButton button, const std::string &app)
{
  return mapKeyDown({id, mask}, button, QString::fromStdString(app)).modifiers;
}

KeyModifierMask ShortcutMapper::keyRepeat(KeyID id, KeyModifierMask mask, KeyButton button) const
{
  return mapKeyRepeat({id, mask}, button).modifiers;
}

void ShortcutMapper::keyUp(KeyButton button)
{
  m_pressed.erase(button);
}

void ShortcutMapper::reset()
{
  m_pressed.clear();
}

bool ShortcutMapper::isKeyDown(KeyButton button) const
{
  return m_pressed.contains(button);
}

bool ShortcutMapper::isClipboardShortcut(KeyID id, KeyModifierMask mask)
{
  // Caps/Num/Scroll Lock are state, not extra shortcut modifiers. Preserve them.
  constexpr auto lockMask = KeyModifierCapsLock | KeyModifierNumLock | KeyModifierScrollLock;
  return (id == 'c' || id == 'C' || id == 'v' || id == 'V') && (mask & ~lockMask) == KeyModifierControl;
}

} // namespace deskflow::shortcuts
