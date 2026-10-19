// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

#include "aud_script_node.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

#include "aud_clock.h"
#include "aud_script_stats.hpp"
#include "aud_transport.h"
#include "aud_ump.h"

namespace aud_script {
namespace {

// ############################################################################
// The descriptor

constexpr uint32_t kStepped = AUD_PARAM_AUTOMATABLE | AUD_PARAM_STEPPED;

#define AUD_SCRIPT_MODE(n)     \
  {sizeof(AudParamDescriptor), \
   "mode" #n,                  \
   "Mode of output " #n,       \
   "",                         \
   0.0f,                       \
   1.0f,                       \
   0.0f,                       \
   kStepped,                   \
   2,                          \
   0}

const AudParamDescriptor kParams[AUD_SCRIPT_NUM_PARAMS] = {
    AUD_SCRIPT_MODE(0),
    AUD_SCRIPT_MODE(1),
    AUD_SCRIPT_MODE(2),
    AUD_SCRIPT_MODE(3),
    AUD_SCRIPT_MODE(4),
    AUD_SCRIPT_MODE(5),
    AUD_SCRIPT_MODE(6),
    AUD_SCRIPT_MODE(7),
    AUD_SCRIPT_MODE(8),
    AUD_SCRIPT_MODE(9),
    AUD_SCRIPT_MODE(10),
    AUD_SCRIPT_MODE(11),
    AUD_SCRIPT_MODE(12),
    AUD_SCRIPT_MODE(13),
    AUD_SCRIPT_MODE(14),
    AUD_SCRIPT_MODE(15),
    {sizeof(AudParamDescriptor), "memory_limit", "Memory limit", "MB", 1.0f,
     1024.0f, 32.0f, kStepped, 1024, 0},
    {sizeof(AudParamDescriptor), "tick_budget", "Tick budget", "ms", 1.0f,
     10000.0f, 100.0f, kStepped, 10000, 0},
    {sizeof(AudParamDescriptor), "lookahead", "Lookahead", "blocks", 1.0f, 4.0f,
     3.0f, kStepped, 4, 0},
    {sizeof(AudParamDescriptor), "worker_cores", "Worker cores", "", 0.0f, 1.0f,
     0.0f, kStepped, 2, 0},
};

#undef AUD_SCRIPT_MODE

const AudEventPortDescriptor kEventInputs[] = {
    {sizeof(AudEventPortDescriptor), "in", "Input",
     AUD_EVENT_PORT_MIDI | AUD_EVENT_PORT_CONTROL, 0},
};

#define AUD_SCRIPT_OUTPUT(n)                               \
  {sizeof(AudEventPortDescriptor), "out" #n, "Output " #n, \
   AUD_EVENT_PORT_CONTROL, 0}

const AudEventPortDescriptor kEventOutputs[AUD_SCRIPT_NUM_OUTPUTS] = {
    AUD_SCRIPT_OUTPUT(0),  AUD_SCRIPT_OUTPUT(1),  AUD_SCRIPT_OUTPUT(2),
    AUD_SCRIPT_OUTPUT(3),  AUD_SCRIPT_OUTPUT(4),  AUD_SCRIPT_OUTPUT(5),
    AUD_SCRIPT_OUTPUT(6),  AUD_SCRIPT_OUTPUT(7),  AUD_SCRIPT_OUTPUT(8),
    AUD_SCRIPT_OUTPUT(9),  AUD_SCRIPT_OUTPUT(10), AUD_SCRIPT_OUTPUT(11),
    AUD_SCRIPT_OUTPUT(12), AUD_SCRIPT_OUTPUT(13), AUD_SCRIPT_OUTPUT(14),
    AUD_SCRIPT_OUTPUT(15),
};

#undef AUD_SCRIPT_OUTPUT

const AudStringKeyDescriptor kKeys[AUD_SCRIPT_NUM_KEYS] = {
    {sizeof(AudStringKeyDescriptor), AUD_SCRIPT_KEY_SCRIPT, "script", "Script"},
    {sizeof(AudStringKeyDescriptor), AUD_SCRIPT_KEY_SCRIPT_NAME, "script_name",
     "Script name"},
};

// ############################################################################
// The vtable

void* create(const AudNodeDescriptor*, const AudHostApi* host) {
  try {
    return AudScriptNode::create(host);
  } catch (...) {
    return nullptr;
  }
}

void destroy(void* instance) { delete static_cast<AudScriptNode*>(instance); }

int32_t prepare(void* instance, const AudPrepareInfo* info) {
  if (info == nullptr) return AUD_ERROR_INVALID_ARGUMENT;
  return static_cast<AudScriptNode*>(instance)->prepare(*info);
}

void reset(void* instance, uint32_t reason) {
  static_cast<AudScriptNode*>(instance)->reset(reason);
}

void setParam(void* instance, uint32_t index, float value) {
  static_cast<AudScriptNode*>(instance)->setParam(index, value);
}

void process(void* instance, const AudProcessContext* context) {
  static_cast<AudScriptNode*>(instance)->process(*context);
}

int32_t setString(void* instance, uint32_t key, const char* value) {
  try {
    return static_cast<AudScriptNode*>(instance)->setString(key, value);
  } catch (...) {
    return AUD_ERROR_OUT_OF_MEMORY;
  }
}

const AudNodeVTable kVTable = {
    sizeof(AudNodeVTable),
    create,
    destroy,
    prepare,
    reset,
    setParam,
    process,
    setString,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};

// ############################################################################
// Helpers

// The longest wait of an offline block for its worker beyond the tick
// budget: a load may come first.
constexpr int64_t kOfflineSlackNs = kLoadBudgetNs + 1000000000LL;

// The longest single wait of an offline block: a lost wake-up costs at
// most this.
constexpr int64_t kWaitSliceNs = 100000000LL;

// The interval of the memory measurement and of the placement check.
constexpr int64_t kMeasureIntervalNs = 1000000000LL;

constexpr size_t kMegabyte = 1024u * 1024u;

bool sameBits(float a, float b) {
  uint32_t x = 0;
  uint32_t y = 0;
  std::memcpy(&x, &a, sizeof(x));
  std::memcpy(&y, &b, sizeof(y));
  return x == y;
}

}  // namespace

const AudNodeDescriptor& audScriptModulatorDescriptor() {
  static const AudNodeDescriptor descriptor = [] {
    AudNodeDescriptor d = {};
    d.struct_size = sizeof(AudNodeDescriptor);
    d.abi_major = AUD_ABI_VERSION_MAJOR;
    d.abi_minor = AUD_ABI_VERSION_MINOR;
    d.version = 1;
    d.type_id = AUD_SCRIPT_MODULATOR_TYPE_ID;
    d.name = "Script modulator";
    d.vendor = "Audanika";
    d.capabilities = AUD_NODE_CAP_VARIABLE_BLOCK | AUD_NODE_CAP_EVENTS |
                     AUD_NODE_CAP_STRINGS | AUD_NODE_CAP_TRANSPORT |
                     AUD_NODE_CAP_EVENT_OUTPUT;
    d.num_event_inputs = 1;
    d.num_event_outputs = AUD_SCRIPT_NUM_OUTPUTS;
    d.event_inputs = kEventInputs;
    d.event_outputs = kEventOutputs;
    d.num_params = AUD_SCRIPT_NUM_PARAMS;
    d.num_string_keys = AUD_SCRIPT_NUM_KEYS;
    d.params = kParams;
    d.string_keys = kKeys;
    d.vtable = &kVTable;
    return d;
  }();
  return descriptor;
}

// ############################################################################
// Lifecycle

AudScriptNode::AudScriptNode(const AudHostApi* host)
    : host_(host),
      emittedValues_(AUD_SCRIPT_NUM_OUTPUTS * AUD_SCRIPT_NUM_IDS, 0.0f),
      emitted_(AUD_SCRIPT_NUM_OUTPUTS * AUD_SCRIPT_NUM_IDS, 0),
      requests_(kRingEntries) {
  for (uint32_t i = 0; i < AUD_SCRIPT_NUM_PARAMS; ++i) {
    params_[i].store(kParams[i].default_value, std::memory_order_relaxed);
  }
  changed_.reserve(AUD_SCRIPT_MAX_VALUES);
  // The statistics exist before the audio thread counts into them.
  AudScriptGlobalStats::get();
  inputs_.reserve(kRingEntries);
  runner_.onConsole = [](const std::string& line) {
    AudScriptGlobalStats::get().addConsoleLine(line);
  };
  runner_.onError = [](const std::string& text) {
    AudScriptGlobalStats::get().setError(text);
  };
}

AudScriptNode* AudScriptNode::create(const AudHostApi* host) {
  AudScriptNode* node = new (std::nothrow) AudScriptNode(host);
  if (node == nullptr) return nullptr;
  if (!node->start()) {
    delete node;
    return nullptr;
  }
  return node;
}

bool AudScriptNode::start() { return worker_.start(workerEntry, this); }

AudScriptNode::~AudScriptNode() {
  stop_.store(true, std::memory_order_release);
  wake_.post();
  worker_.join();
  std::lock_guard<std::mutex> lock(loadsMutex_);
  for (const AudScriptLoadCommand& command : loads_) {
    endLoad(command.id, AUD_ERROR_CANCELLED, "the node was destroyed");
  }
  loads_.clear();
}

int32_t AudScriptNode::prepare(const AudPrepareInfo& info) {
  if (!(info.sample_rate > 0) || !std::isfinite(info.sample_rate)) {
    return AUD_ERROR_INVALID_ARGUMENT;
  }
  return AUD_OK;
}

void AudScriptNode::reset(uint32_t reason) { (void)reason; }

void AudScriptNode::setParam(uint32_t index, float value) {
  if (index >= AUD_SCRIPT_NUM_PARAMS || std::isnan(value)) return;
  const AudParamDescriptor& d = kParams[index];
  value =
      std::min(d.max_value, std::max(d.min_value, std::floor(value + 0.5f)));
  params_[index].store(value, std::memory_order_relaxed);
}

// ############################################################################
// The audio thread

bool AudScriptNode::stepMode(uint32_t output) const {
  return params_[AUD_SCRIPT_PARAM_MODE0 + output].load(
             std::memory_order_relaxed) >= 0.5f;
}

void AudScriptNode::stage(const AudScriptEntry& entry) {
  // One entry stays free for the request of the block.
  if (requests_.room() > 1) {
    requests_.stage(entry);
  } else {
    dropped_ += 1;
  }
}

void AudScriptNode::stageEvent(const AudEvent& event) {
  AudScriptEntry entry;
  if (event.type == AUD_EVENT_UMP) {
    const uint32_t w0 = event.words[0];
    if (!aud_ump_is_channel_voice(w0) ||
        aud_ump_status(w0) != AUD_UMP_STATUS_CONTROL_CHANGE) {
      return;
    }
    entry.kind = AudScriptEntry::kControlChange;
    entry.channel = aud_ump_channel(w0);
    entry.number = aud_ump_midi1_data1(w0);
    if (aud_ump_message_type(w0) == AUD_UMP_TYPE_MIDI1_CHANNEL_VOICE) {
      entry.value = aud_ump_midi1_data2(w0);
    } else {
      // MIDI 2.0: the 32-bit value on the scale of 0 to 127.
      entry.value = static_cast<double>(event.words[1]) / 4294967295.0 * 127.0;
    }
    stage(entry);
    return;
  }
  if (event.type != AUD_EVENT_CONTROL) return;
  entry.kind = AudScriptEntry::kControl;
  entry.number = event.words[0];
  switch (event.words[1]) {
    case AUD_ARG_FLOAT: {
      float value = 0;
      std::memcpy(&value, &event.words[2], sizeof(value));
      entry.value = value;
      break;
    }
    case AUD_ARG_INT32:
      entry.value = static_cast<int32_t>(event.words[2]);
      break;
    case AUD_ARG_TRUE:
      entry.value = 1;
      break;
    case AUD_ARG_FALSE:
      entry.value = 0;
      break;
    default:
      return;
  }
  stage(entry);
}

void AudScriptNode::emitOne(uint32_t output, uint32_t id, float value,
                            uint32_t offset) {
  const AudEvent event = aud_event_control_float(id, value, offset, output);
  host_->emit_event(host_->host, this, &event);
}

void AudScriptNode::emit(const AudScriptResult& result, bool onTime,
                         uint32_t frames) {
  if (host_ == nullptr || host_->emit_event == nullptr) return;
  const uint32_t last = frames - 1;
  const uint32_t k = onTime ? std::min(result.offset, last) : 0;
  bool step[AUD_SCRIPT_NUM_OUTPUTS];
  for (uint32_t o = 0; o < AUD_SCRIPT_NUM_OUTPUTS; ++o) step[o] = stepMode(o);
  changed_.clear();
  const uint32_t count =
      std::min<uint32_t>(result.count, AUD_SCRIPT_MAX_VALUES);
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t pair =
        result.outputs[i] * AUD_SCRIPT_NUM_IDS + result.ids[i];
    if (emitted_[pair] && sameBits(emittedValues_[pair], result.values[i])) {
      continue;
    }
    changed_.push_back(static_cast<uint16_t>(i));
  }
  // In ascending offsets: the old values of the stepped outputs one frame
  // before the grid point, their new values at it, then the ramps.
  if (k > 0) {
    for (const uint16_t i : changed_) {
      const uint32_t output = result.outputs[i];
      const uint32_t pair = output * AUD_SCRIPT_NUM_IDS + result.ids[i];
      if (step[output] && emitted_[pair]) {
        emitOne(output, result.ids[i], emittedValues_[pair], k - 1);
      }
    }
  }
  for (const uint16_t i : changed_) {
    const uint32_t output = result.outputs[i];
    if (step[output]) emitOne(output, result.ids[i], result.values[i], k);
  }
  for (const uint16_t i : changed_) {
    const uint32_t output = result.outputs[i];
    if (!step[output]) emitOne(output, result.ids[i], result.values[i], last);
  }
  for (const uint16_t i : changed_) {
    const uint32_t pair =
        result.outputs[i] * AUD_SCRIPT_NUM_IDS + result.ids[i];
    emittedValues_[pair] = result.values[i];
    emitted_[pair] = 1;
  }
}

