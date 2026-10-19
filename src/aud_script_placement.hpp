// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// Where a worker of the package runs (ticket 31, plan check 3): on Android
// and Linux it raises its priority and runs on any core or, as
// "worker_cores" asks, bound to the fast cores; on Apple platforms it asks
// for the quality of service class user-interactive. Any core is the
// default: a worker bound to the fast cores of a big.LITTLE device whose
// idle big cores the kernel isolates waits on the core of the audio thread
// that woke it (aud_audio_script.h, AUD_SCRIPT_CORES_ANY).

#ifndef AUD_SCRIPT_PLACEMENT_HPP
#define AUD_SCRIPT_PLACEMENT_HPP

#include <cstdint>
#include <vector>

namespace aud_script {

// Where the calling thread runs after audScriptPlaceCurrentThread().
struct AudScriptPlacement {
  // Whether the thread asked for the fast cores, AUD_SCRIPT_CORES_FAST.
  bool fastCores = false;
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

// [worker] Places the calling thread and tells where it runs: on Android
// and Linux bound to the fast cores with `fastCores`, else free on any core
// - an earlier binding is lifted -, both with the raised priority; on Apple
// platforms the quality of service class user-interactive either way.
AudScriptPlacement audScriptPlaceCurrentThread(bool fastCores);

// [worker] The core the calling thread runs on, -1 when the platform does
// not tell; cheap enough to ask after every tick.
int32_t audScriptCurrentCpu();

// [worker] Binds the calling thread to its fast cores again when the
// system reset its affinity: Android does that when it moves the app
// between cpusets, to the background and back (io-003). A worker calls it
// about once a second; false when the binding failed.
bool audScriptRefreshPlacement(AudScriptPlacement& placement);

}  // namespace aud_script

#endif  // AUD_SCRIPT_PLACEMENT_HPP
