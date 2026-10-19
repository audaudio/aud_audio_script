// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// The counting allocator of a QuickJS-ng runtime (ticket 31): malloc,
// calloc, realloc and free of the system that keep the bytes a runtime
// holds by their usable size, so that the memory of a context is a counter
// to read - JS_ComputeMemoryUsage walks the whole heap, which would hold up
// the worker between two ticks. A runtime runs on one thread at a time, so
// a plain counter does. Header only, for the adapter and its tests.

#ifndef AUD_SCRIPT_HEAP_HPP
#define AUD_SCRIPT_HEAP_HPP

#include <cstddef>
#include <cstdlib>

#if __APPLE__
#include <malloc/malloc.h>
#elif _WIN32 || __linux__ || __ANDROID__
#include <malloc.h>
#endif

namespace aud_script {

// The bytes a runtime holds from the system's allocator.
struct AudScriptHeap {
  size_t bytes = 0;

  // The usable size of a block of the system's allocator; 0 where the
  // platform does not tell, which leaves the count at 0.
  static size_t usableSize(const void* block) {
#if __APPLE__
    return malloc_size(block);
#elif _WIN32
    return _msize(const_cast<void*>(block));
#elif __linux__ || __ANDROID__
    return malloc_usable_size(const_cast<void*>(block));
#else
    (void)block;
    return 0;
#endif
  }

  void* calloc(size_t count, size_t size) {
    void* block = std::calloc(count, size);
    if (block != nullptr) bytes += usableSize(block);
    return block;
  }

  void* malloc(size_t size) {
    void* block = std::malloc(size);
    if (block != nullptr) bytes += usableSize(block);
    return block;
  }

  void free(void* block) {
    if (block == nullptr) return;
    bytes -= usableSize(block);
    std::free(block);
  }

  // A realloc to 0 bytes frees the block and returns nullptr, as glibc and
  // bionic do; a failed realloc keeps the old block and its count.
  void* realloc(void* block, size_t size) {
    if (size == 0) {
      free(block);
      return nullptr;
    }
    const size_t old = block != nullptr ? usableSize(block) : 0;
    void* moved = std::realloc(block, size);
    if (moved != nullptr) bytes += usableSize(moved) - old;
    return moved;
  }
};

}  // namespace aud_script

#endif  // AUD_SCRIPT_HEAP_HPP