void AudScriptNode::waitForResult(uint64_t tick) {
  // The results come in the order of their blocks: the last one tells.
  auto ready = [this, tick] {
    const size_t size = results_.size();
    return size > 0 && results_.at(size - 1)->tick >= tick;
  };
  if (ready()) return;
  // An offline block waits for its worker; a live block never does.
  AudScriptRealtimeExempt exempt;
  const int64_t budgetNs = static_cast<int64_t>(
      params_[AUD_SCRIPT_PARAM_TICK_BUDGET].load(std::memory_order_relaxed) *
      1e6);
  const int64_t deadline = aud_clock_now_ns() + budgetNs + kOfflineSlackNs;
  waiting_.store(true, std::memory_order_seq_cst);
  std::atomic_thread_fence(std::memory_order_seq_cst);
  while (!ready()) {
    const int64_t left = deadline - aud_clock_now_ns();
    if (left <= 0) break;
    resultReady_.waitFor(std::min(left, kWaitSliceNs));
  }
  waiting_.store(false, std::memory_order_seq_cst);
  while (resultReady_.tryWait()) {
  }
}

const AudScriptResult* AudScriptNode::takeResult(uint64_t tick) {
  // The last result of this block or before; the ones before it go.
  const size_t size = results_.size();
  size_t last = size;
  for (size_t i = 0; i < size && results_.at(i)->tick <= tick; ++i) last = i;
  if (last == size) return nullptr;
  for (size_t i = 0; i < last; ++i) results_.pop();
  return results_.at(0);
}

