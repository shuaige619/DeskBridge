/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QObject>

class InputTimingMetricsTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void disabled_doesNotSampleOrRecord();
  void sampling_isIndependentForEachKind();
  void histogram_reportsUpperBoundsAndExactMaximum();
  void report_respectsIntervalAndResetsSamples();
  void finishSample_recordsOnlyActiveTokens();
  void histogram_handlesOverflowAndNegativeDurations();
};
