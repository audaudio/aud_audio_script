// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// The native tests of the parts of the modulator (ticket 31): the tick
// grid, the histogram, the values and their undo, the two rings, the fast
// cores and the placement, the exports of the statistics, the simulation
// and the bench, and the node through its bare vtable with a host of the
// tests - the order of the emitted events, MIDI 2.0 and numeric controls,
// the loads it reports, the settings it refuses.

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "aud_script_engine.hpp"
#include "aud_script_fixture.hpp"
#include "aud_script_node.hpp"
#include "aud_script_placement.hpp"
#include "aud_script_rings.hpp"
#include "aud_script_runner.hpp"
#include "aud_script_stats.hpp"
#include "aud_transport.h"

#if __APPLE__
#include <sys/qos.h>
#endif

using namespace aud_script;
using namespace aud_script_test;

// ############################################################################
// The tick grid

AUD_TEST(computes_the_grid_point_of_a_block) {
  AudScriptBlock block;
  block.frames = 256;
  block.sampleRate = 48000;
  block.tempo = 120;
  block.playing = true;
  // 256 frames at 120 BPM are 0.0107 beats: 94 grid points per beat.
  block.b0 = 0;
  AudScriptGrid grid = audScriptGrid(block);
  AUD_CHECK(grid.beat ==
            std::floor((0 + 256.0 * 120 / 60 / 48000) / (1.0 / 94)) *
                (1.0 / 94));
  AUD_CHECK(grid.offset == 255);
  // A grid point that falls on the first frame of the next block wraps to
  // 0: here exactly at beat 4.
  block.b0 = 374 * (256.0 * 120 / 60 / 48000);
  grid = audScriptGrid(block);
  AUD_CHECK(grid.offset < 256);
  // A standing transport: the beat of the block at frame 0.
  block.playing = false;
  block.b0 = 7.25;
  grid = audScriptGrid(block);
  AUD_CHECK(grid.beat == 7.25 && grid.offset == 0);
  // Blocks longer than a beat: one grid point per beat.
  block.playing = true;
  block.b0 = 0.5;
  block.frames = 48000;
  grid = audScriptGrid(block);
  AUD_CHECK(grid.beat == 2.0);
  AUD_CHECK(grid.offset == 36000);
  // No frames or no tempo: the beat of the block.
  block.frames = 0;
  AUD_CHECK(audScriptGrid(block).beat == 0.5);
  block.frames = 256;
  block.tempo = 0;
  AUD_CHECK(audScriptGrid(block).offset == 0);
}

// ############################################################################
// The histogram

AUD_TEST(the_histogram_tells_its_percentiles) {
  AudScriptHistogram histogram;
  AUD_CHECK(histogram.percentile(0.5) == 0);
  for (int i = 1; i <= 100; ++i) histogram.add(i);
  AUD_CHECK(histogram.count() == 100);
  AUD_CHECK_NEAR(histogram.percentile(0.5), 50, 50 * 0.023);
  AUD_CHECK_NEAR(histogram.percentile(0.99), 99, 99 * 0.023);
  histogram.add(0.01);
  histogram.add(1e12);
  AUD_CHECK(AudScriptHistogram::binOf(0.01) == 0);
  AUD_CHECK(AudScriptHistogram::binOf(1e12) == AudScriptHistogram::kBins - 1);
  AUD_CHECK(histogram.percentile(1.0) > 1e8);
  histogram.reset();
  AUD_CHECK(histogram.count() == 0);
}

// ############################################################################
// The values

AUD_TEST(the_values_undo_an_aborted_tick) {
  AudScriptValues values;
  values.begin();
  AUD_CHECK(values.set(0, 1, 0.5f) == nullptr);
  AUD_CHECK(values.set(15, 511, 0.25f) == nullptr);
  values.commit();
  values.begin();
  AUD_CHECK(values.set(0, 1, 0.75f) == nullptr);
  AUD_CHECK(values.set(3, 3, 1.0f) == nullptr);
  AUD_CHECK(values.set(3, 3, 0.0f) == nullptr);
  AUD_CHECK(values.size() == 3);
  values.rollback();
  AUD_CHECK(values.size() == 2);
  AUD_CHECK(values.outputAt(0) == 0 && values.idAt(0) == 1 &&
            values.valueAt(0) == 0.5f);
  AUD_CHECK(values.outputAt(1) == 15 && values.idAt(1) == 511);
  // Full: a new pair is refused, a known one is taken.
  values.begin();
  for (uint32_t i = 0; values.size() < AUD_SCRIPT_MAX_VALUES; ++i) {
    AUD_CHECK(values.set(i / 512 + 4, i % 512, 1.0f) == nullptr);
  }
  AUD_CHECK(values.set(1, 1, 1.0f) != nullptr);
  AUD_CHECK(values.set(0, 1, 1.0f) == nullptr);
  values.rollback();
  AUD_CHECK(values.size() == 2 && values.valueAt(0) == 0.5f);
  // The pair is free again after the undo.
  AUD_CHECK(values.set(4, 0, 0.5f) == nullptr);
}