AudScriptNode::Prediction AudScriptNode::predict(
    const AudProcessContext& context) const {
  Prediction prediction;
  const uint32_t frames = context.frames;
  const AudTransportSnapshot* transport = context.transport;
  if (transport == nullptr || transport->num_segments == 0 ||
      transport->segments == nullptr) {
    return prediction;
  }
  const AudTransportSegment* segment =
      aud_transport_segment_at(transport, frames > 0 ? frames - 1 : 0);
  if (segment == nullptr) return prediction;
  const double after = static_cast<double>(frames - segment->sample_offset);
  prediction.endBeat =
      aud_beats_from_ticks(segment->beat) +
      aud_transport_beats_after(segment, context.sample_rate, after);
  prediction.tempo = segment->tempo + segment->tempo_increment * after;
  prediction.playing = (segment->flags & AUD_SEGMENT_PLAYING) != 0;
  if (prediction.playing && context.sample_rate > 0) {
    // As the tick grid writes it.
    prediction.beatsPerBlock = static_cast<double>(frames) * prediction.tempo /
                               60 / context.sample_rate;
  }
  return prediction;
}

void AudScriptNode::stageRequest(uint64_t block, const Prediction& prediction,
                                 const AudProcessContext& context,
                                 uint32_t lookahead) {
  AudScriptEntry request;
  request.kind = AudScriptEntry::kRequest;
  request.tick = block;
  request.frames = context.frames;
  request.sampleRate = context.sample_rate;
  // Block n + 1 starts where block n ends; the blocks after it follow with
  // the same frames and tempo.
  request.b0 = prediction.endBeat + static_cast<double>(block - block_ - 1) *
                                        prediction.beatsPerBlock;
  request.tempo = prediction.tempo;
  request.playing = prediction.playing ? 1 : 0;
  request.budgetMs = static_cast<uint32_t>(
      params_[AUD_SCRIPT_PARAM_TICK_BUDGET].load(std::memory_order_relaxed));
  request.memoryMb = static_cast<uint32_t>(
      params_[AUD_SCRIPT_PARAM_MEMORY_LIMIT].load(std::memory_order_relaxed));
  request.lookahead = lookahead;
  if (requests_.room() > 0) {
    requests_.stage(request);
  } else {
    AudScriptGlobalStats::get().countSkipped(1);
  }
  requested_ = block;
}

