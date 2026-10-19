// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// The statistics of the package (ticket 31): a histogram of tick times
// fine enough for the 50th and the 99th percentile, and the counters of all
// modulators of the process. The audio thread counts late ticks and
// dropped events through lock-free atomics; the workers write the rest;
// the texts - the last error and the console lines - sit behind a mutex
// that only the workers and the control thread take.

#ifndef AUD_SCRIPT_STATS_HPP
#define AUD_SCRIPT_STATS_HPP

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>

#include "aud_audio_script.h"
#include "aud_script_engine.hpp"
#include "aud_script_placement.hpp"

namespace aud_script {

// ############################################################################
// The histogram

// Tick times in µs in bins of 2^(1/32), 2.2 % wide, from kLowestUs up to
// kLowestUs * 2^kOctaves (about 268 s); the first bin also takes what lies
// below. Counts are atomics, so that a reader sees whole counts while a
// worker writes; a percentile of a histogram that changes while it is read
// may be off by the ticks of that moment.
class AudScriptHistogram {
 public:
  static constexpr double kLowestUs = 0.25;
  static constexpr int kBinsPerOctave = 32;
  static constexpr int kOctaves = 30;
  static constexpr int kBins = kBinsPerOctave * kOctaves + 1;

  // Counts one time of `us` µs.
  void add(double us) {
    bins_[binOf(us)].fetch_add(1, std::memory_order_relaxed);
    count_.fetch_add(1, std::memory_order_relaxed);
  }

  // The time below which `share` (0 to 1) of the counted times lie: the
  // geometric centre of the bin that holds it; 0 without counts.
  double percentile(double share) const {
    const uint64_t count = count_.load(std::memory_order_relaxed);
    if (count == 0) return 0;
    const double wanted = std::ceil(share * static_cast<double>(count));
    uint64_t sum = 0;
    for (int bin = 0; bin < kBins; ++bin) {
      sum += bins_[bin].load(std::memory_order_relaxed);
      if (static_cast<double>(sum) >= wanted && sum > 0) return centreOf(bin);
    }
    return centreOf(kBins - 1);
  }

  // The times counted.
  uint64_t count() const { return count_.load(std::memory_order_relaxed); }

  void reset() {
    for (auto& bin : bins_) bin.store(0, std::memory_order_relaxed);
    count_.store(0, std::memory_order_relaxed);
  }

  // The bin of `us`.
  static int binOf(double us) {
    if (!(us > kLowestUs)) return 0;
    const double position = std::log2(us / kLowestUs) * kBinsPerOctave;
    if (!(position < kBins - 1)) return kBins - 1;
    return static_cast<int>(position);
  }

  // The geometric centre of a bin in µs.
  static double centreOf(int bin) {
    return kLowestUs * std::exp2((bin + 0.5) / kBinsPerOctave);
  }

 private:
  std::atomic<uint64_t> bins_[kBins] = {};
  std::atomic<uint64_t> count_{0};
};

// ############################################################################
// The statistics of the process

// The counters of all modulators of the process; one instance,
// AudScriptGlobalStats::get().
class AudScriptGlobalStats {
 public:
  // The console lines kept.
  static constexpr size_t kConsoleLines = 64;
  // The longest console line and error text kept, in bytes.
  static constexpr size_t kMaxText = kScriptMaxText;

  static AudScriptGlobalStats& get();

  // [realtime] A block whose values were late; input events without room.
  void countLate() { lateTicks_.fetch_add(1, std::memory_order_relaxed); }
  void countDropped(uint64_t events) {
    droppedEvents_.fetch_add(events, std::memory_order_relaxed);
  }
  // [realtime, worker] A request that a newer one replaced.
  void countSkipped(uint64_t requests) {
    skippedRequests_.fetch_add(requests, std::memory_order_relaxed);
  }

  // [worker] A tick of `us` µs that ended with `status`
  // (AUD_SCRIPT_TICK_*).
  void countTick(double us, int32_t status);

  // [worker] A load that ended with `result` and its times in µs.
  void countLoad(int32_t result, double createUs, double loadUs);

  // [realtime] The lookahead of the block a modulator renders.
  void setLookahead(uint32_t blocks) {
    lookahead_.store(blocks, std::memory_order_relaxed);
  }

  // [worker] The bytes the current context holds.
  void setMemory(uint64_t bytes) {
    memory_.store(bytes, std::memory_order_relaxed);
  }

  // [worker] The placement of a worker that placed itself.
  void setPlacement(const AudScriptPlacement& placement);

  // [worker] The core a worker ran its last tick on.
  void setLastCpu(int32_t cpu) {
    lastCpu_.store(cpu, std::memory_order_relaxed);
  }

  // [worker, control] The last error, the console lines.
  void setError(const std::string& text);
  void addConsoleLine(const std::string& line);

  // [any thread] Reads the counters into `stats`, whose struct_size the
  // caller checked.
  void read(AudScriptStats* stats) const;

  // [any thread] The last error; the console lines, one per line.
  std::string error() const;
  std::string console() const;

  // [any thread] Resets everything.
  void reset();

 private:
  AudScriptGlobalStats() = default;

  std::atomic<uint64_t> ticks_{0};
  std::atomic<uint64_t> lateTicks_{0};
  std::atomic<uint64_t> skippedRequests_{0};
  std::atomic<uint64_t> abortedTicks_{0};
  std::atomic<uint64_t> droppedEvents_{0};
  std::atomic<uint64_t> loads_{0};
  std::atomic<uint64_t> failedLoads_{0};
  std::atomic<uint64_t> memory_{0};
  std::atomic<uint32_t> placementMode_{0};
  std::atomic<int32_t> priority_{0};
  std::atomic<uint64_t> cpus_{0};
  std::atomic<uint32_t> qos_{0};
  std::atomic<uint32_t> lookahead_{0};
  std::atomic<int32_t> lastCpu_{-1};
  std::atomic<uint32_t> workerCores_{0};
  AudScriptHistogram histogram_;
  // The sum and the longest of the tick times, the times of the last load:
  // written by the workers under the mutex, read under it.
  mutable std::mutex mutex_;
  double tickSumUs_ = 0;
  double tickMaxUs_ = 0;
  double createUs_ = 0;
  double loadUs_ = 0;
  std::string error_;
  std::deque<std::string> console_;
};

// `text` cut to `limit` bytes without splitting a UTF-8 sequence.
std::string audScriptClip(const std::string& text, size_t limit);

}  // namespace aud_script

#endif  // AUD_SCRIPT_STATS_HPP
