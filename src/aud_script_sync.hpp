// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// What the worker of a modulator and the audio thread share (ticket 31): a
// counting semaphore the audio thread posts without blocking, a thread with
// a stack of its own size, and the switch of Clang's RealtimeSanitizer
// around the one wait the audio thread does - for the worker of an offline
// render. Header only, one implementation per platform;
// std::counting_semaphore needs C++20, and std::thread takes no stack size.
// The semaphore follows aud_audio_graph's aud_semaphore.hpp, which is not
// exported, and adds the timed wait.

#ifndef AUD_SCRIPT_SYNC_HPP
#define AUD_SCRIPT_SYNC_HPP

#include <cstddef>
#include <cstdint>

#if _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif __APPLE__
#include <dispatch/dispatch.h>
#include <pthread.h>
#else
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#endif

#if defined(__has_feature)
#if __has_feature(realtime_sanitizer)
#include <sanitizer/rtsan_interface.h>
#define AUD_SCRIPT_RTSAN 1
#endif
#endif

namespace aud_script {

// ############################################################################
// The semaphore

// A counting semaphore: post() never blocks and takes no lock, so that the
// audio thread may wake the worker (script-001).
class AudScriptSemaphore {
 public:
  AudScriptSemaphore() {
#if _WIN32
    handle_ = CreateSemaphoreA(nullptr, 0, 0x7fffffff, nullptr);
#elif __APPLE__
    handle_ = dispatch_semaphore_create(0);
#else
    sem_init(&handle_, 0, 0);
#endif
  }

  ~AudScriptSemaphore() {
#if _WIN32
    CloseHandle(handle_);
#elif __APPLE__
    // A dispatch semaphore must not be released below its initial count.
    while (dispatch_semaphore_wait(handle_, DISPATCH_TIME_NOW) == 0) {
    }
    dispatch_release(handle_);
#else
    sem_destroy(&handle_);
#endif
  }

  AudScriptSemaphore(const AudScriptSemaphore&) = delete;
  AudScriptSemaphore& operator=(const AudScriptSemaphore&) = delete;

  // [realtime] Increments the count and wakes a waiter; never blocks.
  void post() {
#if _WIN32
    ReleaseSemaphore(handle_, 1, nullptr);
#elif __APPLE__
    dispatch_semaphore_signal(handle_);
#else
    sem_post(&handle_);
#endif
  }

  // Waits until the count is positive or `timeoutNs` passed; true when the
  // count was taken.
  bool waitFor(int64_t timeoutNs) {
    if (timeoutNs < 0) timeoutNs = 0;
#if _WIN32
    return WaitForSingleObject(handle_,
                               static_cast<DWORD>(timeoutNs / 1000000)) ==
           WAIT_OBJECT_0;
#elif __APPLE__
    return dispatch_semaphore_wait(
               handle_, dispatch_time(DISPATCH_TIME_NOW, timeoutNs)) == 0;
#else
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += static_cast<time_t>(timeoutNs / 1000000000);
    deadline.tv_nsec += static_cast<long>(timeoutNs % 1000000000);
    if (deadline.tv_nsec >= 1000000000) {
      deadline.tv_sec += 1;
      deadline.tv_nsec -= 1000000000;
    }
    while (sem_timedwait(&handle_, &deadline) != 0) {
      if (errno != EINTR) return false;
    }
    return true;
#endif
  }

  // Takes the count without waiting; true when it was positive.
  bool tryWait() { return waitFor(0); }

 private:
#if _WIN32
  HANDLE handle_;
#elif __APPLE__
  dispatch_semaphore_t handle_;
#else
  sem_t handle_;
#endif
};

// ############################################################################
// The thread

// The stack of a worker: QuickJS-ng checks its own stack against a limit
// of kScriptStackBytes; the thread has twice as much, so that the native
// frames below the scripts and a sanitizer's larger frames fit.
constexpr size_t kScriptStackBytes = 2u * 1024u * 1024u;
constexpr size_t kThreadStackBytes = 2u * kScriptStackBytes;

// A joinable thread with a stack of kThreadStackBytes.
class AudScriptThread {
 public:
  AudScriptThread() = default;
  AudScriptThread(const AudScriptThread&) = delete;
  AudScriptThread& operator=(const AudScriptThread&) = delete;
  ~AudScriptThread() { join(); }

  // [control] Starts `run(user)` on a new thread; false when the system
  // refuses the thread.
  bool start(void (*run)(void*), void* user) {
    if (started_) return false;
    run_ = run;
    user_ = user;
#if _WIN32
    handle_ = CreateThread(nullptr, kThreadStackBytes, entry, this,
                           STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    started_ = handle_ != nullptr;
#else
    pthread_attr_t attributes;
    if (pthread_attr_init(&attributes) != 0) return false;
    pthread_attr_setstacksize(&attributes, kThreadStackBytes);
    started_ = pthread_create(&thread_, &attributes, entry, this) == 0;
    pthread_attr_destroy(&attributes);
#endif
    return started_;
  }

  // [control] Waits for the thread to end.
  void join() {
    if (!started_) return;
#if _WIN32
    WaitForSingleObject(handle_, INFINITE);
    CloseHandle(handle_);
#else
    pthread_join(thread_, nullptr);
#endif
    started_ = false;
  }

 private:
#if _WIN32
  static DWORD WINAPI entry(LPVOID self) {
    auto* thread = static_cast<AudScriptThread*>(self);
    thread->run_(thread->user_);
    return 0;
  }
  HANDLE handle_ = nullptr;
#else
  static void* entry(void* self) {
    auto* thread = static_cast<AudScriptThread*>(self);
    thread->run_(thread->user_);
    return nullptr;
  }
  pthread_t thread_{};
#endif
  void (*run_)(void*) = nullptr;
  void* user_ = nullptr;
  bool started_ = false;
};

// ############################################################################
// The RealtimeSanitizer

// Switches Clang's RealtimeSanitizer off for its scope: around the wait of
// an offline render for its worker, which a realtime render never does.
struct AudScriptRealtimeExempt {
#if AUD_SCRIPT_RTSAN
  AudScriptRealtimeExempt() { __rtsan_disable(); }
  ~AudScriptRealtimeExempt() { __rtsan_enable(); }
#else
  AudScriptRealtimeExempt() {}
#endif
  AudScriptRealtimeExempt(const AudScriptRealtimeExempt&) = delete;
  AudScriptRealtimeExempt& operator=(const AudScriptRealtimeExempt&) = delete;
};

}  // namespace aud_script

#endif  // AUD_SCRIPT_SYNC_HPP