AUD_TEST(checks_the_arguments_of_a_send) {
  AUD_CHECK(audScriptCheckSend(0, 0, 0) == nullptr);
  AUD_CHECK(audScriptCheckSend(15, 511, -1e30) == nullptr);
  AUD_CHECK(audScriptCheckSend(16, 0, 0) != nullptr);
  AUD_CHECK(audScriptCheckSend(NAN, 0, 0) != nullptr);
  AUD_CHECK(audScriptCheckSend(0, 512, 0) != nullptr);
  AUD_CHECK(audScriptCheckSend(0, 2.5, 0) != nullptr);
  AUD_CHECK(audScriptCheckSend(0, 0, INFINITY) != nullptr);
}

// ############################################################################
// The rings

AUD_TEST(the_batch_ring_publishes_whole_batches) {
  AudScriptBatchRing<int> ring(3);
  AUD_CHECK(ring.capacity() == 4);
  AUD_CHECK(ring.room() == 4);
  AUD_CHECK(ring.stage(1) && ring.stage(2));
  int item = 0;
  AUD_CHECK(!ring.pop(item));
  ring.publish();
  AUD_CHECK(ring.stage(3) && ring.stage(4));
  AUD_CHECK(!ring.stage(5));
  AUD_CHECK(ring.pop(item) && item == 1);
  AUD_CHECK(ring.pop(item) && item == 2);
  AUD_CHECK(!ring.pop(item));
  ring.publish();
  AUD_CHECK(ring.pop(item) && item == 3);
  AUD_CHECK(ring.room() == 3);
}

AUD_TEST(the_slot_ring_hands_out_its_slots_in_order) {
  AudScriptSlotRing<int, 2> ring;
  AUD_CHECK(ring.size() == 0);
  *ring.writeSlot() = 1;
  ring.commit();
  *ring.writeSlot() = 2;
  ring.commit();
  AUD_CHECK(ring.writeSlot() == nullptr);
  AUD_CHECK(ring.size() == 2 && *ring.at(0) == 1 && *ring.at(1) == 2);
  ring.pop();
  AUD_CHECK(ring.size() == 1 && *ring.at(0) == 2);
  AUD_CHECK(ring.writeSlot() != nullptr);
  ring.pop();
  AUD_CHECK(ring.size() == 0);
}

// ############################################################################
// Placement

AUD_TEST(finds_the_fast_cores_of_a_device) {
  // Two clusters: the big one.
  AUD_CHECK(audScriptFastCoresOf({1700000, 1700000, 2350000, 2350000}) == 0xC);
  // Three clusters: the big and the prime cores.
  AUD_CHECK(audScriptFastCoresOf(
                {1800000, 1800000, 2400000, 2400000, 3000000}) == 0x1C);
  // One cluster has none; an offline core is none.
  AUD_CHECK(audScriptFastCoresOf({2000000, 2000000}) == 0);
  AUD_CHECK(audScriptFastCoresOf({0, 1000000, 2000000}) == 0x4);
  AUD_CHECK(audScriptFastCoresOf({}) == 0);
}

AUD_TEST(places_a_worker_and_keeps_it_there) {
  for (const bool fast : {false, true}) {
    AudScriptPlacement placement;
    int32_t cpu = -2;
    std::thread thread([&placement, &cpu, fast] {
      placement = audScriptPlaceCurrentThread(fast);
      AUD_CHECK(audScriptRefreshPlacement(placement));
      cpu = audScriptCurrentCpu();
    });
    thread.join();
    AUD_CHECK(placement.fastCores == fast);
#if __APPLE__
    // Apple platforms keep the quality of service class either way.
    AUD_CHECK(placement.mode == AUD_SCRIPT_PLACEMENT_QOS);
    AUD_CHECK(placement.qos == QOS_CLASS_USER_INTERACTIVE);
    AUD_CHECK(placement.cpus == 0);
#else
    if (!fast) {
      AUD_CHECK(placement.mode == AUD_SCRIPT_PLACEMENT_ANY_CORE);
      AUD_CHECK(placement.cpus == 0);
    } else {
      AUD_CHECK(placement.mode != AUD_SCRIPT_PLACEMENT_ANY_CORE &&
                placement.mode != AUD_SCRIPT_PLACEMENT_QOS);
    }
#endif
    AUD_CHECK(cpu >= -1);
  }
}

