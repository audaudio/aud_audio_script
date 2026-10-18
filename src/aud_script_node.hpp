// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// The node aud.script.modulator (ticket 31, decision script-001): runs a
// script on a worker thread of its own, once per audio block, and emits
// the values the script sends as AUD_EVENT_CONTROL events on its 16 event
// outputs.
//
// The audio thread of block n:
//
// 1. stages the input events of block n - MIDI control changes and
//    numeric controls of its event input - for the worker;
// 2. takes the newest result of the worker: the result for block n is on
//    time and enters the block at the grid point (step) or as a ramp; an
//    older one arrived late and enters as a step at frame 0 or as a ramp;
//    without the result for block n the block counts as late, and the
//    samplers hold. An offline block waits for its result;
// 3. publishes the staged events with the request for block n + 1, its
//    beat predicted from the transport of block n, and posts the worker's
//    semaphore - the one system call of the audio thread, which never
//    blocks.
//
// The worker runs the newest request only and counts the skipped ones; it
// delivers every event before the tick, in order. Loads arrive from the
// control thread under a mutex the audio thread never takes; the worker
// builds the context and swaps it in before the first tick whose request
// the audio thread issued after the load, so that an offline render that
// follows a load always ticks the new script.

#ifndef AUD_SCRIPT_NODE_HPP
#define AUD_SCRIPT_NODE_HPP

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "aud_abi.h"
#include "aud_audio_script.h"
#include "aud_script_placement.hpp"
#include "aud_script_rings.hpp"
#include "aud_script_runner.hpp"
#include "aud_script_sync.hpp"

namespace aud_script {

// An entry of the ring from the audio thread to the worker: an input event
// or the request of a block.
struct AudScriptEntry {
  enum Kind : uint32_t { kControlChange = 0, kControl = 1, kRequest = 2 };
  uint32_t kind = kRequest;
  // An event: the channel and the controller of a control change, or the
  // id of a control, and its value.
  uint32_t channel = 0;
  uint32_t number = 0;
  uint32_t frames = 0;
  double value = 0;
  // A request: the block, its predicted beat, the transport, the budget
  // and the memory limit.
  uint64_t tick = 0;
  double b0 = 0;
  double tempo = 0;
  double sampleRate = 0;
  uint32_t playing = 0;
  uint32_t budgetMs = 0;
  uint32_t memoryMb = 0;
  uint32_t reserved = 0;
};

// A result of the worker: the values of the context for a block.
struct AudScriptResult {
  uint64_t tick = 0;
  uint32_t offset = 0;
  uint32_t count = 0;
  uint16_t outputs[AUD_SCRIPT_MAX_VALUES];
  uint16_t ids[AUD_SCRIPT_MAX_VALUES];
  float values[AUD_SCRIPT_MAX_VALUES];
};

// A load the control thread hands the worker.
struct AudScriptLoadCommand {
  AudScriptLoad load;
  // The id of the engine's load, or 0.
  int32_t id = 0;
  // The new context ticks the requests after this one.
  uint64_t applyAfter = 0;
};

class AudScriptNode {
 public:
  // The entries of the ring to the worker and the results in flight.
  static constexpr size_t kRingEntries = 2048;
  static constexpr size_t kResults = 8;

  // [control] Creates a node and starts its worker; nullptr when memory or
  // the thread is refused.
  static AudScriptNode* create(const AudHostApi* host);

  // [control] Stops the worker; the loads it did not start end as
  // cancelled.
  ~AudScriptNode();

  AudScriptNode(const AudScriptNode&) = delete;
  AudScriptNode& operator=(const AudScriptNode&) = delete;

  // [control] Takes the sample rate; nothing to allocate.
  int32_t prepare(const AudPrepareInfo& info);

  // [realtime] Keeps the values and the worker; the next result enters as
  // usual.
  void reset(uint32_t reason);

  // [realtime] Sets a parameter.
  void setParam(uint32_t index, float value);

  // [realtime] Renders a block: emits the values of the worker, hands it
  // the request of the next block.
  void process(const AudProcessContext& context);

  // [control] Applies a string setting.
  int32_t setString(uint32_t key, const char* value);

 private:
  explicit AudScriptNode(const AudHostApi* host);

  bool start();

  // ..........................................................................
  // The audio thread

  void stageEvent(const AudEvent& event);
  void stage(const AudScriptEntry& entry);
  void emit(const AudScriptResult& result, bool onTime, uint32_t frames);
  void emitOne(uint32_t output, uint32_t id, float value, uint32_t offset);
  // The newest result for `tick` or later, waiting up to the offline
  // timeout; the newest one when it times out.
  const AudScriptResult* waitForResult(uint64_t tick);
  bool stepMode(uint32_t output) const;

  // ..........................................................................
  // The worker

  static void workerEntry(void* self);
  void workerMain();
  // Builds the loads that wait; the newest built one becomes pending.
  void takeLoads();
  void endLoad(int32_t id, int32_t result, const char* message);
  void publish(uint64_t tick, uint32_t offset);
  void measureMemory(bool force);

  const AudHostApi* host_;
  std::atomic<float> params_[AUD_SCRIPT_NUM_PARAMS];

  // The audio thread's.
  uint64_t block_ = 0;
  uint64_t dropped_ = 0;
  // The value each (output, id) pair emitted last, and whether it did.
  std::vector<float> emittedValues_;
  std::vector<uint8_t> emitted_;
  // The pairs of the result being emitted that changed.
  std::vector<uint16_t> changed_;

  // Shared between the audio thread and the worker.
  AudScriptBatchRing<AudScriptEntry> requests_;
  AudScriptSlotRing<AudScriptResult, kResults> results_;
  AudScriptSemaphore wake_;
  AudScriptSemaphore resultReady_;
  // Raised while the audio thread waits for a result of an offline block.
  std::atomic<bool> waiting_{false};
  // The last request the audio thread issued.
  std::atomic<uint64_t> issued_{0};
  std::atomic<bool> stop_{false};

  // Shared between the control thread and the worker.
  std::mutex loadsMutex_;
  std::deque<AudScriptLoadCommand> loads_;

  // The control thread's.
  std::string source_;
  bool hasSource_ = false;
  std::string name_ = "script";
  uint32_t engine_ = AUD_SCRIPT_ENGINE_QUICKJS;

  // The worker's.
  AudScriptRunner runner_;
  std::unique_ptr<AudScriptState> pending_;
  uint64_t pendingAfter_ = 0;
  std::vector<AudScriptInput> inputs_;
  AudScriptPlacement placement_;
  int64_t measuredNs_ = 0;

  AudScriptThread worker_;
};

// The descriptor of aud.script.modulator.
const AudNodeDescriptor& audScriptModulatorDescriptor();

}  // namespace aud_script

#endif  // AUD_SCRIPT_NODE_HPP
