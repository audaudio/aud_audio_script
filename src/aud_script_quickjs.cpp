// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// The QuickJS-ng implementation of the engine interface (ticket 31): a
// runtime of its own per context, so that the memory limit and the
// interrupt of one script touch no other; the interrupt handler stops a
// call at its deadline. QuickJS-ng is an interpreter in C without a JIT, so
// it runs the same on every platform, iOS included.
//
// The runtime allocates through malloc functions of the context that count
// the bytes it holds by their usable size, so that the memory of a context
// is a counter to read - JS_ComputeMemoryUsage walks the whole heap, which
// would hold up the worker between two ticks. QuickJS-ng enforces the
// memory limit in its own allocation wrappers, before it calls these
// functions, so JS_SetMemoryLimit works as with its default allocator.

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>

#include "aud_audio_script.h"
#include "aud_clock.h"
#include "aud_script_engine.hpp"
#include "aud_script_heap.hpp"
#include "aud_script_sync.hpp"
#include "quickjs.h"

namespace aud_script {
namespace {

// ############################################################################
// The counting allocator

void* heapCalloc(void* opaque, size_t count, size_t size) {
  return static_cast<AudScriptHeap*>(opaque)->calloc(count, size);
}

void* heapMalloc(void* opaque, size_t size) {
  return static_cast<AudScriptHeap*>(opaque)->malloc(size);
}

void heapFree(void* opaque, void* block) {
  static_cast<AudScriptHeap*>(opaque)->free(block);
}

void* heapRealloc(void* opaque, void* block, size_t size) {
  return static_cast<AudScriptHeap*>(opaque)->realloc(block, size);
}

size_t heapUsableSize(const void* block) {
  return AudScriptHeap::usableSize(block);
}

const JSMallocFunctions kHeapFunctions = {heapCalloc, heapMalloc, heapFree,
                                          heapRealloc, heapUsableSize};

// ############################################################################
// The context

// The fields of aud.time, in the order of AudQuickJsContext::timeAtoms_.
const char* const kTimeFields[] = {"beat",       "tempo",  "playing",
                                   "sampleRate", "frames", "tick"};
constexpr int kNumTimeFields = 6;

class AudQuickJsContext final : public AudScriptContext {
 public:
  AudQuickJsContext(AudScriptSink* sink) : sink_(sink) {}

  ~AudQuickJsContext() override {
    if (ctx_ != nullptr) {
      JS_FreeValue(ctx_, aud_);
      JS_FreeValue(ctx_, time_);
      for (JSAtom atom : timeAtoms_) JS_FreeAtom(ctx_, atom);
      JS_FreeAtom(ctx_, onTick_);
      JS_FreeAtom(ctx_, onControlChange_);
      JS_FreeAtom(ctx_, onControl_);
      JS_FreeContext(ctx_);
    }
    if (rt_ != nullptr) JS_FreeRuntime(rt_);
  }

  // Creates the runtime, the context and the globals; false with a
  // message when memory runs out.
  bool init(size_t memoryLimit, std::string* error) {
    rt_ = JS_NewRuntime2(&kHeapFunctions, &heap_);
    if (rt_ == nullptr)
      return fail("QuickJS-ng has no memory for a runtime", error);
    JS_SetMemoryLimit(rt_, memoryLimit);
    JS_SetMaxStackSize(rt_, kScriptStackBytes);
    JS_SetInterruptHandler(rt_, interrupt, this);
    // Atomics.wait would block the worker.
    JS_SetCanBlock(rt_, false);
    ctx_ = JS_NewContext(rt_);
    if (ctx_ == nullptr)
      return fail("QuickJS-ng has no memory for a context", error);
    JS_SetContextOpaque(ctx_, this);
    for (int i = 0; i < kNumTimeFields; ++i) {
      timeAtoms_[i] = JS_NewAtom(ctx_, kTimeFields[i]);
    }
    onTick_ = JS_NewAtom(ctx_, "onTick");
    onControlChange_ = JS_NewAtom(ctx_, "onControlChange");
    onControl_ = JS_NewAtom(ctx_, "onControl");
    JSValue global = JS_GetGlobalObject(ctx_);
    aud_ = JS_NewObject(ctx_);
    time_ = JS_NewObject(ctx_);
    JSValue console = JS_NewObject(ctx_);
    bool ok = !JS_IsException(aud_) && !JS_IsException(time_) &&
              !JS_IsException(console);
    if (ok) {
      AudScriptTime start;
      ok = setTime(start) &&
           JS_DefinePropertyValueStr(ctx_, aud_, "outputs",
                                     JS_NewInt32(ctx_, AUD_SCRIPT_NUM_OUTPUTS),
                                     JS_PROP_ENUMERABLE) >= 0 &&
           JS_DefinePropertyValueStr(ctx_, aud_, "time",
                                     JS_DupValue(ctx_, time_),
                                     JS_PROP_ENUMERABLE) >= 0 &&
           JS_DefinePropertyValueStr(ctx_, aud_, "send",
                                     JS_NewCFunction(ctx_, send, "send", 3),
                                     JS_PROP_ENUMERABLE) >= 0 &&
           JS_DefinePropertyValueStr(ctx_, global, "aud",
                                     JS_DupValue(ctx_, aud_),
                                     JS_PROP_ENUMERABLE) >= 0;
      for (const char* name : {"log", "info", "warn", "error", "debug"}) {
        ok = ok && JS_SetPropertyStr(ctx_, console, name,
                                     JS_NewCFunction(ctx_, log, name, 1)) >= 0;
      }
      ok = ok && JS_SetPropertyStr(ctx_, global, "console",
                                   JS_DupValue(ctx_, console)) >= 0;
    }
    JS_FreeValue(ctx_, console);
    JS_FreeValue(ctx_, global);
    if (!ok) {
      JS_FreeValue(ctx_, JS_GetException(ctx_));
      return fail("QuickJS-ng has no memory for the globals", error);
    }
    return true;
  }

