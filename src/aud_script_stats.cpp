// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

#include "aud_script_stats.hpp"

#include <algorithm>
#include <cstring>

namespace aud_script {

std::string audScriptClip(const std::string& text, size_t limit) {
  if (text.size() <= limit) return text;
  size_t end = limit;
  // Back to the start of a UTF-8 sequence: continuation bytes are 10xxxxxx.
  while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) {
    end -= 1;
  }
  return text.substr(0, end);
}

AudScriptGlobalStats& AudScriptGlobalStats::get() {
  // Never destroyed: a worker may still count while the process exits.
  static AudScriptGlobalStats* stats = new AudScriptGlobalStats();
  return *stats;
}

void AudScriptGlobalStats::countTick(double us, int32_t status) {
  if (status != AUD_SCRIPT_TICK_OK) {
    abortedTicks_.fetch_add(1, std::memory_order_relaxed);
  }
  histogram_.add(us);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    tickSumUs_ += us;
    tickMaxUs_ = std::max(tickMaxUs_, us);
  }
  // Last and with release order: a reader that sees the tick sees what the
  // worker published before it, its result included.
  ticks_.fetch_add(1, std::memory_order_release);
}

void AudScriptGlobalStats::countLoad(int32_t result, double createUs,
                                     double loadUs) {
  loads_.fetch_add(1, std::memory_order_relaxed);
  if (result != AUD_OK) {
    failedLoads_.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  createUs_ = createUs;
  loadUs_ = loadUs;
}

void AudScriptGlobalStats::setPlacement(const AudScriptPlacement& placement) {
  placementMode_.store(placement.mode, std::memory_order_relaxed);
  priority_.store(placement.priority, std::memory_order_relaxed);
  cpus_.store(placement.cpus, std::memory_order_relaxed);
  qos_.store(placement.qos, std::memory_order_relaxed);
}

void AudScriptGlobalStats::setError(const std::string& text) {
  std::lock_guard<std::mutex> lock(mutex_);
  error_ = audScriptClip(text, kMaxText);
}

void AudScriptGlobalStats::addConsoleLine(const std::string& line) {
  std::lock_guard<std::mutex> lock(mutex_);
  console_.push_back(audScriptClip(line, kMaxText));
  while (console_.size() > kConsoleLines) console_.pop_front();
}

void AudScriptGlobalStats::read(AudScriptStats* stats) const {
  AudScriptStats s = {};
  s.struct_size = sizeof(AudScriptStats);
  s.ticks = ticks_.load(std::memory_order_acquire);
  s.late_ticks = lateTicks_.load(std::memory_order_relaxed);
  s.skipped_requests = skippedRequests_.load(std::memory_order_relaxed);
  s.aborted_ticks = abortedTicks_.load(std::memory_order_relaxed);
  s.dropped_events = droppedEvents_.load(std::memory_order_relaxed);
  s.loads = loads_.load(std::memory_order_relaxed);
  s.failed_loads = failedLoads_.load(std::memory_order_relaxed);
  s.tick_time_p50_us = histogram_.percentile(0.5);
  s.tick_time_p99_us = histogram_.percentile(0.99);
  s.placement = placementMode_.load(std::memory_order_relaxed);
  s.priority = priority_.load(std::memory_order_relaxed);
  s.cpus = cpus_.load(std::memory_order_relaxed);
  s.qos_class = qos_.load(std::memory_order_relaxed);
  s.lookahead = lookahead_.load(std::memory_order_relaxed);
  s.memory_bytes = memory_.load(std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    s.tick_time_sum_us = tickSumUs_;
    s.tick_time_max_us = tickMaxUs_;
    s.context_create_us = createUs_;
    s.load_us = loadUs_;
  }
  const uint64_t counted = histogram_.count();
  s.tick_time_mean_us =
      counted > 0 ? s.tick_time_sum_us / static_cast<double>(counted) : 0;
  std::memcpy(stats, &s, sizeof(s));
}

std::string AudScriptGlobalStats::error() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return error_;
}

std::string AudScriptGlobalStats::console() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::string text;
  for (const std::string& line : console_) {
    text += line;
    text += '\n';
  }
  return text;
}

void AudScriptGlobalStats::reset() {
  ticks_.store(0, std::memory_order_relaxed);
  lateTicks_.store(0, std::memory_order_relaxed);
  skippedRequests_.store(0, std::memory_order_relaxed);
  abortedTicks_.store(0, std::memory_order_relaxed);
  droppedEvents_.store(0, std::memory_order_relaxed);
  loads_.store(0, std::memory_order_relaxed);
  failedLoads_.store(0, std::memory_order_relaxed);
  memory_.store(0, std::memory_order_relaxed);
  lookahead_.store(0, std::memory_order_relaxed);
  histogram_.reset();
  std::lock_guard<std::mutex> lock(mutex_);
  tickSumUs_ = 0;
  tickMaxUs_ = 0;
  createUs_ = 0;
  loadUs_ = 0;
  error_.clear();
  console_.clear();
}

}  // namespace aud_script
