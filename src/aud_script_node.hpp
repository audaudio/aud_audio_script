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
// The audio thread of block n, with the lookahead L (the parameter
// `lookahead`, 3 by default):
//
// 1. applies the parameter events of the block, so that a new lookahead
//    counts from this block on;
// 2. stages the requests of the blocks before n + L that are not requested
//    yet - blocks 1 to L - 1 at the first block, the blocks a larger
//    lookahead adds -, then the input events of block n - MIDI control
//    changes and numeric controls of its event input - for the worker;
// 3. takes the result of block n from the results that wait in order:
//    the result of block n is on time and enters the block at the grid
//    point (step) or as a ramp; without it the newest older result arrived
//    late and enters as a step at frame 0 or as a ramp, the block counts
//    as late, and without any the samplers hold. Older results go; newer
//    ones wait for their blocks. An offline block waits for its result;
// 4. publishes the staged entries with the request of block n + L, its
//    beat predicted from the transport at the end of block n plus L - 1
//    blocks, and posts the worker's semaphore - the one system call of
//    the audio thread, which never blocks.
//
// The worker runs the pending requests in order, each once, with the
// events that came before each; only when more than L requests wait - it
// fell behind - it runs the newest one and counts the others as skipped.
// Loads arrive from the control thread under a mutex the audio thread
// never takes; the worker builds the context and swaps it in before the
// first tick whose request the audio thread issued after the load, so that
// an offline render that follows a load always ticks the new script.

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
  // A request: the block, its predicted beat, the transport, the budget,
  // the memory limit and the lookahead.
  uint64_t tick = 0;
  double b0 = 0;
  double tempo = 0;
  double sampleRate = 0;
  uint32_t playing = 0;
  uint32_t budgetMs = 0;
  uint32_t memoryMb = 0;
  uint32_t lookahead = 1;
};

// A result of the worker: the values of the context for a block, and the
// grid point of its tick - the beat, the tempo and the transport state -,
// from which the block that emits it finds the frame of the grid point in
// its own transport.
struct AudScriptResult {
  uint64_t tick = 0;
  double beat = 0;
  double tempo = 0;
  uint32_t playing = 0;
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
  // The entries of the ring to the worker and the results in flight: more
  // than the largest lookahead plus one.
  static constexpr size_t kRingEntries = 2048;
  static constexpr size_t kResults = 8;
  static constexpr uint32_t kMaxLookahead = 4;

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
  // the request of the block `lookahead` blocks ahead.
  void process(const AudProcessContext& context);

  // [control] Applies a string setting.
  int32_t setString(uint32_t key, const char* value);

 private:
  explicit AudScriptNode(const AudHostApi* host);

  bool start();

  // ..........................................................................
  // The audio thread

  // Where the transport stands at the end of a block, for the prediction
  // of the requests.
  struct Prediction {
    double endBeat = 0;
    double beatsPerBlock = 0;
    double tempo = 120;
    bool playing = false;
  };

  Prediction predict(const AudProcessContext& context) const;
  void stageRequest(uint64_t block, const Prediction& prediction,
                    const AudProcessContext& context, uint32_t lookahead);
  void stageEvent(const AudEvent& event);
  void stage(const AudScriptEntry& entry);
  // The result of block `tick`, or the newest older one; drops the older
  // ones; nullptr without one.
  const AudScriptResult* takeResult(uint64_t tick);
  // The frame of the result's grid point in the block of `context`, as the
  // tick grid computes it from the block's own transport: 0 when the
  // transport stands or the point lies outside the block.
  uint32_t gridOffset(const AudScriptResult& result,
                      const AudProcessContext& context) const;
  void emit(const AudScriptResult& result, bool onTime, uint32_t frames,
            uint32_t offset);
  void emitOne(uint32_t output, uint32_t id, float value, uint32_t offset);
  // Waits until a result for `tick` or later is there, up to the offline
  // timeout.
  void waitForResult(uint64_t tick);
  bool stepMode(uint32_t output) const;

  // ..........................................................................
  // The worker

  static void workerEntry(void* self);
  void workerMain();
  // Builds the loads that wait; the newest built one becomes pending.
  void takeLoads();
  void endLoad(int32_t id, int32_t result, const char* message);
  // Runs the tick of a request with the inputs that came before it.
  void runRequest(const AudScriptEntry& request);
  void publish(const AudScriptBlock& block, const AudScriptGrid& grid);
  // Places the worker as "worker_cores" asks, when it changed.
  void place();
  void measureMemory(bool force);

  const AudHostApi* host_;
  std::atomic<float> params_[AUD_SCRIPT_NUM_PARAMS];

  // The audio thread's: the block being rendered, the last block
  // requested, the events without room.
  uint64_t block_ = 0;
  uint64_t requested_ = 0;
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
  std::string name_ = "script";

  // The worker's.
  AudScriptRunner runner_;
  std::unique_ptr<AudScriptState> pending_;
  uint64_t pendingAfter_ = 0;
  std::vector<AudScriptInput> inputs_;
  std::vector<AudScriptEntry> batch_;
  AudScriptPlacement placement_;
  bool placed_ = false;
  int64_t measuredNs_ = 0;

  AudScriptThread worker_;
};

// The descriptor of aud.script.modulator.
const AudNodeDescriptor& audScriptModulatorDescriptor();

}  // namespace aud_script

#endif  // AUD_SCRIPT_NODE_HPP