// ############################################################################
// The statistics

AUD_TEST(the_exports_of_the_statistics_check_their_arguments) {
  aud_audio_script_reset_stats();
  AUD_CHECK(aud_audio_script_stats(nullptr) == AUD_ERROR_INVALID_ARGUMENT);
  AudScriptStats s{};
  s.struct_size = 8;
  AUD_CHECK(aud_audio_script_stats(&s) == AUD_ERROR_INVALID_ARGUMENT);
  AudScriptGlobalStats::get().setError("first");
  AudScriptGlobalStats::get().setError(std::string(5000, 'e'));
  size_t size = 0;
  AUD_CHECK(aud_audio_script_last_error(nullptr, 0, nullptr) ==
            AUD_ERROR_INVALID_ARGUMENT);
  AUD_CHECK(aud_audio_script_last_error(nullptr, 0, &size) ==
            AUD_ERROR_BUFFER_TOO_SMALL);
  AUD_CHECK(size == AudScriptGlobalStats::kMaxText);
  char small[4];
  AUD_CHECK(aud_audio_script_last_error(small, sizeof(small), &size) ==
            AUD_ERROR_BUFFER_TOO_SMALL);
  for (int i = 0; i < 70; ++i) {
    AudScriptGlobalStats::get().addConsoleLine("line " + std::to_string(i));
  }
  std::string console(4096, '\0');
  AUD_CHECK(aud_audio_script_console(&console[0], console.size(), &size) ==
            AUD_OK);
  console.resize(size);
  AUD_CHECK(console.find("line 5\n") == std::string::npos);
  AUD_CHECK(console.find("line 6\n") == 0);
  AUD_CHECK(console.find("line 69\n") != std::string::npos);
  // A UTF-8 sequence is never cut.
  AUD_CHECK(audScriptClip("ab\xC3\xA4", 3) == "ab");
  aud_audio_script_reset_stats();
  AUD_CHECK(lastError().empty());
  AUD_CHECK(stats().ticks == 0 && stats().tick_time_mean_us == 0);
}

namespace {

// A sink that takes every send and drops the console.
struct NullSink : AudScriptSink {
  const char* send(double, double, double) override { return nullptr; }
  void log(const std::string&) override {}
};

}  // namespace

AUD_TEST(counts_the_memory_of_a_quickjs_context_as_it_allocates) {
  NullSink sink;
  std::string error;
  std::unique_ptr<AudScriptContext> context =
      audScriptCreateQuickJs(4u * 1024u * 1024u, &sink, &error);
  AUD_CHECK(context != nullptr);
  if (context == nullptr) return;
  const uint64_t empty = context->memoryUsed();
  AUD_CHECK(empty > 10000);
  AUD_CHECK(context->evaluate("var keep = [];\n"
                              "for (var i = 0; i < 10000; ++i) keep.push([i]);",
                              "grow.js", 0, &error) == AUD_SCRIPT_TICK_OK);
  const uint64_t grown = context->memoryUsed();
  AUD_CHECK(grown > empty + 200000);
  // QuickJS-ng counts references: the arrays go at once.
  AUD_CHECK(context->evaluate("keep = null;", "shrink.js", 0, &error) ==
            AUD_SCRIPT_TICK_OK);
  AUD_CHECK(context->memoryUsed() < grown - 200000);
  // The limit holds with the counting allocator.
  AUD_CHECK(context->evaluate("var big = [];\n"
                              "for (;;) big.push({ s: 'x' + big.length });",
                              "limit.js", 0, &error) == AUD_SCRIPT_TICK_FAILED);
  AUD_CHECK(error.find("out of memory") != std::string::npos);
  AUD_CHECK(context->memoryUsed() < 5u * 1024u * 1024u);
}

AUD_TEST(creates_its_contexts_on_quickjs) {
  NullSink sink;
  std::string error;
  std::unique_ptr<AudScriptContext> context =
      audScriptCreateContext(1u << 20, &sink, &error);
  AUD_CHECK(context != nullptr && context->memoryUsed() > 0);
  AUD_CHECK(context->evaluate("var version = 1;", "probe.js", 0, &error) ==
            AUD_SCRIPT_TICK_OK);
}

// ############################################################################
// The simulation

