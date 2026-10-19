// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// The bench of the package (ticket 31, scope 3): the ticks of a script on a
// worker thread placed like the worker of a node - on Android the fast
// cores with an audio priority, on Apple platforms user-interactive - and
// by default paced at the duration of a block, as the audio callback paces
// the worker; a bench on the main thread measures another core and another
// clock (ticket 28). Reports the tick times, the creation of the context,
// the load and the memory of the context.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "aud_audio_script.h"
#include "aud_clock.h"
#include "aud_script_placement.hpp"
#include "aud_script_runner.hpp"
#include "aud_script_stats.hpp"
#include "aud_script_sync.hpp"

namespace aud_script {
namespace {

constexpr size_t kMegabyte = 1024u * 1024u;
constexpr uint32_t kMaxTicks = 10000000;

struct AudBenchJob {
  const AudScriptBenchRequest* request = nullptr;
  AudScriptBenchResult result = {};
  int32_t code = AUD_OK;
};

bool valid(const AudScriptBenchRequest* request,
           const AudScriptBenchResult* result) {
  if (request == nullptr || result == nullptr) return false;
  if (request->struct_size < sizeof(AudScriptBenchRequest)) return false;
  if (result->struct_size < sizeof(AudScriptBenchResult)) return false;
  if (request->source == nullptr || request->frames == 0) return false;
  if (request->ticks == 0 || request->ticks > kMaxTicks ||
      request->warmup_ticks > kMaxTicks) {
    return false;
  }
  if (!(request->sample_rate >= 8000 && request->sample_rate <= 384000)) {
    return false;
  }
  return request->tempo >= 1 && request->tempo <= 999;
}

// The value below which `share` of the sorted `times` lie.
double percentile(const std::vector<double>& sorted, double share) {
  if (sorted.empty()) return 0;
  const double rank = std::ceil(share * static_cast<double>(sorted.size()));
  const size_t index = rank < 1 ? 0 : static_cast<size_t>(rank) - 1;
  return sorted[std::min(index, sorted.size() - 1)];
}

void bench(AudBenchJob& job) {
  const AudScriptBenchRequest& request = *job.request;
  AudScriptBenchResult& result = job.result;
  const AudScriptPlacement placement = audScriptPlaceCurrentThread(
      (request.flags & AUD_SCRIPT_BENCH_FAST_CORES) != 0);
  result.placement = placement.mode;
  result.priority = placement.priority;
  result.cpus = placement.cpus;
  result.qos_class = placement.qos;
  AudScriptRunner runner;
  AudScriptLoad load;
  load.source = request.source;
  if (request.name != nullptr && request.name[0] != 0) load.name = request.name;
  load.memoryLimit =
      (request.memory_limit_mb > 0 ? request.memory_limit_mb : 32) * kMegabyte;
  AudScriptLoadTimes times;
  std::string error;
  std::unique_ptr<AudScriptState> state = runner.load(load, &times, &error);
  if (state == nullptr) {
    const std::string text =
        audScriptClip(error, AUD_SCRIPT_BENCH_ERROR_BYTES - 1);
    std::memcpy(result.error, text.c_str(), text.size() + 1);
    job.code = AUD_ERROR_FAILED;
    return;
  }
  runner.adopt(std::move(state));
  result.context_create_us = times.createUs;
  result.load_us = times.loadUs;
  const double frames = request.frames;
  const double beatsPerBlock =
      frames * request.tempo / 60 / request.sample_rate;
  const double blockUs = frames / request.sample_rate * 1e6;
  result.block_us = blockUs;
  const int64_t budgetNs =
      static_cast<int64_t>(request.tick_budget_ms > 0 ? request.tick_budget_ms
                                                      : 100) *
      1000000LL;
  const bool paced = (request.flags & AUD_SCRIPT_BENCH_UNPACED) == 0;
  const uint32_t total = request.warmup_ticks + request.ticks;
  std::vector<double> timed;
  timed.reserve(request.ticks);
  double sum = 0;
  const int64_t start = aud_clock_now_ns();
  for (uint32_t i = 0; i < total; ++i) {
    if (paced) {
      // The tick of block i starts i block durations after the first.
      const int64_t due = start + static_cast<int64_t>(i * blockUs * 1000.0);
      const int64_t wait = due - aud_clock_now_ns();
      if (wait > 0) std::this_thread::sleep_for(std::chrono::nanoseconds(wait));
    }
    AudScriptBlock block;
    block.tick = i + 1;
    block.b0 = (i + 1) * beatsPerBlock;
    block.tempo = request.tempo;
    block.playing = true;
    block.sampleRate = request.sample_rate;
    block.frames = request.frames;
    const AudScriptTickResult tick =
        runner.runTick(block, nullptr, 0, budgetNs);
    if (i < request.warmup_ticks) continue;
    timed.push_back(tick.timeUs);
    sum += tick.timeUs;
    if (tick.timeUs > blockUs) result.over_block += 1;
    if (tick.status != AUD_SCRIPT_TICK_OK) result.aborted += 1;
  }
  // The core of the last tick.
  result.last_cpu = audScriptCurrentCpu();
  result.memory_bytes = runner.state()->context->memoryUsed();
  runner.adopt(nullptr);
  std::sort(timed.begin(), timed.end());
  result.ticks = static_cast<uint32_t>(timed.size());
  result.mean_us = sum / static_cast<double>(timed.size());
  result.p50_us = percentile(timed, 0.5);
  result.p99_us = percentile(timed, 0.99);
  result.max_us = timed.back();
}

void benchEntry(void* user) {
  auto& job = *static_cast<AudBenchJob*>(user);
  try {
    bench(job);
  } catch (...) {
    job.code = AUD_ERROR_OUT_OF_MEMORY;
  }
}

}  // namespace
}  // namespace aud_script

AUD_EXPORT int32_t aud_audio_script_bench_run(
    const AudScriptBenchRequest* request, AudScriptBenchResult* result) {
  using namespace aud_script;
  if (!valid(request, result)) return AUD_ERROR_INVALID_ARGUMENT;
  AudBenchJob job;
  job.request = request;
  job.result.struct_size = sizeof(AudScriptBenchResult);
  AudScriptThread thread;
  if (!thread.start(benchEntry, &job)) return AUD_ERROR_FAILED;
  thread.join();
  if (job.code == AUD_ERROR_FAILED) {
    std::memcpy(result->error, job.result.error, sizeof(result->error));
  }
  if (job.code != AUD_OK) return job.code;
  *result = job.result;
  return AUD_OK;
}
