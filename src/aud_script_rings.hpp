// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// The two lock-free single-producer single-consumer rings between the audio
// thread and the worker of a modulator (ticket 31). Neither allocates after
// its constructor, neither takes a lock.
//
// - AudScriptBatchRing carries the requests and the input events from the
//   audio thread to the worker. The producer stages the entries of a block
//   and publishes them at once with the request at their end, so that the
//   worker never sees the events of a block without its request: the
//   events of a block reach the script before the tick of the next block,
//   never before the tick of their own. AudSpscQueue of aud_audio_core
//   publishes every item on its own.
// - AudScriptSlotRing carries the results from the worker to the audio
//   thread in order, written and read in place, since a result holds the
//   values of up to AUD_SCRIPT_MAX_VALUES pairs.

#ifndef AUD_SCRIPT_RINGS_HPP
#define AUD_SCRIPT_RINGS_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace aud_script {

// ############################################################################
// The batch ring

template <typename T>
class AudScriptBatchRing {
 public:
  // A ring of `capacity` entries, rounded up to a power of two.
  explicit AudScriptBatchRing(size_t capacity)
      : capacity_(roundUp(capacity)),
        mask_(capacity_ - 1),
        slots_(new T[capacity_]) {}

  AudScriptBatchRing(const AudScriptBatchRing&) = delete;
  AudScriptBatchRing& operator=(const AudScriptBatchRing&) = delete;

  // [producer] The entries that can still be staged.
  size_t room() const {
    const uint64_t head = head_.load(std::memory_order_acquire);
    return capacity_ - static_cast<size_t>(staged_ - head);
  }

  // [producer] Stages an entry without publishing it; false when the ring
  // is full.
  bool stage(const T& item) {
    if (room() == 0) return false;
    slots_[staged_ & mask_] = item;
    staged_ += 1;
    return true;
  }

  // [producer] Publishes the staged entries.
  void publish() { tail_.store(staged_, std::memory_order_release); }

  // [consumer] Pops a published entry; false when there is none.
  bool pop(T& item) {
    const uint64_t head = head_.load(std::memory_order_relaxed);
    const uint64_t tail = tail_.load(std::memory_order_acquire);
    if (head == tail) return false;
    item = slots_[head & mask_];
    head_.store(head + 1, std::memory_order_release);
    return true;
  }

  size_t capacity() const { return capacity_; }

 private:
  static size_t roundUp(size_t value) {
    size_t result = 1;
    while (result < value) result <<= 1;
    return result;
  }

  const size_t capacity_;
  const size_t mask_;
  std::unique_ptr<T[]> slots_;
  // The producer's own count of staged entries.
  uint64_t staged_ = 0;
  alignas(64) std::atomic<uint64_t> head_{0};
  alignas(64) std::atomic<uint64_t> tail_{0};
};

// ############################################################################
// The slot ring

template <typename T, size_t N>
class AudScriptSlotRing {
 public:
  AudScriptSlotRing() : slots_(new T[N]) {}

  AudScriptSlotRing(const AudScriptSlotRing&) = delete;
  AudScriptSlotRing& operator=(const AudScriptSlotRing&) = delete;

  // [producer] The slot to write next, or nullptr when the ring is full.
  T* writeSlot() {
    const uint64_t tail = tail_.load(std::memory_order_relaxed);
    const uint64_t head = head_.load(std::memory_order_acquire);
    if (tail - head >= N) return nullptr;
    return &slots_[tail % N];
  }

  // [producer] Publishes the slot writeSlot() returned.
  void commit() {
    tail_.store(tail_.load(std::memory_order_relaxed) + 1,
                std::memory_order_release);
  }

  // [consumer] The published slots that wait.
  size_t size() const {
    return static_cast<size_t>(tail_.load(std::memory_order_acquire) -
                               head_.load(std::memory_order_relaxed));
  }

  // [consumer] The slot `index` places behind the oldest one; index below
  // size().
  const T* at(size_t index) const {
    return &slots_[(head_.load(std::memory_order_relaxed) + index) % N];
  }

  // [consumer] Gives the oldest slot back.
  void pop() {
    head_.store(head_.load(std::memory_order_relaxed) + 1,
                std::memory_order_release);
  }

 private:
  std::unique_ptr<T[]> slots_;
  alignas(64) std::atomic<uint64_t> head_{0};
  alignas(64) std::atomic<uint64_t> tail_{0};
};

}  // namespace aud_script

#endif  // AUD_SCRIPT_RINGS_HPP