AUD_TEST(the_simulation_checks_its_arguments) {
  AudScriptSimulation* result = nullptr;
  AudScriptSimulationRequest request{};
  request.struct_size = sizeof(AudScriptSimulationRequest);
  request.source = "";
  request.sample_rate = 48000;
  request.tempo = 120;
  request.frames = 256;
  request.blocks = 2;
  AUD_CHECK(aud_audio_script_simulate(&request, nullptr) ==
            AUD_ERROR_INVALID_ARGUMENT);
  AUD_CHECK(aud_audio_script_simulate(nullptr, &result) ==
            AUD_ERROR_INVALID_ARGUMENT);
  auto refused = [&](AudScriptSimulationRequest changed) {
    AudScriptSimulation* r = nullptr;
    const int32_t code = aud_audio_script_simulate(&changed, &r);
    return code == AUD_ERROR_INVALID_ARGUMENT && r == nullptr;
  };
  AudScriptSimulationRequest bad = request;
  bad.struct_size = 8;
  AUD_CHECK(refused(bad));
  bad = request;
  bad.source = nullptr;
  AUD_CHECK(refused(bad));
  bad = request;
  bad.frames = 0;
  AUD_CHECK(refused(bad));
  bad = request;
  bad.sample_rate = 1000;
  AUD_CHECK(refused(bad));
  bad = request;
  bad.tempo = 0;
  AUD_CHECK(refused(bad));
  bad = request;
  bad.blocks = 1000001;
  AUD_CHECK(refused(bad));
  bad = request;
  bad.num_events = 1;
  AUD_CHECK(refused(bad));
  AudScriptControlChange event{};
  event.channel = 16;
  bad.events = &event;
  AUD_CHECK(refused(bad));
  // Zero blocks run no tick.
  bad = request;
  bad.blocks = 0;
  AUD_CHECK(aud_audio_script_simulate(&bad, &result) == AUD_OK);
  AUD_CHECK(result != nullptr && result->num_ticks == 0);
  aud_audio_script_simulation_free(result);
  aud_audio_script_simulation_free(nullptr);
}

AUD_TEST(the_simulation_reports_a_failed_load) {
  AudScriptSimulationRequest request{};
  request.struct_size = sizeof(AudScriptSimulationRequest);
  request.source = "throw new TypeError('no');";
  request.name = "broken.js";
  request.sample_rate = 48000;
  request.tempo = 120;
  request.frames = 256;
  request.blocks = 10;
  AudScriptSimulation* result = nullptr;
  AUD_CHECK(aud_audio_script_simulate(&request, &result) == AUD_OK);
  AUD_CHECK(result != nullptr);
  if (result == nullptr) return;
  AUD_CHECK(result->load_result == AUD_ERROR_FAILED);
  AUD_CHECK(result->num_ticks == 0);
  AUD_CHECK(std::string(result->error).find("broken.js: TypeError: no") == 0);
  aud_audio_script_simulation_free(result);
}

AUD_TEST(the_simulation_delivers_events_a_lookahead_later) {
  const std::string script =
      "var seen = 0;\n"
      "aud.onControlChange = function () { ++seen; };\n"
      "aud.onTick = function () { aud.send(0, 0, seen); };\n";
  const std::vector<AudScriptControlChange> events = {{10, 0, 1, 1, 0},
                                                      {300, 0, 1, 1, 0}};
  for (const uint32_t lookahead : {0u, 1u, 3u}) {
    AudScriptSimulation* result =
        simulate(script, 48000, 128, 120, 8, events, lookahead);
    AUD_CHECK(result != nullptr);
    if (result == nullptr) continue;
    // Block 0 and block 2 hold the events.
    const uint32_t l = lookahead > 0 ? lookahead : 1;
    for (uint32_t t = 0; t < result->num_ticks; ++t) {
      const uint32_t tick = result->ticks[t].tick;
      const double wanted = (tick >= l ? 1 : 0) + (tick >= 2 + l ? 1 : 0);
      AUD_CHECK(result->sends[result->ticks[t].first_send].value == wanted);
    }
    aud_audio_script_simulation_free(result);
  }
  AudScriptSimulationRequest request{};
  request.struct_size = sizeof(AudScriptSimulationRequest);
  request.source = "";
  request.sample_rate = 48000;
  request.tempo = 120;
  request.frames = 128;
  request.lookahead = 5;
  AudScriptSimulation* refused = nullptr;
  AUD_CHECK(aud_audio_script_simulate(&request, &refused) ==
            AUD_ERROR_INVALID_ARGUMENT);
}

