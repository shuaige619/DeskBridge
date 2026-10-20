/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "InputTimingMetricsTests.h"

#include "client/InputTimingMetrics.h"

#include <QTest>

namespace {
using Kind = InputTimingMetrics::Kind;
using Clock = InputTimingMetrics::Clock;
using Report = InputTimingMetrics::Report;
using namespace std::chrono_literals;

const auto kStart = Clock::time_point(100s);
} // namespace

void InputTimingMetricsTests::disabled_doesNotSampleOrRecord()
{
  InputTimingMetrics metrics(false);
  QVERIFY(!metrics.isEnabled());
  for (int i = 0; i < 32; ++i) {
    QVERIFY(!metrics.beginSample(Kind::MouseParseToSubmit).active);
  }
  metrics.recordElapsed(Kind::MouseParseToSubmit, 5ms);
  metrics.finishSample(Kind::Batch, {kStart, true}, kStart + 1ms);
  Report report;
  QVERIFY(!metrics.takeReportAt(kStart, report));
  QVERIFY(!metrics.takeReportAt(kStart + 30s, report));
}

void InputTimingMetricsTests::sampling_isIndependentForEachKind()
{
  InputTimingMetrics metrics(true);
  for (const auto kind : {Kind::MouseParseToSubmit, Kind::PendingDwell, Kind::Batch}) {
    QVERIFY(metrics.beginSample(kind).active);
    for (uint64_t i = 1; i < InputTimingMetrics::kSampleEvery; ++i) {
      QVERIFY(!metrics.beginSample(kind).active);
    }
    QVERIFY(metrics.beginSample(kind).active);
  }
}

void InputTimingMetricsTests::histogram_reportsUpperBoundsAndExactMaximum()
{
  InputTimingMetrics metrics(true);
  Report report;
  QVERIFY(!metrics.takeReportAt(kStart, report));
  for (const auto duration : {0ns, 1000ns, 1001ns, 2000ns, 3000ns, 8000ns, 99999ns}) {
    metrics.recordElapsed(Kind::MouseParseToSubmit, duration);
  }
  QVERIFY(metrics.takeReportAt(kStart + 10s, report));
  const auto &summary = report.get(Kind::MouseParseToSubmit);
  QCOMPARE(summary.samples, uint64_t{7});
  QCOMPARE(summary.meanUs, 115.0 / 7.0);
  QCOMPARE(summary.p50UpperUs, 2.0);
  QCOMPARE(summary.p95UpperUs, 128.0);
  QCOMPARE(summary.p99UpperUs, 128.0);
  QCOMPARE(summary.maxUs, 99.999);
  QCOMPARE(report.get(Kind::PendingDwell).samples, uint64_t{0});
}

void InputTimingMetricsTests::report_respectsIntervalAndResetsSamples()
{
  InputTimingMetrics metrics(true);
  Report report;
  QVERIFY(!metrics.takeReportAt(kStart, report));
  metrics.recordElapsed(Kind::Batch, 2ms);
  metrics.recordElapsed(Kind::PendingDwell, 250us);
  QVERIFY(!metrics.takeReportAt(kStart + 10s - 1ns, report));
  QVERIFY(metrics.takeReportAt(kStart + 10s, report));
  QCOMPARE(report.get(Kind::Batch).samples, uint64_t{1});
  QCOMPARE(report.get(Kind::PendingDwell).maxUs, 250.0);
  QVERIFY(!metrics.takeReportAt(kStart + 20s, report));
  metrics.recordElapsed(Kind::Batch, 3ms);
  QVERIFY(!metrics.takeReportAt(kStart + 20s + 1ns, report));
  QVERIFY(metrics.takeReportAt(kStart + 30s, report));
  QCOMPARE(report.get(Kind::Batch).samples, uint64_t{1});
  QCOMPARE(report.get(Kind::Batch).maxUs, 3000.0);
  QCOMPARE(report.get(Kind::PendingDwell).samples, uint64_t{0});
}

void InputTimingMetricsTests::finishSample_recordsOnlyActiveTokens()
{
  InputTimingMetrics metrics(true);
  Report report;
  QVERIFY(!metrics.takeReportAt(kStart, report));
  metrics.finishSample(Kind::MouseParseToSubmit, {kStart, false}, kStart + 50ms);
  metrics.finishSample(Kind::MouseParseToSubmit, {kStart, true}, kStart + 125us);
  QVERIFY(metrics.takeReportAt(kStart + 10s, report));
  QCOMPARE(report.get(Kind::MouseParseToSubmit).samples, uint64_t{1});
  QCOMPARE(report.get(Kind::MouseParseToSubmit).maxUs, 125.0);
}

void InputTimingMetricsTests::histogram_handlesOverflowAndNegativeDurations()
{
  InputTimingMetrics metrics(true);
  Report report;
  QVERIFY(!metrics.takeReportAt(kStart, report));
  metrics.recordElapsed(Kind::PendingDwell, -1ns);
  metrics.recordElapsed(Kind::Batch, 20s);
  QVERIFY(metrics.takeReportAt(kStart + 10s, report));
  QCOMPARE(report.get(Kind::PendingDwell).maxUs, 0.0);
  QCOMPARE(report.get(Kind::Batch).p95UpperUs, 20'000'000.0);
  QCOMPARE(report.get(Kind::Batch).maxUs, 20'000'000.0);
}

QTEST_GUILESS_MAIN(InputTimingMetricsTests)
