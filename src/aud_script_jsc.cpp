// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// The JavaScriptCore implementation of the engine interface (ticket 31), on
// Apple platforms only, through the public C API of the system framework -
// no private interface. That API has neither a memory limit nor a time
// budget (both exist only as private interfaces), so a script that runs
// away blocks the worker and its heap grows until the system stops the
// app; and it does not tell the memory a context holds. The engine exists
// for the comparison with QuickJS-ng on the engine an app on iOS ran
// before, and as a fallback. Apps get no JIT on iOS, so it interprets
// there too.

#include <cmath>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "aud_audio_script.h"
#include "aud_script_engine.hpp"

#if __APPLE__

#include <JavaScriptCore/JavaScript.h>

namespace aud_script {
namespace {

// The longest console line, in bytes.
constexpr size_t kMaxLine = 4096;

const char* const kTimeFields[] = {"beat",       "tempo",  "playing",
                                   "sampleRate", "frames", "tick"};
constexpr int kNumTimeFields = 6;

// A JSStringRef released at the end of its scope.
class AudJsString {
 public:
  explicit AudJsString(const char* text)
      : string_(JSStringCreateWithUTF8CString(text)) {}
  ~AudJsString() { JSStringRelease(string_); }
  AudJsString(const AudJsString&) = delete;
  AudJsString& operator=(const AudJsString&) = delete;
  JSStringRef get() const { return string_; }

 private:
  JSStringRef string_;
};

// The UTF-8 text of a JSStringRef.
std::string utf8Of(JSStringRef string) {
  const size_t capacity = JSStringGetMaximumUTF8CStringSize(string);
  std::vector<char> buffer(capacity > 0 ? capacity : 1);
  const size_t written =
      JSStringGetUTF8CString(string, buffer.data(), buffer.size());
  return std::string(buffer.data(), written > 0 ? written - 1 : 0);
}

class AudJscContext final : public AudScriptContext {
 public:
  explicit AudJscContext(AudScriptSink* sink) : sink_(sink) {}

  ~AudJscContext() override {
    if (ctx_ != nullptr) {
      if (aud_ != nullptr) JSValueUnprotect(ctx_, aud_);
      if (time_ != nullptr) JSValueUnprotect(ctx_, time_);
      JSGlobalContextRelease(ctx_);
    }
    for (JSStringRef name : timeNames_) {
      if (name != nullptr) JSStringRelease(name);
    }
    for (JSStringRef name : {onTick_, onControlChange_, onControl_}) {
      if (name != nullptr) JSStringRelease(name);
    }
    if (functionClass_ != nullptr) JSClassRelease(functionClass_);
    if (group_ != nullptr) JSContextGroupRelease(group_);
  }

  bool init(std::string* error) {
    group_ = JSContextGroupCreate();
    if (group_ == nullptr) return fail(error);
    ctx_ = JSGlobalContextCreateInGroup(group_, nullptr);
    if (ctx_ == nullptr) return fail(error);
    for (int i = 0; i < kNumTimeFields; ++i) {
      timeNames_[i] = JSStringCreateWithUTF8CString(kTimeFields[i]);
    }
    onTick_ = JSStringCreateWithUTF8CString("onTick");
    onControlChange_ = JSStringCreateWithUTF8CString("onControlChange");
    onControl_ = JSStringCreateWithUTF8CString("onControl");
    // A function whose object carries the context, so that aud.send and
    // console.log find it.
    JSClassDefinition definition = kJSClassDefinitionEmpty;
    definition.className = "AudFunction";
    definition.callAsFunction = call;
    functionClass_ = JSClassCreate(&definition);
    const JSPropertyAttributes fixed =
        kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete;
    aud_ = JSObjectMake(ctx_, nullptr, nullptr);
    JSValueProtect(ctx_, aud_);
    time_ = JSObjectMake(ctx_, nullptr, nullptr);
    JSValueProtect(ctx_, time_);
    AudScriptTime start;
    setTime(start);
    set(aud_, "outputs", JSValueMakeNumber(ctx_, AUD_SCRIPT_NUM_OUTPUTS),
        fixed);
    set(aud_, "time", time_, fixed);
    set(aud_, "send", function(kSend), fixed);
    JSObjectRef global = JSContextGetGlobalObject(ctx_);
    set(global, "aud", aud_, fixed);
    JSObjectRef console = JSObjectMake(ctx_, nullptr, nullptr);
    for (const char* name : {"log", "info", "warn", "error", "debug"}) {
      set(console, name, function(kLog), kJSPropertyAttributeNone);
    }
    set(global, "console", console, kJSPropertyAttributeNone);
    return true;
  }