AUD_TEST(the_simulation_runs_the_ticks_of_a_script) {
  // The changes of a block reach the script before the tick of the next
  // one; the sends of the load come with the first tick; an aborted tick
  // reports no sends.
  const std::string script =
      "aud.send(9, 9, 0.5);\n"
      "var seen = [];\n"
      "aud.onControlChange = function (c, n, v) { seen.push(c + ':' + n + ':' "
      "+ v); };\n"
      "aud.onTick = function () {\n"
      "  aud.send(0, 1, seen.length);\n"
      "  if (seen.length) console.log(seen.join(','));\n"
      "  seen = [];\n"
      "  if (aud.time.tick === 4) { for (;;) {} }\n"
      "};\n";
  std::vector<AudScriptControlChange> events(3);
  events[0] = {300, 1, 2, 3, 0};
  events[1] = {100, 4, 5, 6, 0};
  events[2] = {100, 7, 8, 9, 0};
  AudScriptSimulationRequest request{};
  request.struct_size = sizeof(AudScriptSimulationRequest);
  request.source = script.c_str();
  request.sample_rate = 48000;
  request.tempo = 120;
  request.frames = 128;
  request.blocks = 6;
  request.tick_budget_ms = 20;
  request.num_events = 3;
  request.events = events.data();
  AudScriptSimulation* result = nullptr;
  AUD_CHECK(aud_audio_script_simulate(&request, &result) == AUD_OK);
  AUD_CHECK(result != nullptr && result->load_result == AUD_OK);
  if (result == nullptr) return;
  AUD_CHECK(result->num_ticks == 5);
  AUD_CHECK(result->context_create_us > 0 && result->load_us > 0);
  std::vector<std::vector<double>> sends;
  for (uint32_t t = 0; t < result->num_ticks; ++t) {
    const AudScriptSimulationTick& tick = result->ticks[t];
    AUD_CHECK(tick.tick == t + 1);
    AUD_CHECK(tick.beat ==
              audScriptGrid({tick.tick, tick.tick * 128.0 * 120 / 60 / 48000,
                             120, true, 48000, 128})
                  .beat);
    std::vector<double> values;
    for (uint32_t s = 0; s < tick.num_sends; ++s) {
      values.push_back(result->sends[tick.first_send + s].output * 100 +
                       result->sends[tick.first_send + s].value);
    }
    sends.push_back(values);
  }
  AUD_CHECK((sends[0] == std::vector<double>{900.5, 2}));
  AUD_CHECK((sends[1] == std::vector<double>{0}));
  AUD_CHECK((sends[2] == std::vector<double>{1}));
  AUD_CHECK(sends[3].empty());
  AUD_CHECK(result->ticks[3].status == AUD_SCRIPT_TICK_INTERRUPTED);
  AUD_CHECK((sends[4] == std::vector<double>{0}));
  AUD_CHECK(std::string(result->console) ==
            "script: 4:5:6,7:8:9\nscript: 1:2:3\n");
  AUD_CHECK(std::string(result->error).find("interrupted") !=
            std::string::npos);
  aud_audio_script_simulation_free(result);
}

// ############################################################################
// The bench

AUD_TEST(the_bench_times_the_ticks_of_a_script) {
  const std::string source = syntheticScript();
  AudScriptBenchRequest request{};
  request.struct_size = sizeof(AudScriptBenchRequest);
  request.source = source.c_str();
  request.sample_rate = 48000;
  request.tempo = 120;
  request.frames = 128;
  request.ticks = 40;
  request.warmup_ticks = 5;
  AudScriptBenchResult result{};
  result.struct_size = sizeof(AudScriptBenchResult);
  const auto start = std::chrono::steady_clock::now();
  AUD_CHECK(aud_audio_script_bench_run(&request, &result) == AUD_OK);
  const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
          .count();
  // Paced: 45 blocks of 2.67 ms.
  AUD_CHECK(seconds > 0.1);
  AUD_CHECK(result.ticks == 40);
  AUD_CHECK_NEAR(result.block_us, 128 / 48000.0 * 1e6, 1e-6);
  AUD_CHECK(result.mean_us > 0 && result.p50_us <= result.p99_us &&
            result.p99_us <= result.max_us);
  AUD_CHECK(result.context_create_us > 0 && result.load_us > 0);
  AUD_CHECK(result.memory_bytes > 0);
  AUD_CHECK(result.aborted == 0);
#if __APPLE__
  AUD_CHECK(result.placement == AUD_SCRIPT_PLACEMENT_QOS);
#endif
  // Unpaced, with a stress script and a budget that every tick misses.
  const std::string busy =
      "aud.onTick = function () { var s = Date.now(); while (Date.now() - s < "
      "200) {} };";
  request.source = busy.c_str();
  request.frames = 16;
  request.flags = AUD_SCRIPT_BENCH_UNPACED;
  request.ticks = 5;
  request.warmup_ticks = 0;
  request.tick_budget_ms = 5;
  AUD_CHECK(aud_audio_script_bench_run(&request, &result) == AUD_OK);
  AUD_CHECK(result.aborted == 5 && result.over_block == 5);
}

