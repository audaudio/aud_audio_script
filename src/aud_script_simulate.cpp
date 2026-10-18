// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// The simulation of a modulator (ticket 31): the host code of the worker -
// the same runner, tick grid and order of the input events - without a
// graph, on a thread with the stack of a worker. It reports every tick with
// its aud.send calls, so that an app compares the modulator with a
// reference outside the engine, and a test compares the node with it.

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include "aud_audio_script.h"
#include "aud_script_runner.hpp"
#include "aud_script_sync.hpp"

namespace aud_script {
namespace {

constexpr uint32_t kMaxBlocks = 1000000;
constexpr size_t kMegabyte = 1024u * 1024u;

// What the thread of a simulation works on.
struct AudSimulationJob {
  const AudScriptSimulationRequest* request = nullptr;
  int32_t loadResult = AUD_OK;
  std::vector<AudScriptSimulationTick> ticks;
  std::vector<AudScriptSend> sends;
  std::string error;
  std::string console;
  AudScriptLoadTimes times;
  bool outOfMemory = false;
};

bool valid(const AudScriptSimulationRequest* request) {
  if (request == nullptr ||
      request->struct_size < sizeof(AudScriptSimulationRequest)) {
    return false;
  }
  if (request->source == nullptr || request->frames == 0 ||
      request->blocks > kMaxBlocks) {
    return false;
  }
  if (!(request->sample_rate >= 8000 && request->sample_rate <= 384000)) {
    return false;
  }
  if (!(request->tempo >= 1 && request->tempo <= 999)) return false;
  if (request->num_events > 0 && request->events == nullptr) return false;
  for (uint32_t i = 0; i < request->num_events; ++i) {
    const AudScriptControlChange& event = request->events[i];
    if (event.channel > 15 || event.number > 127 || event.value > 127) {
      return false;
    }
  }
  return true;
}

void simulate(AudSimulationJob& job) {
  const AudScriptSimulationRequest& request = *job.request;
  AudScriptRunner runner;
  runner.sendLog = &job.sends;
  runner.onConsole = [&job](const std::string& line) {
    job.console += line;
    job.console += '\n';
  };
  runner.onError = [&job](const std::string& text) { job.error = text; };
  AudScriptLoad load;
  load.source = request.source;
  if (request.name != nullptr && request.name[0] != 0) load.name = request.name;
  load.engine = request.engine;
  load.memoryLimit =
      (request.memory_limit_mb > 0 ? request.memory_limit_mb : 32) * kMegabyte;
  std::string error;
  std::unique_ptr<AudScriptState> state = runner.load(load, &job.times, &error);
  if (state == nullptr) {
    job.loadResult = AUD_ERROR_FAILED;
    job.error = error;
    return;
  }
  runner.adopt(std::move(state));
  // The control changes in the order of their frames; equal frames keep
  // their order.
  std::vector<AudScriptControlChange> events(
      request.events, request.events + request.num_events);
  std::stable_sort(
      events.begin(), events.end(),
      [](const AudScriptControlChange& a, const AudScriptControlChange& b) {
        return a.frame < b.frame;
      });
  const double frames = request.frames;
  const double beatsPerBlock =
      frames * request.tempo / 60 / request.sample_rate;
  const int64_t budgetNs =
      static_cast<int64_t>(request.tick_budget_ms > 0 ? request.tick_budget_ms
                                                      : 100) *
      1000000LL;
  std::vector<AudScriptInput> inputs;
  size_t next = 0;
  job.ticks.reserve(request.blocks);
  for (uint32_t n = 1; n < request.blocks; ++n) {
    // The events of the blocks before block n.
    inputs.clear();
    while (next < events.size() && events[next].frame / request.frames < n) {
      AudScriptInput input;
      input.kind = AudScriptInput::kControlChange;
      input.channel = events[next].channel;
      input.number = events[next].number;
      input.value = events[next].value;
      inputs.push_back(input);
      next += 1;
    }
    AudScriptBlock block;
    block.tick = n;
    block.b0 = n * beatsPerBlock;
    block.tempo = request.tempo;
    block.playing = true;
    block.sampleRate = request.sample_rate;
    block.frames = request.frames;
    const uint32_t first = static_cast<uint32_t>(job.sends.size());
    const AudScriptTickResult result =
        runner.runTick(block, inputs.data(), inputs.size(), budgetNs);
    AudScriptSimulationTick tick = {};
    tick.tick = n;
    tick.offset = result.grid.offset;
    tick.first_send = first;
    tick.num_sends = static_cast<uint32_t>(job.sends.size()) - first;
    tick.status = result.status;
    tick.beat = result.grid.beat;
    tick.time_us = result.timeUs;
    job.ticks.push_back(tick);
  }
  // The contexts end on the thread that ran them.
  runner.adopt(nullptr);
}

void simulateEntry(void* user) {
  auto& job = *static_cast<AudSimulationJob*>(user);
  try {
    simulate(job);
  } catch (...) {
    job.outOfMemory = true;
  }
}

// Rounds `bytes` up to a multiple of 8.
size_t aligned(size_t bytes) { return (bytes + 7) & ~static_cast<size_t>(7); }

// The result in one block of memory: the struct, the ticks, the sends and
// the two texts.
AudScriptSimulation* pack(const AudSimulationJob& job) {
  const size_t header = aligned(sizeof(AudScriptSimulation));
  const size_t ticks =
      aligned(job.ticks.size() * sizeof(AudScriptSimulationTick));
  const size_t sends =
      aligned(job.sends.size() * sizeof(AudScriptSimulationSend));
  const size_t error = job.error.size() + 1;
  const size_t console = job.console.size() + 1;
  auto* memory =
      static_cast<char*>(std::malloc(header + ticks + sends + error + console));
  if (memory == nullptr) return nullptr;
  auto* result = reinterpret_cast<AudScriptSimulation*>(memory);
  std::memset(result, 0, sizeof(AudScriptSimulation));
  result->struct_size = sizeof(AudScriptSimulation);
  result->load_result = job.loadResult;
  result->num_ticks = static_cast<uint32_t>(job.ticks.size());
  result->num_sends = static_cast<uint32_t>(job.sends.size());
  auto* tickArray = reinterpret_cast<AudScriptSimulationTick*>(memory + header);
  if (!job.ticks.empty()) {
    std::memcpy(tickArray, job.ticks.data(),
                job.ticks.size() * sizeof(AudScriptSimulationTick));
  }
  auto* sendArray =
      reinterpret_cast<AudScriptSimulationSend*>(memory + header + ticks);
  for (size_t i = 0; i < job.sends.size(); ++i) {
    sendArray[i].output = job.sends[i].output;
    sendArray[i].id = job.sends[i].id;
    sendArray[i].value = job.sends[i].value;
  }
  char* errorText = memory + header + ticks + sends;
  std::memcpy(errorText, job.error.c_str(), error);
  char* consoleText = errorText + error;
  std::memcpy(consoleText, job.console.c_str(), console);
  result->ticks = tickArray;
  result->sends = sendArray;
  result->error = errorText;
  result->console = consoleText;
  result->context_create_us = job.times.createUs;
  result->load_us = job.times.loadUs;
  return result;
}

}  // namespace
}  // namespace aud_script

AUD_EXPORT int32_t aud_audio_script_simulate(
    const AudScriptSimulationRequest* request, AudScriptSimulation** result) {
  using namespace aud_script;
  if (result == nullptr) return AUD_ERROR_INVALID_ARGUMENT;
  *result = nullptr;
  if (!valid(request)) return AUD_ERROR_INVALID_ARGUMENT;
  if (!audScriptEngineAvailable(request->engine)) return AUD_ERROR_UNSUPPORTED;
  try {
    AudSimulationJob job;
    job.request = request;
    AudScriptThread thread;
    if (!thread.start(simulateEntry, &job)) return AUD_ERROR_FAILED;
    thread.join();
    if (job.outOfMemory) return AUD_ERROR_OUT_OF_MEMORY;
    *result = pack(job);
    return *result != nullptr ? AUD_OK : AUD_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return AUD_ERROR_OUT_OF_MEMORY;
  }
}

AUD_EXPORT void aud_audio_script_simulation_free(AudScriptSimulation* result) {
  std::free(result);
}