  int32_t evaluate(const std::string& source, const std::string& name, int64_t,
                   std::string* error) override {
    AudJsString script(source.c_str());
    AudJsString url(name.c_str());
    JSValueRef exception = nullptr;
    JSEvaluateScript(ctx_, script.get(), nullptr, url.get(), 1, &exception);
    return finish(exception, error);
  }

  int32_t controlChange(uint32_t channel, uint32_t number, double value,
                        int64_t, std::string* error) override {
    const JSValueRef args[3] = {JSValueMakeNumber(ctx_, channel),
                                JSValueMakeNumber(ctx_, number),
                                JSValueMakeNumber(ctx_, value)};
    return callHandler(onControlChange_, 3, args, error);
  }

  int32_t control(uint32_t id, double value, int64_t,
                  std::string* error) override {
    const JSValueRef args[2] = {JSValueMakeNumber(ctx_, id),
                                JSValueMakeNumber(ctx_, value)};
    return callHandler(onControl_, 2, args, error);
  }

  int32_t tick(const AudScriptTime& time, int64_t,
               std::string* error) override {
    setTime(time);
    return callHandler(onTick_, 0, nullptr, error);
  }

  void setMemoryLimit(size_t) override {}

  uint64_t memoryUsed() override { return 0; }

 private:
  // The functions the class implements, by their private data.
  enum Function : uintptr_t { kSend = 1, kLog = 2 };

  // The private data of a function: the context and which function.
  struct Callee {
    AudJscContext* self;
    Function function;
  };

  static bool fail(std::string* error) {
    if (error != nullptr) *error = "JavaScriptCore has no memory for a context";
    return false;
  }

  JSObjectRef function(Function which) {
    callees_[which == kSend ? 0 : 1] = Callee{this, which};
    return JSObjectMake(ctx_, functionClass_,
                        &callees_[which == kSend ? 0 : 1]);
  }

  void set(JSObjectRef object, const char* name, JSValueRef value,
           JSPropertyAttributes attributes) {
    AudJsString key(name);
    JSObjectSetProperty(ctx_, object, key.get(), value, attributes, nullptr);
  }

  void setTime(const AudScriptTime& time) {
    const JSValueRef values[kNumTimeFields] = {
        JSValueMakeNumber(ctx_, time.beat),
        JSValueMakeNumber(ctx_, time.tempo),
        JSValueMakeBoolean(ctx_, time.playing),
        JSValueMakeNumber(ctx_, time.sampleRate),
        JSValueMakeNumber(ctx_, time.frames),
        JSValueMakeNumber(ctx_, static_cast<double>(time.tick)),
    };
    for (int i = 0; i < kNumTimeFields; ++i) {
      JSObjectSetProperty(ctx_, time_, timeNames_[i], values[i],
                          kJSPropertyAttributeNone, nullptr);
    }
  }

  // aud.send and console.log.
  static JSValueRef call(JSContextRef ctx, JSObjectRef function, JSObjectRef,
                         size_t argc, const JSValueRef argv[],
                         JSValueRef* exception) {
    const auto* callee =
        static_cast<const Callee*>(JSObjectGetPrivate(function));
    if (callee->function == kSend) {
      double values[3] = {NAN, NAN, NAN};
      for (size_t i = 0; i < 3 && i < argc; ++i) {
        values[i] = JSValueToNumber(ctx, argv[i], exception);
        if (*exception != nullptr) return nullptr;
      }
      const char* message =
          callee->self->sink_->send(values[0], values[1], values[2]);
      if (message != nullptr) {
        *exception = rangeError(ctx, message);
        return nullptr;
      }
      return JSValueMakeUndefined(ctx);
    }
    std::string line;
    for (size_t i = 0; i < argc; ++i) {
      JSStringRef text = JSValueToStringCopy(ctx, argv[i], exception);
      if (*exception != nullptr) return nullptr;
      if (i > 0) line += ' ';
      if (line.size() < kMaxLine) line += utf8Of(text);
      JSStringRelease(text);
    }
    callee->self->sink_->log(line);
    return JSValueMakeUndefined(ctx);
  }

