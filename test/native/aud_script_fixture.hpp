// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// What the native tests of the modulator share: a graph of the engine with
// the package and a recorder registered, the recorder - a node of the
// tests that writes down every control event that reaches its 16 event
// inputs, without allocating -, the transport, offline and live renders,
// and the synthetic workload of the package, which scripts/test-native.js
// takes from lib/src/aud_script_bench.dart.

#ifndef AUD_SCRIPT_FIXTURE_HPP
#define AUD_SCRIPT_FIXTURE_HPP

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "aud_audio_graph.h"
#include "aud_audio_script.h"
#include "aud_test.hpp"
#include "aud_transport.h"
#include "aud_ump.h"

namespace aud_script_test {

// ............................................................................
// The recorder

// A control event as the recorder saw it.
struct Record {
  int64_t block = 0;    // the block, counted from the first
  uint32_t offset = 0;  // the frame inside the block
  uint32_t port = 0;    // the input, the output of the modulator
  uint32_t id = 0;
  float value = 0;
  bool operator==(const Record& o) const {
    return block == o.block && offset == o.offset && port == o.port &&
           id == o.id && value == o.value;
  }
};

// The records of the recorder that rendered last, and per block its frames
// and the beat of the transport at its first frame. The vectors are
// reserved before the render, so that the audio thread only writes into
// them.
struct Recorder {
  std::vector<Record> records;
  std::vector<double> beats;
  std::vector<uint32_t> frames;
  uint32_t blockFrames = 0;
  int64_t block = 0;
  uint64_t overflow = 0;
  static Recorder& get() {
    static Recorder recorder;
    return recorder;
  }
  void clear(uint32_t frames) {
    records.clear();
    records.reserve(1 << 20);
    beats.clear();
    beats.reserve(1 << 16);
    this->frames.clear();
    this->frames.reserve(1 << 16);
    blockFrames = frames;
    block = 0;
    overflow = 0;
  }
};

constexpr const char* kRecorderTypeId = "aud.test.recorder";

inline void* recorderCreate(const AudNodeDescriptor*, const AudHostApi*) {
  return &Recorder::get();
}
inline void recorderDestroy(void*) {}
inline int32_t recorderPrepare(void*, const AudPrepareInfo*) { return AUD_OK; }
inline void recorderReset(void*, uint32_t) {}
inline void recorderSetParam(void*, uint32_t, float) {}
inline void recorderProcess(void* instance, const AudProcessContext* context) {
  auto* recorder = static_cast<Recorder*>(instance);
  for (uint32_t i = 0; i < context->num_events; ++i) {
    const AudEvent& event = context->events[i];
    if (event.type != AUD_EVENT_CONTROL) continue;
    if (recorder->records.size() == recorder->records.capacity()) {
      recorder->overflow += 1;
      continue;
    }
    Record record;
    record.block = recorder->block;
    record.offset = event.sample_offset;
    record.port = event.port;
    record.id = event.words[0];
    std::memcpy(&record.value, &event.words[2], sizeof(float));
    recorder->records.push_back(record);
  }
  const AudTransportSnapshot* transport = context->transport;
  if (recorder->beats.size() < recorder->beats.capacity()) {
    recorder->beats.push_back(
        transport != nullptr && transport->num_segments > 0
            ? aud_beats_from_ticks(transport->segments[0].beat)
            : 0.0);
    recorder->frames.push_back(context->frames);
  }
  recorder->block += 1;
}

#define AUD_TEST_RECORDER_INPUT(n) \
  {sizeof(AudEventPortDescriptor), "in" #n, "In " #n, AUD_EVENT_PORT_CONTROL, 0}

inline const AudNodeDescriptor& recorderDescriptor() {
  static const AudEventPortDescriptor inputs[16] = {
      AUD_TEST_RECORDER_INPUT(0),  AUD_TEST_RECORDER_INPUT(1),
      AUD_TEST_RECORDER_INPUT(2),  AUD_TEST_RECORDER_INPUT(3),
      AUD_TEST_RECORDER_INPUT(4),  AUD_TEST_RECORDER_INPUT(5),
      AUD_TEST_RECORDER_INPUT(6),  AUD_TEST_RECORDER_INPUT(7),
      AUD_TEST_RECORDER_INPUT(8),  AUD_TEST_RECORDER_INPUT(9),
      AUD_TEST_RECORDER_INPUT(10), AUD_TEST_RECORDER_INPUT(11),
      AUD_TEST_RECORDER_INPUT(12), AUD_TEST_RECORDER_INPUT(13),
      AUD_TEST_RECORDER_INPUT(14), AUD_TEST_RECORDER_INPUT(15),
  };
  static const AudNodeVTable vtable = {
      sizeof(AudNodeVTable),
      recorderCreate,
      recorderDestroy,
      recorderPrepare,
      recorderReset,
      recorderSetParam,
      recorderProcess,
      nullptr,
      nullptr,
      nullptr,
      nullptr,
      nullptr,
  };
  static const AudNodeDescriptor descriptor = [] {
    AudNodeDescriptor d = {};
    d.struct_size = sizeof(AudNodeDescriptor);
    d.abi_major = AUD_ABI_VERSION_MAJOR;
    d.abi_minor = AUD_ABI_VERSION_MINOR;
    d.version = 1;
    d.type_id = kRecorderTypeId;
    d.name = "Recorder";
    d.vendor = "Test";
    d.capabilities = AUD_NODE_CAP_VARIABLE_BLOCK | AUD_NODE_CAP_EVENTS |
                     AUD_NODE_CAP_TRANSPORT;
    d.num_event_inputs = 16;
    d.event_inputs = inputs;
    d.vtable = &vtable;
    return d;
  }();
  return descriptor;
}

#undef AUD_TEST_RECORDER_INPUT

// ............................................................................
// Scripts

// The synthetic workload of the package; `stress` the tenfold one.
inline std::string syntheticScript(bool stress = false) {
  const char* path =
      std::getenv(stress ? "AUD_TEST_SYNTHETIC_STRESS" : "AUD_TEST_SYNTHETIC");
  if (path == nullptr) return std::string();
  std::ifstream file(path);
  std::stringstream text;
  text << file.rdbuf();
  return text.str();
}

// ............................................................................
// The graph

// A graph of the engine with no audio buses of its own beyond a stereo
// output, the package and the recorder registered, and a modulator whose
// outputs 0 to 15 lead to the inputs of the recorder. The watchdog counts
// what the audio thread must not do; every test ends without a violation.
struct Graph {
  AudGraph* graph = nullptr;
  uint32_t channels = 2;
  uint32_t frames;
  double rate;
  int32_t modulator = 0;
  int32_t recorder = 0;
  uint64_t violationsBefore = aud_graph_watchdog_violations();

  explicit Graph(uint32_t blockFrames = 256, double sampleRate = 48000,
                 bool connect = true)
      : frames(blockFrames), rate(sampleRate) {
    AudGraphConfig config{};
    config.struct_size = sizeof(AudGraphConfig);
    config.sample_rate = sampleRate;
    config.max_frames = blockFrames;
    config.num_output_buses = 1;
    config.output_channels = &channels;
    graph = aud_graph_create(&config);
    AUD_CHECK(graph != nullptr);
    AUD_CHECK(aud_audio_script_register(host()) == AUD_OK);
    AUD_CHECK(host()->register_node_type(host()->host, &recorderDescriptor()) ==
              AUD_OK);
    Recorder::get().clear(blockFrames);
    modulator =
        aud_graph_create_node(graph, AUD_SCRIPT_MODULATOR_TYPE_ID, nullptr);
    AUD_CHECK(modulator > 0);
    recorder = aud_graph_create_node(graph, kRecorderTypeId, nullptr);
    AUD_CHECK(recorder > 0);
    AUD_CHECK(aud_graph_begin(graph) == AUD_OK);
    if (connect) {
      for (uint32_t port = 0; port < AUD_SCRIPT_NUM_OUTPUTS; ++port) {
        AUD_CHECK(aud_graph_connect_events(graph, modulator, port, recorder,
                                           port) == AUD_OK);
      }
    }
    AUD_CHECK(aud_graph_commit(graph) > 0);
    AUD_CHECK(aud_graph_start(graph) == AUD_OK);
  }

  ~Graph() {
    AUD_CHECK(aud_graph_watchdog_violations() - violationsBefore == 0);
    aud_graph_destroy(graph);
  }

  Graph(const Graph&) = delete;
  Graph& operator=(const Graph&) = delete;

  const AudHostApi* host() const { return aud_graph_host_api(graph); }

  // Loads a script and waits until the worker built it; the load's result.
  int32_t load(const std::string& source) {
    const int32_t id = aud_graph_set_string(
        graph, modulator, AUD_SCRIPT_KEY_SCRIPT, source.c_str());
    AUD_CHECK(id > 0);
    AUD_CHECK(aud_graph_wait_loads(graph, modulator, 20000) == AUD_OK);
    AudLoadInfo info{};
    info.struct_size = sizeof(AudLoadInfo);
    AUD_CHECK(aud_graph_load_info(graph, id, &info) == AUD_OK);
    lastMessage = info.message;
    return info.result;
  }

  std::string lastMessage;

  void param(uint32_t index, float value) {
    AUD_CHECK(aud_graph_set_param(graph, modulator, index, value, 0) == AUD_OK);
  }

  void transport(uint32_t type, double value = 0) {
    AudTransportRequest request{};
    request.struct_size = sizeof(AudTransportRequest);
    request.type = type;
    request.at.struct_size = sizeof(AudTimestamp);
    request.value = value;
    AUD_CHECK(aud_graph_transport(graph, &request) == AUD_OK);
  }

  // Plays the transport from beat 0 at `tempo`.
  void play(double tempo) {
    transport(AUD_TRANSPORT_REQUEST_SET_TEMPO, tempo);
    transport(AUD_TRANSPORT_REQUEST_START);
  }

  // A MIDI 1.0 control change for the modulator at an absolute frame.
  void controlChange(int64_t frame, uint32_t channel, uint32_t number,
                     uint32_t value) {
    const uint32_t word = aud_ump_midi1_word(0, 0xB0u | channel, number, value);
    const AudEvent event = aud_event_ump(&word, 1, 0, 0);
    AudTimestamp at{};
    at.struct_size = sizeof(AudTimestamp);
    at.domain = AUD_TIME_SAMPLE;
    at.value = frame;
    AUD_CHECK(aud_graph_send_event(graph, modulator, &event, &at, 0) == AUD_OK);
  }

  // Renders `blocks` blocks offline.
  void renderOffline(uint32_t blocks) {
    const size_t total = static_cast<size_t>(blocks) * frames;
    std::vector<std::vector<float>> out(channels, std::vector<float>(total));
    std::vector<float*> pointers;
    for (auto& channel : out) pointers.push_back(channel.data());
    const AudAudioBus bus{sizeof(AudAudioBus), channels, pointers.data()};
    AudOfflineRequest request{};
    request.struct_size = sizeof(AudOfflineRequest);
    request.frames = static_cast<uint32_t>(total);
    request.block_frames = frames;
    request.outputs = &bus;
    request.start_sample_position = aud_graph_sample_position(graph);
    AUD_CHECK(aud_graph_render_offline(graph, &request) == AUD_OK);
  }

  // Renders `blocks` blocks offline in the cycle of `sizes`.
  void renderOfflineSizes(uint32_t blocks, const std::vector<uint32_t>& sizes) {
    size_t total = 0;
    for (uint32_t b = 0; b < blocks; ++b) total += sizes[b % sizes.size()];
    std::vector<std::vector<float>> out(channels, std::vector<float>(total));
    std::vector<float*> pointers;
    for (auto& channel : out) pointers.push_back(channel.data());
    const AudAudioBus bus{sizeof(AudAudioBus), channels, pointers.data()};
    AudOfflineRequest request{};
    request.struct_size = sizeof(AudOfflineRequest);
    request.frames = static_cast<uint32_t>(total);
    request.num_block_sizes = static_cast<uint32_t>(sizes.size());
    request.block_sizes = sizes.data();
    request.outputs = &bus;
    request.start_sample_position = aud_graph_sample_position(graph);
    AUD_CHECK(aud_graph_render_offline(graph, &request) == AUD_OK);
  }

  // Renders `blocks` blocks like a stream, back to back on the calling
  // thread, faster than real time.
  void renderLive(uint32_t blocks) {
    std::vector<std::vector<float>> out(channels, std::vector<float>(frames));
    std::vector<float*> pointers;
    for (auto& channel : out) pointers.push_back(channel.data());
    const AudAudioBus bus{sizeof(AudAudioBus), channels, pointers.data()};
    for (uint32_t b = 0; b < blocks; ++b) {
      AudRenderRequest request{};
      request.struct_size = sizeof(AudRenderRequest);
      request.frames = frames;
      request.num_output_buses = 1;
      request.outputs = &bus;
      AUD_CHECK(aud_graph_render(graph, &request) == AUD_OK);
    }
  }
};

// The statistics of the process.
inline AudScriptStats stats() {
  AudScriptStats s{};
  s.struct_size = sizeof(AudScriptStats);
  AUD_CHECK(aud_audio_script_stats(&s) == AUD_OK);
  return s;
}

// The last error of the process, read into room for the longest one: the
// worker may change it meanwhile.
inline std::string lastError() {
  size_t size = 0;
  std::string text(8192, '\0');
  AUD_CHECK(aud_audio_script_last_error(&text[0], text.size(), &size) ==
            AUD_OK);
  text.resize(size);
  return text;
}

// The records of a block.
inline std::vector<Record> recordsOf(int64_t block) {
  std::vector<Record> result;
  for (const Record& record : Recorder::get().records) {
    if (record.block == block) result.push_back(record);
  }
  return result;
}

// Runs `simulate` and returns its result; the caller frees it.
inline AudScriptSimulation* simulate(
    const std::string& source, double rate, uint32_t frames, double tempo,
    uint32_t blocks, const std::vector<AudScriptControlChange>& events = {},
    uint32_t lookahead = 1) {
  AudScriptSimulationRequest request{};
  request.struct_size = sizeof(AudScriptSimulationRequest);
  request.source = source.c_str();
  request.sample_rate = rate;
  request.tempo = tempo;
  request.frames = frames;
  request.blocks = blocks;
  request.num_events = static_cast<uint32_t>(events.size());
  request.events = events.empty() ? nullptr : events.data();
  request.lookahead = lookahead;
  AudScriptSimulation* result = nullptr;
  AUD_CHECK(aud_audio_script_simulate(&request, &result) == AUD_OK);
  return result;
}

}  // namespace aud_script_test

#endif  // AUD_SCRIPT_FIXTURE_HPP