AUD_TEST(the_bench_checks_its_arguments) {
  AudScriptBenchRequest request{};
  request.struct_size = sizeof(AudScriptBenchRequest);
  request.source = "";
  request.sample_rate = 48000;
  request.tempo = 120;
  request.frames = 128;
  request.ticks = 1;
  AudScriptBenchResult result{};
  result.struct_size = sizeof(AudScriptBenchResult);
  AUD_CHECK(aud_audio_script_bench_run(nullptr, &result) ==
            AUD_ERROR_INVALID_ARGUMENT);
  AUD_CHECK(aud_audio_script_bench_run(&request, nullptr) ==
            AUD_ERROR_INVALID_ARGUMENT);
  auto refused = [&](AudScriptBenchRequest changed) {
    return aud_audio_script_bench_run(&changed, &result) ==
           AUD_ERROR_INVALID_ARGUMENT;
  };
  AudScriptBenchRequest bad = request;
  bad.struct_size = 8;
  AUD_CHECK(refused(bad));
  bad = request;
  bad.ticks = 0;
  AUD_CHECK(refused(bad));
  bad = request;
  bad.source = nullptr;
  AUD_CHECK(refused(bad));
  bad = request;
  bad.sample_rate = 500000;
  AUD_CHECK(refused(bad));
  bad = request;
  bad.tempo = 1000;
  AUD_CHECK(refused(bad));
  AudScriptBenchResult shortResult{};
  shortResult.struct_size = 8;
  AUD_CHECK(aud_audio_script_bench_run(&request, &shortResult) ==
            AUD_ERROR_INVALID_ARGUMENT);
  aud_audio_script_reset_stats();
  bad = request;
  bad.source = "syntax error here";
  bad.name = "bench.js";
  AUD_CHECK(aud_audio_script_bench_run(&bad, &result) == AUD_ERROR_FAILED);
  AUD_CHECK(std::string(result.error).find("bench.js: SyntaxError") == 0);
  // The bench counts nothing into the statistics of the modulators.
  AUD_CHECK(lastError().empty());
}

// ############################################################################
// The node through its bare vtable

namespace {

// A host of the tests: records the emitted events and the loads.
struct TestHost {
  AudHostApi api{};
  std::vector<AudEvent> events;
  std::mutex mutex;
  int32_t nextLoad = 1;
  std::map<int32_t, std::pair<int32_t, std::string>> ended;
  std::atomic<int32_t> cancelled{0};

  TestHost() {
    events.reserve(4096);
    api.struct_size = sizeof(AudHostApi);
    api.abi_major = AUD_ABI_VERSION_MAJOR;
    api.abi_minor = AUD_ABI_VERSION_MINOR;
    api.host = this;
    api.emit_event = [](void* host, void*, const AudEvent* event) {
      static_cast<TestHost*>(host)->events.push_back(*event);
    };
    api.begin_load = [](void* host, void*, const char*) {
      auto* self = static_cast<TestHost*>(host);
      std::lock_guard<std::mutex> lock(self->mutex);
      return self->nextLoad++;
    };
    api.end_load = [](void* host, int32_t id, int32_t result,
                      const char* message) {
      auto* self = static_cast<TestHost*>(host);
      std::lock_guard<std::mutex> lock(self->mutex);
      self->ended[id] = {result, message != nullptr ? message : ""};
    };
    api.load_cancelled = [](void* host, int32_t id) {
      return static_cast<TestHost*>(host)->cancelled.load() == id ? 1 : 0;
    };
  }

