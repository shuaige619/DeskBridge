/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "arch/Arch.h"
#include "base/Log.h"

#include <QObject>

class OSXShortcutMapperTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void exactShortcuts_data();
  void exactShortcuts();
  void repeatsKeepInitialAppAndSettings();
  void unconvertedPressStaysUnconvertedAfterFocusChange();
  void repeatKeepsMappedOutputWhenModifiersChange();
  void keyUpAllowsFreshDecision();
  void heldKeysHaveIndependentDecisions();
  void resetForgetsHeldKeys();
  void invalidButtonIsNotMapped();
  void keyStateRestoresControlAndPreservesInputSwitch();
  void fullShortcutKeyStatePreservesShiftAndInputSwitch();
  void shiftTapReusesReleasedButtonWithoutLeavingModifiers_data();
  void shiftTapReusesReleasedButtonWithoutLeavingModifiers();
  void keyStateResetReleasesHeldKeys();
  void keyStateRestoresBothControlKeys();
  void genericRulesRespectAppBoundaries();
  void genericPressKeepsOutputAcrossFocusAndConfigChanges();
  void unmappedRepeatRetainsNativeSemantics();
  void keyStatePairsChangedOutputKeyAndReleasesOnReset();

private:
  Arch m_arch;
  Log m_log;
};