void AudScriptNode::process(const AudProcessContext& context) {
  const uint32_t frames = context.frames;
  // The parameters first: a new lookahead counts from this block on.
  for (uint32_t i = 0; i < context.num_events; ++i) {
    const AudEvent& event = context.events[i];
    if (event.type == AUD_EVENT_PARAM) {
      setParam(event.words[0], aud_event_param_value(&event));
    }
  }
  const uint32_t lookahead = static_cast<uint32_t>(
      params_[AUD_SCRIPT_PARAM_LOOKAHEAD].load(std::memory_order_relaxed));
  const uint64_t tick = block_;
  const uint64_t target = tick + lookahead;
  const Prediction prediction = predict(context);
  // The blocks before the target that nobody requested yet, before the
  // events of this block, which belong to the target's tick.
  while (requested_ + 1 < target) {
    stageRequest(requested_ + 1, prediction, context, lookahead);
  }
  for (uint32_t i = 0; i < context.num_events; ++i) {
    const AudEvent& event = context.events[i];
    if (event.type != AUD_EVENT_PARAM) stageEvent(event);
  }
  // The values of the worker for this block.
  if (tick > 0 && (context.flags & AUD_PROCESS_OFFLINE) != 0) {
    waitForResult(tick);
  }
  const AudScriptResult* result = takeResult(tick);
  bool onTime = false;
  if (result != nullptr) {
    onTime = result->tick == tick;
    if (frames > 0) emit(*result, onTime, frames);
    results_.pop();
  }
  AudScriptGlobalStats& stats = AudScriptGlobalStats::get();
  if (tick > 0 && !onTime) stats.countLate();
  stats.setLookahead(lookahead);
  // The request of the block `lookahead` blocks ahead.
  if (requested_ < target) stageRequest(target, prediction, context, lookahead);
  if (dropped_ > 0) {
    stats.countDropped(dropped_);
    dropped_ = 0;
  }
  requests_.publish();
  issued_.store(requested_, std::memory_order_release);
  wake_.post();
  block_ = tick + 1;
}