  int32_t evaluate(const std::string& source, const std::string& name,
                   int64_t deadlineNs, std::string* error) override {
    begin(deadlineNs);
    // QuickJS-ng reads the source up to its terminating zero.
    const JSValue result = JS_Eval(ctx_, source.c_str(), source.size(),
                                   name.c_str(), JS_EVAL_TYPE_GLOBAL);
    return finish(result, error);
  }

  int32_t controlChange(uint32_t channel, uint32_t number, double value,
                        int64_t deadlineNs, std::string* error) override {
    JSValue args[3] = {JS_NewInt32(ctx_, static_cast<int32_t>(channel)),
                       JS_NewInt32(ctx_, static_cast<int32_t>(number)),
                       JS_NewFloat64(ctx_, value)};
    return callHandler(onControlChange_, 3, args, deadlineNs, error);
  }

  int32_t control(uint32_t id, double value, int64_t deadlineNs,
                  std::string* error) override {
    JSValue args[2] = {JS_NewInt32(ctx_, static_cast<int32_t>(id)),
                       JS_NewFloat64(ctx_, value)};
    return callHandler(onControl_, 2, args, deadlineNs, error);
  }

  int32_t tick(const AudScriptTime& time, int64_t deadlineNs,
               std::string* error) override {
    if (!setTime(time)) {
      begin(deadlineNs);
      return finish(JS_EXCEPTION, error);
    }
    return callHandler(onTick_, 0, nullptr, deadlineNs, error);
  }

  void setMemoryLimit(size_t bytes) override { JS_SetMemoryLimit(rt_, bytes); }

  // The counter of the allocator: O(1), so that the worker reads it
  // between two ticks.
  uint64_t memoryUsed() override { return heap_.bytes; }

 private:
  static bool fail(const char* message, std::string* error) {
    if (error != nullptr) *error = message;
    return false;
  }

  // Interrupts a call that runs past its deadline.
  static int interrupt(JSRuntime*, void* opaque) {
    auto* self = static_cast<AudQuickJsContext*>(opaque);
    if (self->deadline_ != 0 && aud_clock_now_ns() > self->deadline_) {
      self->interrupted_ = true;
      return 1;
    }
    return 0;
  }

  // aud.send(output, id, value).
  static JSValue send(JSContext* ctx, JSValueConst, int argc,
                      JSValueConst* argv) {
    auto* self = static_cast<AudQuickJsContext*>(JS_GetContextOpaque(ctx));
    double values[3] = {NAN, NAN, NAN};
    for (int i = 0; i < 3 && i < argc; ++i) {
      if (JS_ToFloat64(ctx, &values[i], argv[i]) < 0) return JS_EXCEPTION;
    }
    const char* message = self->sink_->send(values[0], values[1], values[2]);
    if (message != nullptr) return JS_ThrowRangeError(ctx, "%s", message);
    return JS_UNDEFINED;
  }

  // console.log(...args): the arguments as strings, joined by spaces, cut
  // to kScriptMaxText bytes.
  static JSValue log(JSContext* ctx, JSValueConst, int argc,
                     JSValueConst* argv) {
    auto* self = static_cast<AudQuickJsContext*>(JS_GetContextOpaque(ctx));
    std::string line;
    for (int i = 0; i < argc; ++i) {
      size_t length = 0;
      const char* text = JS_ToCStringLen(ctx, &length, argv[i]);
      if (text == nullptr) return JS_EXCEPTION;
      if (i > 0 && line.size() < kScriptMaxText) line += ' ';
      const size_t room =
          kScriptMaxText - std::min(line.size(), kScriptMaxText);
      line.append(text, audScriptClipLength(text, length, room));
      JS_FreeCString(ctx, text);
    }
    self->sink_->log(line);
    return JS_UNDEFINED;
  }

