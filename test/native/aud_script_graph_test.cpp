// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// The native tests of the modulator on the graph engine (ticket 31): the
// registration; the ramp and the step at their exact offsets; the
// emission of changed values only; the late worker that holds, counts and
// catches up; the skipped requests; the aborted tick; the memory limit;
// the failed load that keeps the old context; deterministic offline
// renders; the emissions of the synthetic workload equal to the
// simulation; JavaScriptCore on Apple platforms; and the probe that proves
// the RealtimeSanitizer run catches an allocation. Every graph ends with
// no violation of the engine's watchdog.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "aud_script_fixture.hpp"

using namespace aud_script_test;

namespace {

// Rates and blocks whose grid points lie far from the rounding of the
// beats: the node predicts the beat of a block from the transport's ticks,
// the simulation multiplies it, and both must floor alike.
constexpr double kSafeRate = 44100;
constexpr uint32_t kSafeFrames = 300;
constexpr double kSafeTempo = 113;

// Waits until the process counted `ticks` ticks; false after 5 s.
bool waitForTicks(uint64_t ticks) {
  for (int i = 0; i < 5000; ++i) {
    if (stats().ticks >= ticks) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return false;
}

}  // namespace

// ############################################################################
// Registration

AUD_TEST(registers_the_modulator_with_an_engine_once) {
  Graph g;
  const AudNodeDescriptor* d =
      aud_graph_node_type_by_id(g.graph, AUD_SCRIPT_MODULATOR_TYPE_ID);
  AUD_CHECK(d != nullptr);
  if (d != nullptr) {
    AUD_CHECK(d->abi_major == AUD_ABI_VERSION_MAJOR);
    AUD_CHECK(d->abi_minor == AUD_ABI_VERSION_MINOR);
    AUD_CHECK(d->num_input_buses == 0 && d->num_output_buses == 0);
    AUD_CHECK(d->num_event_inputs == 1 && d->num_event_outputs == 16);
    AUD_CHECK(std::strcmp(d->event_inputs[0].id, "in") == 0);
    AUD_CHECK(std::strcmp(d->event_outputs[15].id, "out15") == 0);
    AUD_CHECK(d->num_params == AUD_SCRIPT_NUM_PARAMS);
    AUD_CHECK(std::strcmp(d->params[AUD_SCRIPT_PARAM_MODE0 + 3].id, "mode3") ==
              0);
    AUD_CHECK(std::strcmp(d->params[AUD_SCRIPT_PARAM_MEMORY_LIMIT].id,
                          "memory_limit") == 0);
    AUD_CHECK(d->params[AUD_SCRIPT_PARAM_MEMORY_LIMIT].default_value == 32);
    AUD_CHECK(std::strcmp(d->params[AUD_SCRIPT_PARAM_TICK_BUDGET].id,
                          "tick_budget") == 0);
    AUD_CHECK(d->params[AUD_SCRIPT_PARAM_TICK_BUDGET].default_value == 100);
    AUD_CHECK(d->num_string_keys == 3);
    AUD_CHECK(std::strcmp(d->string_keys[AUD_SCRIPT_KEY_ENGINE].id, "engine") ==
              0);
    const uint32_t wanted = AUD_NODE_CAP_VARIABLE_BLOCK | AUD_NODE_CAP_EVENTS |
                            AUD_NODE_CAP_STRINGS | AUD_NODE_CAP_TRANSPORT |
                            AUD_NODE_CAP_EVENT_OUTPUT;
    AUD_CHECK(d->capabilities == wanted);
  }
  AUD_CHECK(aud_audio_script_register(g.host()) == AUD_ERROR_DUPLICATE_TYPE);
  AUD_CHECK(aud_audio_script_register(nullptr) == AUD_ERROR_INVALID_ARGUMENT);
  AudHostApi other = *g.host();
  other.register_node_type = nullptr;
  AUD_CHECK(aud_audio_script_register(&other) == AUD_ERROR_INVALID_ARGUMENT);
  other = *g.host();
  other.abi_major = 1;
  AUD_CHECK(aud_audio_script_register(&other) == AUD_ERROR_ABI_MAJOR);
  other = *g.host();
  other.struct_size = 64;
  AUD_CHECK(aud_audio_script_register(&other) == AUD_ERROR_ABI_MINOR);
}

// ############################################################################
// Ramp, step, changes

AUD_TEST(ramps_a_new_value_to_the_last_frame_of_the_block) {
  aud_audio_script_reset_stats();
  Graph g(256);
  AUD_CHECK(g.load("aud.onTick = function () {\n"
                   "  aud.send(0, 300, aud.time.tick / 1000);\n"
                   "};\n") == AUD_OK);
  g.renderOffline(10);
  // Block 0 has no tick; block n emits the value of tick n at its last
  // frame.
  AUD_CHECK(recordsOf(0).empty());
  for (int64_t block = 1; block < 10; ++block) {
    const std::vector<Record> records = recordsOf(block);
    AUD_CHECK(records.size() == 1);
    if (records.size() != 1) continue;
    AUD_CHECK(records[0].offset == 255);
    AUD_CHECK(records[0].port == 0 && records[0].id == 300);
    AUD_CHECK(records[0].value == static_cast<float>(block / 1000.0));
  }
  const AudScriptStats s = stats();
  // Tick 10, the request the last block issued, may have run too.
  AUD_CHECK(s.ticks == 9 || s.ticks == 10);
  AUD_CHECK(s.late_ticks == 0);
  AUD_CHECK(s.loads == 1 && s.failed_loads == 0);
  AUD_CHECK(s.memory_bytes > 0);
  AUD_CHECK(s.context_create_us > 0 && s.load_us > 0);
}

AUD_TEST(steps_from_the_old_to_the_new_value_at_the_grid_point) {
  aud_audio_script_reset_stats();
  const std::string script =
      "aud.onTick = function () {\n"
      "  aud.send(1, 301, aud.time.tick / 100);\n"
      "  aud.send(2, 302, aud.time.beat);\n"
      "};\n";
  Graph g(kSafeFrames, kSafeRate);
  g.param(AUD_SCRIPT_PARAM_MODE0 + 1, AUD_SCRIPT_MODE_STEP);
  g.play(kSafeTempo);
  AUD_CHECK(g.load(script) == AUD_OK);
  const uint32_t blocks = 200;
  g.renderOffline(blocks);
  AudScriptSimulation* simulation =
      simulate(script, kSafeRate, kSafeFrames, kSafeTempo, blocks);
  AUD_CHECK(simulation != nullptr && simulation->num_ticks == blocks - 1);
  if (simulation == nullptr) return;
  uint32_t inside = 0;
  for (uint32_t i = 0; i < simulation->num_ticks; ++i) {
    const AudScriptSimulationTick& tick = simulation->ticks[i];
    const int64_t block = tick.tick;
    const uint32_t k = tick.offset;
    std::vector<Record> stepped;
    std::vector<Record> ramped;
    for (const Record& record : recordsOf(block)) {
      (record.port == 1 ? stepped : ramped).push_back(record);
    }
    // The stepped output: the old value one frame before k, the new at k.
    const float now = static_cast<float>(block / 100.0);
    const float before = static_cast<float>((block - 1) / 100.0);
    if (block > 1 && k > 0) {
      inside += 1;
      AUD_CHECK(stepped.size() == 2);
      if (stepped.size() == 2) {
        AUD_CHECK(stepped[0].offset == k - 1 && stepped[0].value == before);
        AUD_CHECK(stepped[1].offset == k && stepped[1].value == now);
      }
    } else {
      AUD_CHECK(stepped.size() == 1);
      if (stepped.size() == 1) {
        AUD_CHECK(stepped[0].offset == k && stepped[0].value == now);
      }
    }
    // The ramped output takes the beat of the grid point, the same as the
    // simulation's, at the last frame.
    AUD_CHECK(ramped.size() == 1);
    if (ramped.size() == 1) {
      AUD_CHECK(ramped[0].offset == kSafeFrames - 1);
      AUD_CHECK(ramped[0].value == static_cast<float>(tick.beat));
    }
  }
  // Most grid points lie inside their block.
  AUD_CHECK(inside > blocks / 2);
  aud_audio_script_simulation_free(simulation);
  AUD_CHECK(stats().late_ticks == 0);
}

AUD_TEST(emits_a_value_only_when_it_changed) {
  aud_audio_script_reset_stats();
  Graph g(128);
  AUD_CHECK(g.load("aud.onTick = function () {\n"
                   "  aud.send(2, 10, 0.5);\n"
                   "  aud.send(2, 11, Math.floor(aud.time.tick / 4) / 10);\n"
                   "  aud.send(2, 11, Math.floor(aud.time.tick / 4) / 10);\n"
                   "};\n") == AUD_OK);
  g.renderOffline(13);
  const std::vector<Record>& all = Recorder::get().records;
  std::vector<std::tuple<int64_t, uint32_t, float>> got;
  for (const Record& record : all)
    got.emplace_back(record.block, record.id, record.value);
  const std::vector<std::tuple<int64_t, uint32_t, float>> wanted = {
      {1, 10, 0.5f}, {1, 11, 0.0f},  {4, 11, 0.1f},
      {8, 11, 0.2f}, {12, 11, 0.3f},
  };
  AUD_CHECK(got == wanted);
}

AUD_TEST(a_new_context_starts_without_values_and_emits_what_differs) {
  aud_audio_script_reset_stats();
  Graph g(128);
  AUD_CHECK(g.load("aud.onTick = function () { aud.send(0, 1, 0.25); "
                   "aud.send(0, 2, 0.5); };") == AUD_OK);
  g.renderOffline(3);
  // The load sends a value too: it is the first value of the context.
  AUD_CHECK(g.load("aud.send(0, 3, 0.75);\n"
                   "aud.onTick = function () { aud.send(0, 1, 0.25); };") ==
            AUD_OK);
  g.renderOffline(3);
  std::vector<std::tuple<int64_t, uint32_t, float>> got;
  for (const Record& record : Recorder::get().records) {
    got.emplace_back(record.block, record.id, record.value);
  }
  const std::vector<std::tuple<int64_t, uint32_t, float>> wanted = {
      {1, 1, 0.25f},
      {1, 2, 0.5f},
      {4, 3, 0.75f},
  };
  AUD_CHECK(got == wanted);
}

// ############################################################################
// Input events

AUD_TEST(delivers_the_events_of_a_block_before_the_tick_of_the_next) {
  aud_audio_script_reset_stats();
  Graph g(100);
  AUD_CHECK(
      g.load("var log = [];\n"
             "aud.onControlChange = function (c, n, v) {\n"
             "  log.push(c * 1000000 + n * 1000 + v);\n"
             "};\n"
             "aud.onControl = function (id, v) { log.push(-id - v); };\n"
             "aud.onTick = function () {\n"
             "  for (var i = 0; i < log.length; ++i) aud.send(4, i, log[i]);\n"
             "  aud.send(5, 0, log.length);\n"
             "  log = [];\n"
             "};\n") == AUD_OK);
  // Two changes in block 1 and one in block 2; a numeric control in block
  // 2 through the event input.
  g.controlChange(150, 3, 74, 100);
  g.controlChange(199, 0, 1, 7);
  g.controlChange(200, 15, 127, 127);
  const AudEvent control = aud_event_control_float(42, 0.5f, 0, 0);
  AudTimestamp at{};
  at.struct_size = sizeof(AudTimestamp);
  at.domain = AUD_TIME_SAMPLE;
  at.value = 250;
  AUD_CHECK(aud_graph_send_event(g.graph, g.modulator, &control, &at, 0) ==
            AUD_OK);
  g.renderOffline(5);
  std::vector<std::tuple<int64_t, uint32_t, uint32_t, float>> got;
  for (const Record& record : Recorder::get().records) {
    got.emplace_back(record.block, record.port, record.id, record.value);
  }
  // Within a block the values come in the order the script first sent
  // their pairs.
  const std::vector<std::tuple<int64_t, uint32_t, uint32_t, float>> wanted = {
      {1, 5, 0, 0.0f},    {2, 5, 0, 2.0f},        {2, 4, 0, 3074100.0f},
      {2, 4, 1, 1007.0f}, {3, 4, 0, 15127127.0f}, {3, 4, 1, -42.5f},
      {4, 5, 0, 0.0f},
  };
  AUD_CHECK(got == wanted);
}

// ############################################################################
// The late worker

AUD_TEST(a_late_worker_holds_counts_and_catches_up) {
  aud_audio_script_reset_stats();
  Graph g(128);
  g.param(AUD_SCRIPT_PARAM_MODE0 + 3, AUD_SCRIPT_MODE_STEP);
  AUD_CHECK(g.load("aud.onTick = function () {\n"
                   "  var start = Date.now();\n"
                   "  while (Date.now() - start < 40) {}\n"
                   "  aud.send(3, 5, aud.time.tick);\n"
                   "};\n") == AUD_OK);
  // Twenty blocks far faster than the worker: the first tick runs while
  // the others come in. Which request the worker took first depends on
  // when it woke; usually the first.
  g.renderLive(1);
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  g.renderLive(19);
  AUD_CHECK(Recorder::get().records.empty());
  AUD_CHECK(stats().late_ticks == 19);
  // The first result arrived: the next block takes it as a step at frame
  // 0, late unless the worker took the newest request at once.
  AUD_CHECK(waitForTicks(1));
  g.renderLive(1);
  std::vector<Record> records = recordsOf(20);
  AUD_CHECK(records.size() == 1);
  const float first = records.empty() ? 0 : records[0].value;
  if (records.size() == 1) {
    AUD_CHECK(records[0].offset == 0 && first >= 1 && first <= 20);
  }
  // The worker ran the newest request next and skipped the ones between.
  AUD_CHECK(waitForTicks(2));
  g.renderLive(1);
  records = recordsOf(21);
  AUD_CHECK(records.size() == 1);
  if (records.size() == 1) {
    AUD_CHECK(records[0].offset == 0 && records[0].value >= 20 &&
              records[0].value > first);
  }
  const AudScriptStats s = stats();
  AUD_CHECK(s.skipped_requests >= 17);
  AUD_CHECK(s.late_ticks >= 20);
  // Date.now() counts whole milliseconds.
  AUD_CHECK(s.tick_time_max_us >= 39000);
}

AUD_TEST(a_late_result_ramps_over_the_next_block_in_ramp_mode) {
  aud_audio_script_reset_stats();
  Graph g(128);
  AUD_CHECK(g.load("aud.onTick = function () {\n"
                   "  var start = Date.now();\n"
                   "  while (Date.now() - start < 30) {}\n"
                   "  aud.send(6, 9, aud.time.tick);\n"
                   "};\n") == AUD_OK);
  g.renderLive(1);
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  g.renderLive(2);
  AUD_CHECK(waitForTicks(1));
  g.renderLive(1);
  const std::vector<Record> records = recordsOf(3);
  AUD_CHECK(records.size() == 1);
  if (records.size() == 1) {
    // A late result or, when the worker woke late, the one of the block.
    AUD_CHECK(records[0].offset == 127 && records[0].value >= 1 &&
              records[0].value <= 3);
  }
}

// ############################################################################
// Budget, memory, loads

AUD_TEST(interrupts_a_tick_over_its_budget_and_drops_its_values) {
  aud_audio_script_reset_stats();
  Graph g(128);
  g.param(AUD_SCRIPT_PARAM_TICK_BUDGET, 30);
  g.renderOffline(1);
  AUD_CHECK(g.load("aud.onTick = function () {\n"
                   "  aud.send(0, 1, aud.time.tick);\n"
                   "  if (aud.time.tick === 3) { for (;;) {} }\n"
                   "};\n") == AUD_OK);
  g.renderOffline(6);
  std::vector<std::pair<int64_t, float>> got;
  for (const Record& record : Recorder::get().records) {
    got.emplace_back(record.block, record.value);
  }
  // Tick 3 was aborted: block 3 emits nothing, the values hold.
  const std::vector<std::pair<int64_t, float>> wanted = {
      {2, 2.0f}, {4, 4.0f}, {5, 5.0f}, {6, 6.0f}};
  AUD_CHECK(got == wanted);
  const AudScriptStats s = stats();
  AUD_CHECK(s.aborted_ticks == 1);
  AUD_CHECK(s.late_ticks == 0);
  AUD_CHECK(lastError().find("interrupted") != std::string::npos);
}

AUD_TEST(the_memory_limit_stops_a_load_and_a_tick) {
  aud_audio_script_reset_stats();
  Graph g(128);
  g.param(AUD_SCRIPT_PARAM_MEMORY_LIMIT, 2);
  g.renderOffline(1);
  AUD_CHECK(
      g.load(
          "var keep = [];\n"
          "for (var i = 0; i < 1e7; ++i) keep.push({ i: i, s: 'x' + i });\n") ==
      AUD_ERROR_FAILED);
  AUD_CHECK(g.lastMessage.find("out of memory") != std::string::npos);
  AUD_CHECK(lastError().find("out of memory") != std::string::npos);
  AUD_CHECK(g.load("var keep = [];\n"
                   "aud.onTick = function () {\n"
                   "  for (var i = 0; i < 2000; ++i) keep.push([i, i, i]);\n"
                   "  aud.send(0, 2, keep.length);\n"
                   "};\n") == AUD_OK);
  g.renderOffline(40);
  const AudScriptStats s = stats();
  AUD_CHECK(s.aborted_ticks > 0);
  AUD_CHECK(s.failed_loads == 1);
  AUD_CHECK(s.memory_bytes < 2u * 1024u * 1024u);
  AUD_CHECK(lastError().find("out of memory") != std::string::npos);
  // The values of the aborted ticks were dropped: the last emitted value
  // came from a tick that ended.
  AUD_CHECK(!Recorder::get().records.empty());
}

AUD_TEST(a_failed_load_keeps_the_old_context) {
  aud_audio_script_reset_stats();
  Graph g(128);
  aud_graph_set_string(g.graph, g.modulator, AUD_SCRIPT_KEY_SCRIPT_NAME,
                       "keeper.js");
  AUD_CHECK(
      g.load("var calls = 0;\n"
             "aud.onTick = function () { aud.send(0, 7, ++calls); };\n") ==
      AUD_OK);
  g.renderOffline(3);
  AUD_CHECK(g.load("aud.onTick = function ( {") == AUD_ERROR_FAILED);
  AUD_CHECK(g.lastMessage.find("keeper.js") == 0);
  AUD_CHECK(g.lastMessage.find("SyntaxError") != std::string::npos);
  g.renderOffline(3);
  std::vector<float> values;
  for (const Record& record : Recorder::get().records)
    values.push_back(record.value);
  // The old script ticks on, its state intact.
  AUD_CHECK((values == std::vector<float>{1, 2, 3, 4, 5}));
  const AudScriptStats s = stats();
  AUD_CHECK(s.loads == 2 && s.failed_loads == 1);
}

AUD_TEST(an_exception_in_a_tick_aborts_it_and_reports_the_error) {
  aud_audio_script_reset_stats();
  Graph g(128);
  AUD_CHECK(
      g.load(
          "aud.onTick = function () {\n"
          "  aud.send(0, 1, aud.time.tick);\n"
          "  if (aud.time.tick % 2 === 0) missing();\n"
          "  console.log('tick', aud.time.tick, { a: 1 });\n"
          "};\n"
          "aud.onControlChange = function () { throw new Error('cc'); };\n") ==
      AUD_OK);
  g.controlChange(10, 0, 1, 1);
  g.renderOffline(5);
  std::vector<float> values;
  for (const Record& record : Recorder::get().records)
    values.push_back(record.value);
  AUD_CHECK((values == std::vector<float>{1, 3}));
  AUD_CHECK(stats().aborted_ticks == 2);
  AUD_CHECK(lastError().find("script: ReferenceError") == 0);
  // Room for every line the process keeps: the worker may log meanwhile.
  size_t size = 0;
  std::string console(64 * 4097 + 1, '\0');
  AUD_CHECK(aud_audio_script_console(&console[0], console.size(), &size) ==
            AUD_OK);
  console.resize(size);
  // Tick 5, the request of the last block, may have logged too.
  AUD_CHECK(console.find("script: tick 1 [object Object]\n"
                         "script: tick 3 [object Object]\n") == 0);
}

AUD_TEST(sends_with_invalid_arguments_throw_a_range_error) {
  for (const char* engine : {"quickjs", "javascriptcore"}) {
    aud_audio_script_reset_stats();
    Graph g(128);
    if (aud_graph_set_string(g.graph, g.modulator, AUD_SCRIPT_KEY_ENGINE,
                             engine) != AUD_OK) {
      continue;
    }
    AUD_CHECK(
        g.load("var errors = [];\n"
               "function attempt(o, i, v) {\n"
               "  try { aud.send(o, i, v); } catch (e) {\n"
               "    errors.push(e instanceof RangeError ? 1 : 0);\n"
               "  }\n"
               "}\n"
               "aud.onTick = function () {\n"
               "  errors = [];\n"
               "  attempt(16, 0, 0); attempt(-1, 0, 0); attempt(0.5, 0, 0);\n"
               "  attempt(0, 512, 0); attempt(0, 1.5, 0); attempt(0, 0, NaN);\n"
               "  attempt(0, 0, Infinity); attempt(0, 0);\n"
               "  aud.send(15, 511, errors.length + errors[0] * 100);\n"
               "  aud.send(0, 0, aud.outputs);\n"
               "  aud.send(0, 1, typeof aud.send === 'function' ? 1 : 0);\n"
               "};\n") == AUD_OK);
    g.renderOffline(2);
    const std::vector<Record> records = recordsOf(1);
    AUD_CHECK(records.size() == 3);
    if (records.size() == 3) {
      AUD_CHECK(records[0].port == 15 && records[0].id == 511 &&
                records[0].value == 108.0f);
      AUD_CHECK(records[1].port == 0 && records[1].value == 16.0f);
      AUD_CHECK(records[2].id == 1 && records[2].value == 1.0f);
    }
  }
}

AUD_TEST(sends_to_more_pairs_than_a_context_keeps_throw) {
  aud_audio_script_reset_stats();
  Graph g(128);
  AUD_CHECK(
      g.load("aud.onTick = function () {\n"
             "  for (var o = 0; o < 16; ++o)\n"
             "    for (var i = 0; i < 64; ++i) aud.send(o, i, 1);\n"
             "  try { aud.send(0, 100, 1); } catch (e) { aud.send(1, 0, 2); }\n"
             "};\n") == AUD_OK);
  // The graph takes up to 1024 events per block; the modulator emits 1024.
  g.renderOffline(2);
  AUD_CHECK(recordsOf(1).size() == 1024);
}

// ############################################################################
// Determinism

namespace {

// The control changes of the synthetic workload: CC 74 of channel 0 across
// the switch of its gain functions.
std::vector<AudScriptControlChange> synthetic_events() {
  std::vector<AudScriptControlChange> events;
  const uint32_t frames[] = {0, 4000, 30000, 30001, 61234, 90000, 100500};
  const uint8_t values[] = {20, 90, 64, 65, 127, 0, 100};
  for (size_t i = 0; i < 7; ++i) {
    AudScriptControlChange event{};
    event.frame = frames[i];
    event.channel = 0;
    event.number = 74;
    event.value = values[i];
    events.push_back(event);
  }
  return events;
}

// Renders the synthetic workload offline on a new graph.
std::vector<Record> renderSynthetic(uint32_t blocks, bool stress = false) {
  Graph g(kSafeFrames, kSafeRate);
  g.play(kSafeTempo);
  AUD_CHECK(g.load(syntheticScript(stress)) == AUD_OK);
  for (const AudScriptControlChange& event : synthetic_events()) {
    g.controlChange(event.frame, event.channel, event.number, event.value);
  }
  g.renderOffline(blocks);
  return Recorder::get().records;
}

}  // namespace

AUD_TEST(offline_renders_are_deterministic_and_never_late) {
  AUD_CHECK(!syntheticScript().empty());
  aud_audio_script_reset_stats();
  const std::vector<Record> first = renderSynthetic(400);
  const std::vector<Record> second = renderSynthetic(400);
  AUD_CHECK(first.size() > 400);
  AUD_CHECK(first == second);
  const AudScriptStats s = stats();
  AUD_CHECK(s.late_ticks == 0);
  AUD_CHECK(s.skipped_requests == 0);
  // The worker may also run the request the last block issued.
  AUD_CHECK(s.ticks >= 2 * 399 && s.ticks <= 2 * 400);
  AUD_CHECK(s.aborted_ticks == 0);
}

AUD_TEST(the_emissions_of_the_synthetic_workload_equal_the_simulation) {
  for (const bool stress : {false, true}) {
    const uint32_t blocks = 500;
    const std::vector<Record> records = renderSynthetic(blocks, stress);
    AudScriptSimulation* simulation =
        simulate(syntheticScript(stress), kSafeRate, kSafeFrames, kSafeTempo,
                 blocks, synthetic_events());
    AUD_CHECK(simulation != nullptr && simulation->load_result == AUD_OK);
    if (simulation == nullptr) continue;
    // The changes the node must emit: per tick the last value of each pair,
    // when it differs from the value emitted before, at the last frame.
    std::map<std::pair<uint32_t, uint32_t>, float> emitted;
    std::vector<Record> wanted;
    for (uint32_t t = 0; t < simulation->num_ticks; ++t) {
      const AudScriptSimulationTick& tick = simulation->ticks[t];
      AUD_CHECK(tick.status == AUD_SCRIPT_TICK_OK);
      AUD_CHECK(tick.num_sends == 12);
      std::map<std::pair<uint32_t, uint32_t>, float> last;
      std::vector<std::pair<uint32_t, uint32_t>> order;
      for (uint32_t s = 0; s < tick.num_sends; ++s) {
        const AudScriptSimulationSend& send =
            simulation->sends[tick.first_send + s];
        const auto pair = std::make_pair(send.output, send.id);
        if (last.find(pair) == last.end()) order.push_back(pair);
        last[pair] = static_cast<float>(send.value);
      }
      for (const auto& pair : order) {
        const auto found = emitted.find(pair);
        if (found != emitted.end() && found->second == last[pair]) continue;
        emitted[pair] = last[pair];
        Record record;
        record.block = tick.tick;
        record.offset = kSafeFrames - 1;
        record.port = pair.first;
        record.id = pair.second;
        record.value = last[pair];
        wanted.push_back(record);
      }
    }
    AUD_CHECK(wanted.size() > 1000);
    AUD_CHECK(records == wanted);
    aud_audio_script_simulation_free(simulation);
  }
}

// ############################################################################
// JavaScriptCore

AUD_TEST(runs_on_javascriptcore_on_apple_platforms) {
  aud_audio_script_reset_stats();
  Graph g(256);
  const int32_t result = aud_graph_set_string(
      g.graph, g.modulator, AUD_SCRIPT_KEY_ENGINE, "javascriptcore");
#if __APPLE__
  AUD_CHECK(result == AUD_OK);
  AUD_CHECK(g.load("aud.onTick = function () {\n"
                   "  aud.send(0, 300, aud.time.tick / 1000);\n"
                   "  console.log('jsc', aud.time.tick);\n"
                   "};\n") == AUD_OK);
  g.renderOffline(4);
  std::vector<float> values;
  for (const Record& record : Recorder::get().records)
    values.push_back(record.value);
  AUD_CHECK((values == std::vector<float>{0.001f, 0.002f, 0.003f}));
  AudScriptStats s = stats();
  AUD_CHECK(s.engine == AUD_SCRIPT_ENGINE_JAVASCRIPTCORE);
  AUD_CHECK(s.memory_bytes == 0);
  // Back to QuickJS-ng: the engine setting loads the script again.
  const int32_t id = aud_graph_set_string(g.graph, g.modulator,
                                          AUD_SCRIPT_KEY_ENGINE, "quickjs");
  AUD_CHECK(id > 0);
  AUD_CHECK(aud_graph_wait_loads(g.graph, g.modulator, 20000) == AUD_OK);
  g.renderOffline(2);
  s = stats();
  AUD_CHECK(s.engine == AUD_SCRIPT_ENGINE_QUICKJS && s.loads == 2);
  // A failing script on JavaScriptCore keeps the old one too.
  aud_graph_set_string(g.graph, g.modulator, AUD_SCRIPT_KEY_ENGINE,
                       "javascriptcore");
  AUD_CHECK(aud_graph_wait_loads(g.graph, g.modulator, 20000) == AUD_OK);
  AUD_CHECK(g.load("aud.onTick = function ( {") == AUD_ERROR_FAILED);
  AUD_CHECK(g.lastMessage.find("SyntaxError") != std::string::npos);
#else
  AUD_CHECK(result == AUD_ERROR_UNSUPPORTED);
#endif
}

AUD_TEST(javascriptcore_simulates_the_synthetic_workload_like_quickjs) {
#if __APPLE__
  AudScriptSimulation* quickjs =
      simulate(syntheticScript(), kSafeRate, kSafeFrames, kSafeTempo, 300,
               synthetic_events());
  AudScriptSimulation* jsc =
      simulate(syntheticScript(), kSafeRate, kSafeFrames, kSafeTempo, 300,
               synthetic_events(), AUD_SCRIPT_ENGINE_JAVASCRIPTCORE);
  AUD_CHECK(quickjs != nullptr && jsc != nullptr);
  if (quickjs == nullptr || jsc == nullptr) return;
  AUD_CHECK(quickjs->num_sends == jsc->num_sends && jsc->num_sends > 3000);
  double largest = 0;
  for (uint32_t i = 0; i < quickjs->num_sends && i < jsc->num_sends; ++i) {
    AUD_CHECK(quickjs->sends[i].output == jsc->sends[i].output);
    AUD_CHECK(quickjs->sends[i].id == jsc->sends[i].id);
    largest = std::max(
        largest, std::fabs(quickjs->sends[i].value - jsc->sends[i].value));
  }
  // The engines may round sin, cos and pow differently in the last bits.
  AUD_CHECK(largest < 1e-12);
  aud_audio_script_simulation_free(quickjs);
  aud_audio_script_simulation_free(jsc);
#endif
}

// ############################################################################
// Several modulators

AUD_TEST(modulators_run_side_by_side_with_a_bench) {
  aud_audio_script_reset_stats();
  Graph g(64);
  const int32_t other =
      aud_graph_create_node(g.graph, AUD_SCRIPT_MODULATOR_TYPE_ID, nullptr);
  AUD_CHECK(other > 0);
  AUD_CHECK(aud_graph_begin(g.graph) == AUD_OK);
  AUD_CHECK(aud_graph_commit(g.graph) > 0);
  AUD_CHECK(g.load(syntheticScript()) == AUD_OK);
  AUD_CHECK(aud_graph_set_string(g.graph, other, AUD_SCRIPT_KEY_SCRIPT,
                                 syntheticScript(true).c_str()) > 0);
  AUD_CHECK(aud_graph_wait_loads(g.graph, 0, 20000) == AUD_OK);
  std::thread bench([] {
    const std::string source = syntheticScript();
    AudScriptBenchRequest request{};
    request.struct_size = sizeof(AudScriptBenchRequest);
    request.source = source.c_str();
    request.sample_rate = 48000;
    request.tempo = 120;
    request.frames = 64;
    request.ticks = 50;
    request.flags = AUD_SCRIPT_BENCH_UNPACED;
    AudScriptBenchResult result{};
    result.struct_size = sizeof(AudScriptBenchResult);
    AUD_CHECK(aud_audio_script_bench_run(&request, &result) == AUD_OK);
  });
  for (int i = 0; i < 20; ++i) {
    g.renderLive(10);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  bench.join();
  AUD_CHECK(stats().ticks > 0);
}

// ############################################################################
// RealtimeSanitizer

namespace {

// aud.test.probe: allocates on the audio thread when AUD_TEST_RTSAN_PROBE
// is set; scripts/test-native.js checks that the rtsan run catches it, so
// that the run's silence about the modulator means something.
const bool g_probe = std::getenv("AUD_TEST_RTSAN_PROBE") != nullptr;
char* volatile g_probeSink = nullptr;

void* probeCreate(const AudNodeDescriptor*, const AudHostApi*) {
  return new int(0);
}
void probeDestroy(void* instance) { delete static_cast<int*>(instance); }
int32_t probePrepare(void*, const AudPrepareInfo*) { return AUD_OK; }
void probeReset(void*, uint32_t) {}
void probeSetParam(void*, uint32_t, float) {}
void probeProcess(void*, const AudProcessContext*) {
  if (g_probe) {
    g_probeSink = new char[16];
    delete[] g_probeSink;
  }
}

const AudNodeVTable kProbeVTable = {
    sizeof(AudNodeVTable),
    probeCreate,
    probeDestroy,
    probePrepare,
    probeReset,
    probeSetParam,
    probeProcess,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};

const AudNodeDescriptor kProbeDescriptor = [] {
  AudNodeDescriptor d = {};
  d.struct_size = sizeof(AudNodeDescriptor);
  d.abi_major = AUD_ABI_VERSION_MAJOR;
  d.abi_minor = AUD_ABI_VERSION_MINOR;
  d.version = 1;
  d.type_id = "aud.test.probe";
  d.name = "Probe";
  d.vendor = "Test";
  d.capabilities = AUD_NODE_CAP_VARIABLE_BLOCK;
  d.vtable = &kProbeVTable;
  return d;
}();

}  // namespace

AUD_TEST(rtsan_probe) {
  Graph g(128);
  const AudHostApi* host = g.host();
  AUD_CHECK(host->register_node_type(host->host, &kProbeDescriptor) == AUD_OK);
  const int32_t probe =
      aud_graph_create_node(g.graph, "aud.test.probe", nullptr);
  AUD_CHECK(probe > 0);
  AUD_CHECK(aud_graph_begin(g.graph) == AUD_OK);
  AUD_CHECK(aud_graph_commit(g.graph) > 0);
  AUD_CHECK(g.load(syntheticScript()) == AUD_OK);
  g.renderLive(20);
  g.renderOffline(20);
}