// ############################################################################
// The control thread

int32_t AudScriptNode::setString(uint32_t key, const char* value) {
  const std::string text = value != nullptr ? value : "";
  switch (key) {
    case AUD_SCRIPT_KEY_SCRIPT:
      source_ = text;
      break;
    case AUD_SCRIPT_KEY_SCRIPT_NAME:
      name_ = text.empty() ? "script" : text;
      return AUD_OK;
    default:
      return AUD_ERROR_INVALID_ARGUMENT;
  }
  AudScriptLoadCommand command;
  command.load.source = source_;
  command.load.name = name_;
  command.applyAfter = issued_.load(std::memory_order_acquire);
  if (host_ != nullptr && host_->begin_load != nullptr &&
      host_->end_load != nullptr) {
    const int32_t begun = host_->begin_load(host_->host, this, name_.c_str());
    command.id = begun > 0 ? begun : 0;
  }
  const int32_t id = command.id;
  try {
    std::lock_guard<std::mutex> lock(loadsMutex_);
    loads_.push_back(std::move(command));
  } catch (...) {
    endLoad(id, AUD_ERROR_OUT_OF_MEMORY, nullptr);
    return AUD_ERROR_OUT_OF_MEMORY;
  }
  wake_.post();
  return id > 0 ? id : AUD_OK;
}

