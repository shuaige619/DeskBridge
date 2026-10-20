/*
 * SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <QObject>
#include <QTemporaryDir>

class ShortcutRulesTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase();
  void init();
  void roundTrip();
  void invalidJson_data();
  void invalidJson();
  void emptyListsRemainEmpty();
  void migrationPreservesLegacyPreferences();
  void malformedConfigurationFailsClosed();
  void saveRejectsInvalidWithoutChangingCommittedValue();
  void failedDiskWriteRestoresCommittedValue();
  void conflictsUseEffectiveAppIntersection();
  void reservedInputIsAlwaysProtected();
  void macOutputFunctionKeyLimit();
  void displayKeys_data();
  void displayKeys();
  void schemaOneMigratesWithoutWriting();
  void habitDirectionMatrix_data();
  void habitDirectionMatrix();
  void sourceFallbackAndUnknown();
  void habitPreservesRulePreferences();
  void sourceMetadata_data();
  void sourceMetadata();
  void platformModifierLabels();
  void presetConflictsUseEffectiveChords();
  void schemaTwoMigratesWithoutWriting();
  void modifierMappingMatrix_data();
  void modifierMappingMatrix();
  void fullShortcutPreferences();
  void schemaThreePreservesPreferencesWithoutEnablingGesture();
  void shiftTapPreferenceIsExplicitAndIndependent();

private:
  QTemporaryDir m_temp;
  int m_profile = 0;
};
