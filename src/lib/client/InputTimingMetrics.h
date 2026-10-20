/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

// Optional diagnostics for the client's parsing and event submission thread.
// These times do not include network transit or the display's response to input.
// All storage is fixed size; disabled and unsampled paths do not read the clock.
class InputTimingMetrics
{
public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  enum class Kind
  {
    MouseParseToSubmit,
    PendingDwell,
    Batch
  };

  static constexpr size_t kKindCount = 3;
  static constexpr uint64_t kSampleEvery = 16;
  static constexpr auto kReportInterval = std::chrono::seconds(10);

  struct Sample
  {
    TimePoint started{};
    bool active = false;
  };

  struct Summary
  {
    uint64_t samples = 0;
    double meanUs = 0;
    double p50UpperUs = 0;
    double p95UpperUs = 0;
    double p99UpperUs = 0;
    double maxUs = 0;
  };

  struct Report
  {
    std::array<Summary, kKindCount> metrics{};

    const Summary &get(Kind kind) const
    {
      return metrics[static_cast<size_t>(kind)];
    }
  };

  explicit InputTimingMetrics(bool enabled = enabledFromEnvironment()) : m_enabled(enabled)
  {
  }

  static bool enabledFromEnvironment()
  {
    const char *value = std::getenv("DESKBRIDGE_INPUT_TIMING");
    return value != nullptr && std::strcmp(value, "1") == 0;
  }

  bool isEnabled() const
  {
    return m_enabled;
  }

  Sample beginSample(Kind kind)
  {
    if (!m_enabled || (m_sampleCounters[static_cast<size_t>(kind)]++ % kSampleEvery) != 0) {
      return {};
    }

    const auto now = Clock::now();
    startWindow(now);
    return {now, true};
  }

  void finishSample(Kind kind, Sample sample)
  {
    if (m_enabled && sample.active) {
      finishSample(kind, sample, Clock::now());
    }
  }

  // Explicit timestamps and durations also support deterministic unit tests.
  void finishSample(Kind kind, Sample sample, TimePoint ended)
  {
    if (m_enabled && sample.active) {
      recordElapsed(kind, std::chrono::duration_cast<std::chrono::nanoseconds>(ended - sample.started));
    }
  }

  void recordElapsed(Kind kind, std::chrono::nanoseconds elapsed)
  {
    if (m_enabled) {
      m_histograms[static_cast<size_t>(kind)].add(static_cast<uint64_t>(std::max<int64_t>(0, elapsed.count())));
    }
  }

  // Call after a sampled batch to avoid reading the clock on every input event.
  bool takeReport(Report &report)
  {
    return m_enabled && takeReportAt(Clock::now(), report);
  }

  bool takeReportAt(TimePoint now, Report &report)
  {
    if (!m_enabled) {
      return false;
    }
    startWindow(now);
    if (now - m_windowStarted < kReportInterval) {
      return false;
    }

    m_windowStarted = now;
    bool hasSamples = false;
    for (size_t i = 0; i < kKindCount; ++i) {
      report.metrics[i] = m_histograms[i].summary();
      hasSamples |= report.metrics[i].samples != 0;
      m_histograms[i] = {};
    }
    return hasSamples;
  }

private:
  struct Histogram
  {
    // Logarithmic bins from 1 us to about 8 s, then an overflow bin. The
    // overflow bin uses the observed maximum as its percentile upper bound.
    static constexpr size_t kBucketCount = 25;
    static constexpr auto kBounds = [] {
      std::array<uint64_t, kBucketCount - 1> bounds{};
      for (size_t i = 0; i < bounds.size(); ++i) {
        bounds[i] = uint64_t{1000} << i;
      }
      return bounds;
    }();

    void add(uint64_t elapsedNs)
    {
      const auto bin = std::lower_bound(kBounds.begin(), kBounds.end(), elapsedNs) - kBounds.begin();
      ++bins[static_cast<size_t>(bin)];
      ++count;
      totalNs += static_cast<double>(elapsedNs);
      maxNs = (std::max)(maxNs, elapsedNs);
    }

    double percentileUpperUs(uint64_t percent) const
    {
      // Divide before multiplying so the rank calculation cannot overflow.
      const uint64_t rank = (count / 100) * percent + ((count % 100) * percent + 99) / 100;
      uint64_t accumulated = 0;
      for (size_t i = 0; i < bins.size(); ++i) {
        accumulated += bins[i];
        if (accumulated >= rank) {
          const uint64_t upperNs = i < kBounds.size() ? kBounds[i] : maxNs;
          return static_cast<double>(upperNs) / 1000.0;
        }
      }
      return 0;
    }

    Summary summary() const
    {
      if (count == 0) {
        return {};
      }
      return {
          count,
          totalNs / static_cast<double>(count) / 1000.0,
          percentileUpperUs(50),
          percentileUpperUs(95),
          percentileUpperUs(99),
          static_cast<double>(maxNs) / 1000.0
      };
    }

    std::array<uint64_t, kBucketCount> bins{};
    uint64_t count = 0;
    uint64_t maxNs = 0;
    double totalNs = 0;
  };

  void startWindow(TimePoint now)
  {
    if (!m_hasWindow) {
      m_windowStarted = now;
      m_hasWindow = true;
    }
  }

  const bool m_enabled;
  bool m_hasWindow = false;
  TimePoint m_windowStarted{};
  std::array<uint64_t, kKindCount> m_sampleCounters{};
  std::array<Histogram, kKindCount> m_histograms{};
};