// ############################################################################
// The worker

void AudScriptNode::workerEntry(void* self) {
  static_cast<AudScriptNode*>(self)->workerMain();
}

void AudScriptNode::endLoad(int32_t id, int32_t result, const char* message) {
  if (id <= 0 || host_ == nullptr || host_->end_load == nullptr) return;
  host_->end_load(host_->host, id, result, message);
}

void AudScriptNode::takeLoads() {
  std::deque<AudScriptLoadCommand> loads;
  {
    std::lock_guard<std::mutex> lock(loadsMutex_);
    loads.swap(loads_);
  }
  AudScriptGlobalStats& stats = AudScriptGlobalStats::get();
  for (size_t i = 0; i < loads.size(); ++i) {
    AudScriptLoadCommand& command = loads[i];
    if (i + 1 < loads.size()) {
      endLoad(command.id, AUD_ERROR_CANCELLED, "a newer load replaced it");
      continue;
    }
    if (command.id > 0 && host_ != nullptr &&
        host_->load_cancelled != nullptr &&
        host_->load_cancelled(host_->host, command.id) != 0) {
      endLoad(command.id, AUD_ERROR_CANCELLED, nullptr);
      continue;
    }
    command.load.memoryLimit =
        static_cast<size_t>(params_[AUD_SCRIPT_PARAM_MEMORY_LIMIT].load(
            std::memory_order_relaxed)) *
        kMegabyte;
    AudScriptLoadTimes times;
    std::string error;
    std::unique_ptr<AudScriptState> state =
        runner_.load(command.load, &times, &error);
    stats.countLoad(state != nullptr ? AUD_OK : AUD_ERROR_FAILED,
                    times.createUs, times.loadUs);
    if (state == nullptr) {
      stats.setError(error);
      endLoad(command.id, AUD_ERROR_FAILED, error.c_str());
      continue;
    }
    stats.setMemory(times.memory);
    pending_ = std::move(state);
    pendingAfter_ = command.applyAfter;
    endLoad(command.id, AUD_OK, nullptr);
  }
}

void AudScriptNode::publish(uint64_t tick, uint32_t offset) {
  AudScriptResult* slot = results_.writeSlot();
  if (slot == nullptr) {
    AudScriptGlobalStats::get().countSkipped(1);
    return;
  }
  slot->tick = tick;
  slot->offset = offset;
  const AudScriptState* state = runner_.state();
  const uint32_t count = state != nullptr ? state->values.size() : 0;
  for (uint32_t i = 0; i < count; ++i) {
    slot->outputs[i] = static_cast<uint16_t>(state->values.outputAt(i));
    slot->ids[i] = static_cast<uint16_t>(state->values.idAt(i));
    slot->values[i] = state->values.valueAt(i);
  }
  slot->count = count;
  results_.commit();
  std::atomic_thread_fence(std::memory_order_seq_cst);
  if (waiting_.load(std::memory_order_seq_cst)) resultReady_.post();
}

