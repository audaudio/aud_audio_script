// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// The host code of a script (ticket 31), the same for the worker of a
// modulator, the simulation and the bench: the tick grid, the load of a
// source into a context, the input callbacks before a tick, the tick with
// its time budget, and the values a context sent. Runs on one thread at a
// time - the worker - and may allocate.

#ifndef AUD_SCRIPT_RUNNER_HPP
#define AUD_SCRIPT_RUNNER_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "aud_audio_script.h"
#include "aud_script_engine.hpp"

namespace aud_script {

// ############################################################################
// The defaults and limits

// The defaults and limits that the node, the simulation and the bench
// share: the memory limit of a context in MB, the budget of a tick in ms,
// the largest lookahead in blocks.
constexpr size_t kMegabyte = 1024u * 1024u;
constexpr uint32_t kDefaultMemoryLimitMb = 32;
constexpr uint32_t kDefaultTickBudgetMs = 100;
constexpr uint32_t kMaxLookahead = 4;

// Whether a sample rate (8 to 384 kHz) and a tempo (1 to 999 BPM) are what
// a simulation or a bench takes.
bool audScriptValidTiming(double sampleRate, double tempo);

// The budget of a tick in ns from `tickBudgetMs`; 0 takes the default.
int64_t audScriptBudgetNs(uint32_t tickBudgetMs);

// ############################################################################
// The tick grid

// The block a tick is computed for: its number, the beat at its first frame,
// the tempo and the state of the transport, its frames.
struct AudScriptBlock {
  uint64_t tick = 0;
  double b0 = 0;
  double tempo = 120;
  bool playing = false;
  double sampleRate = 48000;
  uint32_t frames = 0;
};

// The grid point of a block: aud.time.beat and its frame offset k.
struct AudScriptGrid {
  double beat = 0;
  uint32_t offset = 0;
};

// The grid point of `block`, computed as aud_audio_script.h writes it.
AudScriptGrid audScriptGrid(const AudScriptBlock& block);

// ############################################################################
// The values

// The values a context sent, one per (output, id) pair, in the order the
// pairs were first sent; at most AUD_SCRIPT_MAX_VALUES pairs. The changes
// of a tick are undone when the tick is aborted; the undo log keeps one
// entry per pair a tick touched, however often the script sends to it.
class AudScriptValues {
 public:
  AudScriptValues();

  // Opens the changes of a tick.
  void begin() { restart(); }

  // Sets the value of a pair; the message of a RangeError when the pair is
  // new and the values are full.
  const char* set(uint32_t output, uint32_t id, float value);

  // Keeps the changes since begin(), or undoes them.
  void commit() { restart(); }
  void rollback();

  // The pairs and their values.
  uint32_t size() const { return static_cast<uint32_t>(pairs_.size()); }
  uint32_t outputAt(uint32_t i) const { return pairs_[i] / AUD_SCRIPT_NUM_IDS; }
  uint32_t idAt(uint32_t i) const { return pairs_[i] % AUD_SCRIPT_NUM_IDS; }
  float valueAt(uint32_t i) const { return values_[i]; }

 private:
  struct Undo {
    uint32_t slot;
    float old;
    bool added;
  };

  // Empties the undo log; the next send to a pair logs its value again.
  void restart() {
    undo_.clear();
    generation_ += 1;
  }

  std::vector<int16_t> index_;
  std::vector<uint16_t> pairs_;
  std::vector<float> values_;
  std::vector<Undo> undo_;
  // The undo log that holds the old value of each slot, by its generation,
  // which restart() advances.
  std::vector<uint64_t> logged_;
  uint64_t generation_ = 0;
};

// One aud.send call.
struct AudScriptSend {
  uint32_t output;
  uint32_t id;
  double value;
};

// An input event for a script: a MIDI control change or a numeric control.
struct AudScriptInput {
  enum Kind : uint32_t { kControlChange = 0, kControl = 1 };
  uint32_t kind = kControlChange;
  uint32_t channel = 0;  // a control change
  uint32_t number = 0;   // the controller, or the control id
  double value = 0;
};

class AudScriptRunner;

// ############################################################################
// A loaded script

// A loaded script: its context and the values it sent. The sink of its
// context.
class AudScriptState final : public AudScriptSink {
 public:
  explicit AudScriptState(AudScriptRunner* runner) : runner_(runner) {}

  const char* send(double output, double id, double value) override;
  void log(const std::string& line) override;

  std::unique_ptr<AudScriptContext> context;
  AudScriptValues values;
  std::string name;
  size_t memoryLimit = 0;
  // The sends of the load, which become the values the context starts
  // with; kept only for a runner with a send log - the simulation reports
  // them with the first tick.
  std::vector<AudScriptSend> loadSends;
  bool loading = false;

 private:
  AudScriptRunner* runner_;
};

// ############################################################################
// The runner

// What a load takes.
struct AudScriptLoad {
  std::string source;
  std::string name = "script";
  size_t memoryLimit = kDefaultMemoryLimitMb * kMegabyte;
};

// The load of `source` named `name` (NULL or "": "script") with a heap of
// `memoryLimitMb` MB (0: the default), as a simulation or a bench asks.
AudScriptLoad audScriptLoadOf(const char* source, const char* name,
                              uint32_t memoryLimitMb);

// The times of a load in µs and the bytes the new context holds.
struct AudScriptLoadTimes {
  double createUs = 0;
  double loadUs = 0;
  uint64_t memory = 0;
};

// What a tick did.
struct AudScriptTickResult {
  // Whether a context ran it.
  bool ran = false;
  int32_t status = AUD_SCRIPT_TICK_OK;
  AudScriptGrid grid;
  // The time of the input callbacks and the tick in µs.
  double timeUs = 0;
};

// The longest load: the evaluation of a source stops after it.
constexpr int64_t kLoadBudgetNs = 10000000000LL;

class AudScriptRunner {
 public:
  // Receive the console lines and the errors, prefixed with the script
  // name.
  std::function<void(const std::string&)> onConsole;
  std::function<void(const std::string&)> onError;

  // When set, receives every aud.send of a tick in order; the sends of an
  // aborted tick are taken back.
  std::vector<AudScriptSend>* sendLog = nullptr;

  // Builds a context for `load` and evaluates the source; nullptr with the
  // error, prefixed with the name, when it fails.
  std::unique_ptr<AudScriptState> load(const AudScriptLoad& load,
                                       AudScriptLoadTimes* times,
                                       std::string* error);

  // Makes `state` the current script; the old one is freed.
  void adopt(std::unique_ptr<AudScriptState> state);

  // The current script, or nullptr.
  AudScriptState* state() const { return state_.get(); }

  // Runs the input callbacks of `inputs` and then the tick of `block`
  // within `budgetNs`. A tick that runs over the budget or throws is
  // aborted: the values it sent are undone. An input callback that throws
  // is reported and the next one runs.
  AudScriptTickResult runTick(const AudScriptBlock& block,
                              const AudScriptInput* inputs, size_t count,
                              int64_t budgetNs);

  // [the context] Records a send of the current tick.
  void recordSend(const AudScriptSend& send) {
    if (sendLog != nullptr) sendLog->push_back(send);
  }

  // Reports a console line or an error.
  void console(const std::string& name, const std::string& line);
  void error(const std::string& name, const std::string& message);

 private:
  std::unique_ptr<AudScriptState> state_;
  // Whether the sends of the load of the current state wait for the first
  // tick of the send log.
  bool loadSendsPending_ = false;
};

}  // namespace aud_script

#endif  // AUD_SCRIPT_RUNNER_HPP
