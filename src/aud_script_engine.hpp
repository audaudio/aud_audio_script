// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// The thin engine interface of the package (ticket 31), internal: a script
// context that evaluates sources in its global scope, calls the handlers a
// script set on `aud` and reports `aud.send` and `console.log` to a sink.
// QuickJS-ng is its only implementation, on every platform, with a runtime
// per context, a memory limit and an interrupt for the time budget; another
// engine could come back behind it. A context belongs to the thread that
// created it.
//
// The global scope a script sees:
//
//   aud.outputs                    16
//   aud.time                       beat, tempo, playing, sampleRate, frames,
//                                  tick; set before every aud.onTick()
//   aud.onTick = function () {}    called once per block
//   aud.onControlChange = function (channel, number, value) {}
//   aud.onControl = function (id, value) {}
//   aud.send(output, id, value)    output 0..15, id 0..511, a finite value
//   console.log(...args)           also info, warn, error and debug

#ifndef AUD_SCRIPT_ENGINE_HPP
#define AUD_SCRIPT_ENGINE_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace aud_script {

// What a context reports while a script runs.
class AudScriptSink {
 public:
  virtual ~AudScriptSink() = default;

  // aud.send(output, id, value) with the arguments as numbers; nullptr
  // when the send is taken, else the message of the RangeError the context
  // throws.
  virtual const char* send(double output, double id, double value) = 0;

  // console.log with its arguments joined by spaces.
  virtual void log(const std::string& line) = 0;
};

// The fields of aud.time.
struct AudScriptTime {
  double beat = 0;
  double tempo = 0;
  bool playing = false;
  double sampleRate = 0;
  uint32_t frames = 0;
  uint64_t tick = 0;
};

// A context: one load of a script.
class AudScriptContext {
 public:
  virtual ~AudScriptContext() = default;

  // Evaluates `source` in the global scope, sloppy mode, as the file
  // `name`; AUD_SCRIPT_TICK_* and the message of a failure in `error`. The
  // calls stop at `deadlineNs` of aud_clock_now_ns(), 0 for none.
  virtual int32_t evaluate(const std::string& source, const std::string& name,
                           int64_t deadlineNs, std::string* error) = 0;

  // Calls aud.onControlChange(channel, number, value), aud.onControl(id,
  // value) or aud.onTick() with aud.time set to `time`, if the script set
  // it; AUD_SCRIPT_TICK_*.
  virtual int32_t controlChange(uint32_t channel, uint32_t number, double value,
                                int64_t deadlineNs, std::string* error) = 0;
  virtual int32_t control(uint32_t id, double value, int64_t deadlineNs,
                          std::string* error) = 0;
  virtual int32_t tick(const AudScriptTime& time, int64_t deadlineNs,
                       std::string* error) = 0;

  // The heap limit in bytes; ignored by an engine without one.
  virtual void setMemoryLimit(size_t bytes) = 0;

  // The bytes the context holds; 0 when the engine does not tell.
  virtual uint64_t memoryUsed() = 0;
};

// Creates a context that reports to `sink`, with a heap of `memoryLimit`
// bytes; nullptr with a message in `error` when memory runs out. The
// engine behind it: QuickJS-ng.
std::unique_ptr<AudScriptContext> audScriptCreateContext(size_t memoryLimit,
                                                         AudScriptSink* sink,
                                                         std::string* error);

// The QuickJS-ng implementation.
std::unique_ptr<AudScriptContext> audScriptCreateQuickJs(size_t memoryLimit,
                                                         AudScriptSink* sink,
                                                         std::string* error);

// The message of a send with invalid arguments, or nullptr: the output an
// integer from 0 to 15, the id one from 0 to 511, the value finite.
const char* audScriptCheckSend(double output, double id, double value);

// The longest console line and error text the package keeps, in bytes.
constexpr size_t kScriptMaxText = 4096;

// How many of the `length` bytes of `text` fit into `limit` bytes without
// splitting a UTF-8 sequence.
size_t audScriptClipLength(const char* text, size_t length, size_t limit);

}  // namespace aud_script

#endif  // AUD_SCRIPT_ENGINE_HPP
