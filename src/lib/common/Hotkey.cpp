/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 - 2026 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2008 Volker Lanz <vl@fidra.de>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "Hotkey.h"

#include <QSettings>

QString Hotkey::text() const
{
  return m_keySequence.isMouseButton() ? kMousebutton.arg(m_keySequence.toString())
                                       : kKeystroke.arg(m_keySequence.toString());
}

Action &Hotkey::actionAt(int index)
{
  return m_actions[index];
}

void Hotkey::addAction(const Action &action)
{
  if (m_actions.contains(action))
    return;
  m_actions.append(action);
}

void Hotkey::removeActionAt(int index)
{
  if (index < 0 || index >= m_actions.size())
    return;
  m_actions.removeAt(index);
}

void Hotkey::loadSettings(QSettings &settings)
{
  m_keySequence.loadSettings(settings);

  m_actions.clear();
  int num = settings.beginReadArray(kSectionActions);
  for (int i = 0; i < num; i++) {
    settings.setArrayIndex(i);
    Action a;
    a.loadSettings(settings);
    m_actions.append(a);
  }

  settings.endArray();
}

void Hotkey::saveSettings(QSettings &settings) const
{
  m_keySequence.saveSettings(settings);

  settings.beginWriteArray(kSectionActions);
  for (int i = 0; i < m_actions.size(); i++) {
    settings.setArrayIndex(i);
    m_actions.at(i).saveSettings(settings);
  }
  settings.endArray();
}

bool Hotkey::operator==(const Hotkey &hk) const
{
  return m_keySequence == hk.keySequence() && m_actions == hk.actions();
}

QTextStream &operator<<(QTextStream &outStream, const Hotkey &hotkey)
{
  // Don't write a config if there is no actions
  if (hotkey.actions().size() == 0)
    return outStream;

  QStringList onPress;
  QStringList onRelease;
  for (const auto &action : hotkey.actions()) {
    const auto type = static_cast<Action::Type>(action.type());
    const bool paired = type == Action::Type::keystroke || type == Action::Type::mousebutton;
    // Old GUI preferences have no tap flag. Preserve their native lifecycle:
    // press holds the output and release lets go, including mouse drags.
    auto &commands = action.activeOnRelease() && (!paired || action.tapAtTrigger()) ? onRelease : onPress;
    if (paired && action.tapAtTrigger()) {
      // Native keystroke()/mousebutton() hold Down until the trigger is
      // released. The graphical editor promises a complete tap at its chosen
      // phase, so expand the pair explicitly without changing native syntax.
      Action down = action;
      Action up = action;
      const bool mouse = action.keySequence().isMouseButton();
      down.setType(static_cast<int>(mouse ? Action::Type::mouseDown : Action::Type::keyDown));
      up.setType(static_cast<int>(mouse ? Action::Type::mouseUp : Action::Type::keyUp));
      commands.append(down.text());
      commands.append(up.text());
    } else {
      commands.append(action.text());
    }
  }
  outStream << QStringLiteral("\t%1 = %2").arg(hotkey.text(), onPress.join(QStringLiteral(", ")));
  if (!onRelease.isEmpty())
    outStream << QStringLiteral("; ") << onRelease.join(QStringLiteral(", "));
  outStream << QLatin1Char('\n');
  return outStream;
}
