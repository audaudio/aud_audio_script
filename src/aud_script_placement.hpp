// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// Where a worker of the package runs (ticket 31, plan check 3): on Android
// it binds itself to the fast cores and raises its priority, on Apple
// platforms it asks for the quality of service class user-interactive. The
// audio thread of aud_audio_io runs on the fast cores since io-003; a
// worker left where the system puts it lands on the small cores of a
// big.LITTLE device and misses the blocks.

#ifndef AUD_SCRIPT_PLACEMENT_HPP
#define AUD_SCRIPT_PLACEMENT_HPP

#include <cstdint>
#include <vector>

namespace aud_script {

// Where the calling thread runs after audScriptPlaceCurrentThread().
struct AudScriptPlacement {
  // AUD_SCRIPT_PLACEMENT_*.
  uint32_t mode = 0;
  // The nice value that held: -19, -16 or -10, or 0 when none did
  // (Android and Linux); the thread priority on Windows.
  int32_t priority = 0;
  // The cores the thread is bound to, a bit per core.
  uint64_t cpus = 0;
  // The QOS_CLASS_* value on Apple platforms.
  uint32_t qos = 0;
};

// The fast cores of a device from the highest clock of each core in kHz:
// the cores above the slowest clock, a bit per core. A core that tells no
// clock (0, e.g. offline) is no fast core; one cluster has none. A copy of
// fastCoresOf() of aud_audio_io (aud_io_internal.hpp, io-003), which the
// package does not export.
uint64_t audScriptFastCoresOf(const std::vector<long>& maxClocksKhz);

// The nice values a worker tries in turn on Android and Linux: the urgent
// audio priority of Android, its audio priority, then a display priority.
constexpr int32_t kScriptNiceValues[] = {-19, -16, -10};

// [worker] Places the calling thread and tells where it runs.
AudScriptPlacement audScriptPlaceCurrentThread();

// [worker] Binds the calling thread to its fast cores again when the
// system reset its affinity: Android does that when it moves the app
// between cpusets, to the background and back (io-003). A worker calls it
// about once a second; false when the binding failed.
bool audScriptRefreshPlacement(AudScriptPlacement& placement);

}  // namespace aud_script

#endif  // AUD_SCRIPT_PLACEMENT_HPP