  // A RangeError with `message`.
  // A RangeError with `message`, made by the context's own constructor so
  // that `instanceof RangeError` holds; a plain Error when the script
  // replaced the constructor.
  static JSValueRef rangeError(JSContextRef ctx, const char* message) {
    AudJsString text(message);
    const JSValueRef args[1] = {JSValueMakeString(ctx, text.get())};
    AudJsString name("RangeError");
    const JSValueRef constructor = JSObjectGetProperty(
        ctx, JSContextGetGlobalObject(ctx), name.get(), nullptr);
    if (constructor != nullptr && JSValueIsObject(ctx, constructor)) {
      JSObjectRef object = JSValueToObject(ctx, constructor, nullptr);
      if (JSObjectIsConstructor(ctx, object)) {
        JSObjectRef error =
            JSObjectCallAsConstructor(ctx, object, 1, args, nullptr);
        if (error != nullptr) return error;
      }
    }
    return JSObjectMakeError(ctx, 1, args, nullptr);
  }

  int32_t callHandler(JSStringRef name, size_t argc, const JSValueRef* argv,
                      std::string* error) {
    JSValueRef exception = nullptr;
    const JSValueRef handler =
        JSObjectGetProperty(ctx_, aud_, name, &exception);
    if (exception != nullptr) return finish(exception, error);
    if (!JSValueIsObject(ctx_, handler)) return AUD_SCRIPT_TICK_OK;
    JSObjectRef object = JSValueToObject(ctx_, handler, nullptr);
    if (object == nullptr || !JSObjectIsFunction(ctx_, object)) {
      return AUD_SCRIPT_TICK_OK;
    }
    JSObjectCallAsFunction(ctx_, object, aud_, argc, argv, &exception);
    return finish(exception, error);
  }

  int32_t finish(JSValueRef exception, std::string* error) {
    if (exception == nullptr) return AUD_SCRIPT_TICK_OK;
    if (error != nullptr) *error = describe(exception);
    return AUD_SCRIPT_TICK_FAILED;
  }

  // The text of an exception with the stack of an error.
  std::string describe(JSValueRef exception) {
    std::string text = stringOf(exception);
    if (JSValueIsObject(ctx_, exception)) {
      JSObjectRef object = JSValueToObject(ctx_, exception, nullptr);
      AudJsString key("stack");
      const JSValueRef stack =
          JSObjectGetProperty(ctx_, object, key.get(), nullptr);
      if (stack != nullptr && JSValueIsString(ctx_, stack)) {
        const std::string trace = stringOf(stack);
        if (!trace.empty()) text += "\n" + trace;
      }
    }
    return text;
  }

  std::string stringOf(JSValueRef value) {
    JSStringRef string = JSValueToStringCopy(ctx_, value, nullptr);
    if (string == nullptr) return std::string();
    std::string text = utf8Of(string);
    JSStringRelease(string);
    return text;
  }

  AudScriptSink* sink_;
  JSContextGroupRef group_ = nullptr;
  JSGlobalContextRef ctx_ = nullptr;
  JSClassRef functionClass_ = nullptr;
  JSObjectRef aud_ = nullptr;
  JSObjectRef time_ = nullptr;
  JSStringRef timeNames_[kNumTimeFields] = {};
  JSStringRef onTick_ = nullptr;
  JSStringRef onControlChange_ = nullptr;
  JSStringRef onControl_ = nullptr;
  Callee callees_[2] = {};
};

}  // namespace

std::unique_ptr<AudScriptContext> audScriptCreateJavaScriptCore(
    AudScriptSink* sink, std::string* error) {
  std::unique_ptr<AudJscContext> context(new (std::nothrow)
                                             AudJscContext(sink));
  if (context == nullptr || !context->init(error)) return nullptr;
  return context;
}

}  // namespace aud_script

#else

namespace aud_script {

std::unique_ptr<AudScriptContext> audScriptCreateJavaScriptCore(
    AudScriptSink*, std::string* error) {
  if (error != nullptr)
    *error = "JavaScriptCore exists on Apple platforms only";
  return nullptr;
}

}  // namespace aud_script

#endif