  // The result of a load once it ended; AUD_PENDING until then.
  int32_t waitEnd(int32_t id) {
    for (int i = 0; i < 20000; ++i) {
      {
        std::lock_guard<std::mutex> lock(mutex);
        const auto found = ended.find(id);
        if (found != ended.end()) return found->second.first;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return AUD_PENDING;
  }
};

// A node of the package driven through its vtable.
struct Bare {
  const AudNodeDescriptor& type = audScriptModulatorDescriptor();
  void* node = nullptr;
  int64_t position = 0;
  explicit Bare(const AudHostApi* host) {
    node = type.vtable->create(&type, host);
    AUD_CHECK(node != nullptr);
    AudPrepareInfo info{};
    info.struct_size = sizeof(AudPrepareInfo);
    info.sample_rate = 48000;
    info.max_frames = 512;
    AUD_CHECK(type.vtable->prepare(node, &info) == AUD_OK);
  }
  ~Bare() { type.vtable->destroy(node); }
  Bare(const Bare&) = delete;
  Bare& operator=(const Bare&) = delete;

  int32_t set(uint32_t key, const char* value) {
    return type.vtable->set_string(node, key, value);
  }

  void process(uint32_t frames, uint32_t flags,
               const std::vector<AudEvent>& events = {},
               const AudTransportSnapshot* transport = nullptr) {
    AudProcessContext context{};
    context.struct_size = sizeof(AudProcessContext);
    context.frames = frames;
    context.sample_rate = 48000;
    context.sample_position = position;
    context.flags = flags;
    context.num_events = static_cast<uint32_t>(events.size());
    context.events = events.empty() ? nullptr : events.data();
    context.transport = transport;
    type.vtable->process(node, &context);
    position += frames;
  }
};

}  // namespace

AUD_TEST(emits_steps_and_ramps_of_several_outputs_in_ascending_offsets) {
  TestHost host;
  Bare bare(&host.api);
  bare.type.vtable->set_param(bare.node, AUD_SCRIPT_PARAM_MODE0 + 1, 1);
  const int32_t id = bare.set(AUD_SCRIPT_KEY_SCRIPT,
                              "aud.onTick = function () {\n"
                              "  aud.send(0, 1, aud.time.tick);\n"
                              "  aud.send(1, 2, aud.time.tick);\n"
                              "  aud.send(1, 3, 7);\n"
                              "};\n");
  AUD_CHECK(id > 0);
  AUD_CHECK(host.waitEnd(id) == AUD_OK);
  // A transport at 120 BPM from beat 0.
  AudTransportSegment segment{};
  segment.struct_size = sizeof(AudTransportSegment);
  segment.tempo = 120;
  segment.flags = AUD_SEGMENT_PLAYING;
  AudTransportSnapshot snapshot{};
  snapshot.struct_size = sizeof(AudTransportSnapshot);
  snapshot.num_segments = 1;
  snapshot.segments = &segment;
  snapshot.time.sample_rate = 48000;
  for (int block = 0; block < 3; ++block) {
    segment.beat = aud_ticks_from_beats(block * 256.0 * 120 / 60 / 48000);
    bare.process(256, AUD_PROCESS_OFFLINE, {}, &snapshot);
  }
  // Block 2: the step of output 1 at k = 254 (one frame before it the old
  // value), the ramp of output 0 at frame 255; the constant 7 holds.
  const AudScriptGrid grid =
      audScriptGrid({2, 2 * 256.0 * 120 / 60 / 48000, 120, true, 48000, 256});
  std::vector<std::tuple<uint32_t, uint32_t, uint32_t, float>> got;
  for (size_t i = 3; i < host.events.size(); ++i) {
    const AudEvent& event = host.events[i];
    float value = 0;
    std::memcpy(&value, &event.words[2], sizeof(value));
    got.emplace_back(event.sample_offset, event.port, event.words[0], value);
  }
  const std::vector<std::tuple<uint32_t, uint32_t, uint32_t, float>> wanted = {
      {grid.offset - 1, 1, 2, 1.0f},
      {grid.offset, 1, 2, 2.0f},
      {255, 0, 1, 2.0f},
  };
  AUD_CHECK(grid.offset > 1);
  AUD_CHECK(host.events.size() == 6);
  AUD_CHECK(got == wanted);
}

AUD_TEST(takes_midi_2_and_numeric_controls_and_parameter_events) {
  TestHost host;
  Bare bare(&host.api);
  const int32_t id = bare.set(
      AUD_SCRIPT_KEY_SCRIPT,
      "var got = [];\n"
      "aud.onControlChange = function (c, n, v) { got.push(v); };\n"
      "aud.onControl = function (id, v) { got.push(id * 10 + v); };\n"
      "aud.onTick = function () {\n"
      "  for (var i = 0; i < got.length; ++i) aud.send(0, i, got[i]);\n"
      "  got = [];\n"
      "};\n");
  AUD_CHECK(host.waitEnd(id) == AUD_OK);
  uint32_t words[2] = {0, 0};
  words[0] = aud_ump_midi2_word0(0, AUD_UMP_STATUS_CONTROL_CHANGE, 3, 74, 0);
  words[1] = 0xFFFFFFFFu;
  const uint32_t note = aud_ump_midi1_word(0, 0x90, 60, 100);
  std::vector<AudEvent> events = {
      aud_event_ump(words, 2, 0, 0),
      aud_event_ump(&note, 1, 1, 0),
      AudEvent{sizeof(AudEvent),
               AUD_EVENT_CONTROL,
               2,
               0,
               0,
               {1, AUD_ARG_INT32, 5, 0}},
      AudEvent{sizeof(AudEvent),
               AUD_EVENT_CONTROL,
               3,
               0,
               0,
               {2, AUD_ARG_TRUE, 0, 0}},
      AudEvent{sizeof(AudEvent),
               AUD_EVENT_CONTROL,
               4,
               0,
               0,
               {3, AUD_ARG_FALSE, 0, 0}},
      AudEvent{sizeof(AudEvent),
               AUD_EVENT_CONTROL,
               5,
               0,
               0,
               {4, AUD_ARG_DOUBLE, 0, 0}},
      aud_event_param(AUD_SCRIPT_PARAM_MODE0, 1, 0, 6),
      aud_event_param(99, 1, 0, 6),
  };
  // The events of block 0 reach the tick of block 2, the lookahead.
  bare.process(64, AUD_PROCESS_OFFLINE, events);
  bare.process(64, AUD_PROCESS_OFFLINE);
  AUD_CHECK(host.events.empty());
  bare.process(64, AUD_PROCESS_OFFLINE);
  std::vector<float> values;
  for (const AudEvent& event : host.events) {
    float value = 0;
    std::memcpy(&value, &event.words[2], sizeof(value));
    values.push_back(value);
    // Output 0 steps now: without a transport the grid point is frame 0.
    AUD_CHECK(event.sample_offset == 0);
  }
  AUD_CHECK((values == std::vector<float>{127, 15, 21, 30}));
}

AUD_TEST(the_node_refuses_what_it_cannot_take) {
  TestHost host;
  {
    Bare bare(nullptr);
    AUD_CHECK(bare.set(99, "x") == AUD_ERROR_INVALID_ARGUMENT);
    // Nor is the key after the last one.
    AUD_CHECK(bare.set(AUD_SCRIPT_NUM_KEYS, "quickjs") ==
              AUD_ERROR_INVALID_ARGUMENT);
    AUD_CHECK(bare.set(AUD_SCRIPT_KEY_SCRIPT_NAME, nullptr) == AUD_OK);
    // Without a host to report it, a load returns AUD_OK.
    AUD_CHECK(bare.set(AUD_SCRIPT_KEY_SCRIPT, nullptr) == AUD_OK);
    AudPrepareInfo info{};
    info.struct_size = sizeof(AudPrepareInfo);
    info.sample_rate = 0;
    AUD_CHECK(bare.type.vtable->prepare(bare.node, &info) ==
              AUD_ERROR_INVALID_ARGUMENT);
    AUD_CHECK(bare.type.vtable->prepare(bare.node, nullptr) ==
              AUD_ERROR_INVALID_ARGUMENT);
    bare.type.vtable->reset(bare.node, AUD_RESET_STOP);
    bare.type.vtable->set_param(bare.node, 99, 1);
    bare.type.vtable->set_param(bare.node, 0, NAN);
    bare.process(64, 0);
    bare.process(64, AUD_PROCESS_OFFLINE);
    bare.process(0, 0);
  }
  // Loads that wait when the node goes end as cancelled; a load replaced by
  // a newer one, too; a load the engine cancelled ends so.
  {
    Bare bare(&host.api);
    host.cancelled = 2;
    const int32_t first = bare.set(AUD_SCRIPT_KEY_SCRIPT, "var a = 1;");
    const int32_t second = bare.set(AUD_SCRIPT_KEY_SCRIPT, "var a = 2;");
    AUD_CHECK(first == 1 && second == 2);
    const int32_t third = bare.set(AUD_SCRIPT_KEY_SCRIPT, "var a = 3;");
    AUD_CHECK(host.waitEnd(third) == AUD_OK);
    AUD_CHECK(host.waitEnd(second) == AUD_ERROR_CANCELLED);
    AUD_CHECK(host.waitEnd(first) == AUD_OK ||
              host.waitEnd(first) == AUD_ERROR_CANCELLED);
  }
}

int main() { return aud_test::run(); }