void AudScriptNode::measureMemory(bool force) {
  const int64_t now = aud_clock_now_ns();
  if (!force && now - measuredNs_ < kMeasureIntervalNs) return;
  measuredNs_ = now;
  audScriptRefreshPlacement(placement_);
  AudScriptState* state = runner_.state();
  if (state != nullptr) {
    AudScriptGlobalStats::get().setMemory(state->context->memoryUsed());
  }
}

void AudScriptNode::runRequest(const AudScriptEntry& request) {
  if (pending_ != nullptr && request.tick > pendingAfter_) {
    runner_.adopt(std::move(pending_));
    measuredNs_ = 0;
  }
  AudScriptState* state = runner_.state();
  const size_t limit = static_cast<size_t>(request.memoryMb) * kMegabyte;
  if (state != nullptr && limit > 0 && limit != state->memoryLimit) {
    state->context->setMemoryLimit(limit);
    state->memoryLimit = limit;
  }
  AudScriptBlock block;
  block.tick = request.tick;
  block.b0 = request.b0;
  block.tempo = request.tempo;
  block.playing = request.playing != 0;
  block.sampleRate = request.sampleRate;
  block.frames = request.frames;
  const AudScriptTickResult result =
      runner_.runTick(block, inputs_.data(), inputs_.size(),
                      static_cast<int64_t>(request.budgetMs) * 1000000LL);
  inputs_.clear();
  publish(request.tick, result.grid.offset);
  if (result.ran) {
    AudScriptGlobalStats& stats = AudScriptGlobalStats::get();
    stats.countTick(result.timeUs, result.status);
    stats.setLastCpu(audScriptCurrentCpu());
  }
}

void AudScriptNode::place() {
  const bool fast = params_[AUD_SCRIPT_PARAM_WORKER_CORES].load(
                        std::memory_order_relaxed) >= 0.5f;
  if (placed_ && fast == placement_.fastCores) return;
  placement_ = audScriptPlaceCurrentThread(fast);
  placed_ = true;
  AudScriptGlobalStats::get().setPlacement(placement_);
}

void AudScriptNode::workerMain() {
  place();
  AudScriptGlobalStats& stats = AudScriptGlobalStats::get();
  measuredNs_ = aud_clock_now_ns();
  while (!stop_.load(std::memory_order_acquire)) {
    wake_.waitFor(kMeasureIntervalNs);
    if (stop_.load(std::memory_order_acquire)) break;
    // A new value of "worker_cores" applies here, never on the audio
    // thread.
    place();
    batch_.clear();
    AudScriptEntry entry;
    size_t requests = 0;
    size_t newest = 0;
    while (requests_.pop(entry)) {
      if (entry.kind == AudScriptEntry::kRequest) {
        requests += 1;
        newest = batch_.size();
      }
      batch_.push_back(entry);
    }
    // The loads that arrived before the newest request are visible now.
    takeLoads();
    // More requests than the lookahead wait: the worker fell behind and
    // runs the newest one only.
    const uint32_t lookahead =
        requests > 0 ? std::max<uint32_t>(1, batch_[newest].lookahead) : 1;
    const bool behind = requests > lookahead;
    if (behind) stats.countSkipped(requests - 1);
    for (size_t i = 0; i < batch_.size(); ++i) {
      const AudScriptEntry& e = batch_[i];
      if (e.kind != AudScriptEntry::kRequest) {
        AudScriptInput input;
        input.kind = e.kind == AudScriptEntry::kControlChange
                         ? AudScriptInput::kControlChange
                         : AudScriptInput::kControl;
        input.channel = e.channel;
        input.number = e.number;
        input.value = e.value;
        inputs_.push_back(input);
      } else if (!behind || i == newest) {
        runRequest(e);
      }
    }
    // Events after the last request wait for the next one.
    measureMemory(false);
  }
  // The contexts end on the thread that ran them.
  runner_.adopt(nullptr);
  pending_.reset();
}

}  // namespace aud_script