  void begin(int64_t deadlineNs) {
    deadline_ = deadlineNs;
    interrupted_ = false;
  }

  // Sets the fields of aud.time; false on an exception.
  bool setTime(const AudScriptTime& time) {
    const JSValue values[kNumTimeFields] = {
        JS_NewFloat64(ctx_, time.beat),
        JS_NewFloat64(ctx_, time.tempo),
        JS_NewBool(ctx_, time.playing),
        JS_NewFloat64(ctx_, time.sampleRate),
        JS_NewInt64(ctx_, time.frames),
        JS_NewInt64(ctx_, static_cast<int64_t>(time.tick)),
    };
    for (int i = 0; i < kNumTimeFields; ++i) {
      if (JS_SetProperty(ctx_, time_, timeAtoms_[i], values[i]) < 0) {
        return false;
      }
    }
    return true;
  }

  // Calls the handler `atom` of aud, when the script set a function there.
  int32_t callHandler(JSAtom atom, int argc, JSValue* argv, int64_t deadlineNs,
                      std::string* error) {
    begin(deadlineNs);
    const JSValue handler = JS_GetProperty(ctx_, aud_, atom);
    if (JS_IsException(handler)) return finish(handler, error);
    if (!JS_IsFunction(ctx_, handler)) {
      JS_FreeValue(ctx_, handler);
      return AUD_SCRIPT_TICK_OK;
    }
    const JSValue result = JS_Call(ctx_, handler, aud_, argc, argv);
    JS_FreeValue(ctx_, handler);
    return finish(result, error);
  }

  // Frees `result`, runs the jobs of promises and turns an exception into
  // AUD_SCRIPT_TICK_* with its message.
  int32_t finish(JSValue result, std::string* error) {
    bool failed = JS_IsException(result);
    JS_FreeValue(ctx_, result);
    while (!failed && JS_IsJobPending(rt_)) {
      JSContext* job = nullptr;
      if (JS_ExecutePendingJob(rt_, &job) < 0) failed = true;
    }
    deadline_ = 0;
    if (!failed) return AUD_SCRIPT_TICK_OK;
    const JSValue exception = JS_GetException(ctx_);
    if (error != nullptr) *error = describe(exception);
    JS_FreeValue(ctx_, exception);
    return interrupted_ ? AUD_SCRIPT_TICK_INTERRUPTED : AUD_SCRIPT_TICK_FAILED;
  }

  // The text of an exception with the stack of an error.
  std::string describe(JSValueConst exception) {
    std::string text = stringOf(exception);
    if (JS_IsError(exception)) {
      const JSValue stack = JS_GetPropertyStr(ctx_, exception, "stack");
      if (JS_IsString(stack)) {
        std::string trace = stringOf(stack);
        while (!trace.empty() &&
               (trace.back() == '\n' || trace.back() == ' ')) {
          trace.pop_back();
        }
        if (!trace.empty()) text += "\n" + trace;
      }
      JS_FreeValue(ctx_, stack);
    }
    if (text.empty() || text == "null") text = "out of memory";
    return text;
  }

  std::string stringOf(JSValueConst value) {
    size_t length = 0;
    const char* text = JS_ToCStringLen(ctx_, &length, value);
    if (text == nullptr) {
      JS_FreeValue(ctx_, JS_GetException(ctx_));
      return std::string();
    }
    std::string result(text, length);
    JS_FreeCString(ctx_, text);
    return result;
  }

  AudScriptSink* sink_;
  // The count of the runtime's allocator; JS_FreeRuntime in the destructor
  // frees the runtime's last blocks through it.
  AudScriptHeap heap_;
  JSRuntime* rt_ = nullptr;
  JSContext* ctx_ = nullptr;
  JSValue aud_ = JS_UNDEFINED;
  JSValue time_ = JS_UNDEFINED;
  JSAtom timeAtoms_[kNumTimeFields] = {};
  JSAtom onTick_ = 0;
  JSAtom onControlChange_ = 0;
  JSAtom onControl_ = 0;
  int64_t deadline_ = 0;
  bool interrupted_ = false;
};

}  // namespace

std::unique_ptr<AudScriptContext> audScriptCreateQuickJs(size_t memoryLimit,
                                                         AudScriptSink* sink,
                                                         std::string* error) {
  std::unique_ptr<AudQuickJsContext> context(new (std::nothrow)
                                                 AudQuickJsContext(sink));
  if (context == nullptr) {
    if (error != nullptr) *error = "out of memory";
    return nullptr;
  }
  if (!context->init(memoryLimit, error)) return nullptr;
  return context;
}

}  // namespace aud_script
