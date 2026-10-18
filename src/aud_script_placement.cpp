// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

#include "aud_script_placement.hpp"

#include <cstdio>

#include "aud_audio_script.h"

#if __APPLE__
#include <pthread.h>
#include <sys/qos.h>
#elif _WIN32
#include <windows.h>
#else
#include <sched.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace aud_script {

uint64_t audScriptFastCoresOf(const std::vector<long>& maxClocksKhz) {
  long slowest = 0;
  for (const long khz : maxClocksKhz) {
    if (khz > 0 && (slowest == 0 || khz < slowest)) slowest = khz;
  }
  uint64_t fast = 0;
  for (size_t cpu = 0; cpu < maxClocksKhz.size() && cpu < 64; ++cpu) {
    if (maxClocksKhz[cpu] > slowest) fast |= uint64_t{1} << cpu;
  }
  return fast;
}

#if !__APPLE__ && !_WIN32

namespace {

// The highest clock of every core in kHz, from cpufreq; 0 where a core
// does not tell, e.g. while it is offline. As maxClocksKhz() of
// aud_audio_io (aud_io_android.cpp).
std::vector<long> maxClocksKhz() {
  std::vector<long> clocks;
  const long cpus = sysconf(_SC_NPROCESSORS_CONF);
  for (long cpu = 0; cpu < cpus && cpu < 64; ++cpu) {
    char path[96];
    std::snprintf(path, sizeof(path),
                  "/sys/devices/system/cpu/cpu%ld/cpufreq/cpuinfo_max_freq",
                  cpu);
    long khz = 0;
    if (FILE* file = std::fopen(path, "r")) {
      if (std::fscanf(file, "%ld", &khz) != 1) khz = 0;
      std::fclose(file);
    }
    clocks.push_back(khz);
  }
  return clocks;
}

// The id of the calling thread for setpriority.
pid_t threadId() { return static_cast<pid_t>(syscall(SYS_gettid)); }

}  // namespace

AudScriptPlacement audScriptPlaceCurrentThread() {
  AudScriptPlacement placement;
  placement.mode = AUD_SCRIPT_PLACEMENT_SYSTEM;
  const uint64_t fast = audScriptFastCoresOf(maxClocksKhz());
  if (fast != 0) {
    cpu_set_t set;
    CPU_ZERO(&set);
    for (size_t cpu = 0; cpu < 64; ++cpu) {
      if ((fast >> cpu) & 1) CPU_SET(cpu, &set);
    }
    if (sched_setaffinity(0, sizeof(set), &set) == 0) {
      placement.mode = AUD_SCRIPT_PLACEMENT_FAST_CORES;
      placement.cpus = fast;
    } else {
      placement.mode = AUD_SCRIPT_PLACEMENT_REFUSED;
    }
  }
  // The first nice value the system accepts, read back.
  const pid_t tid = threadId();
  for (const int32_t nice : kScriptNiceValues) {
    if (setpriority(PRIO_PROCESS, static_cast<id_t>(tid), nice) == 0 &&
        getpriority(PRIO_PROCESS, static_cast<id_t>(tid)) == nice) {
      placement.priority = nice;
      break;
    }
  }
  return placement;
}

bool audScriptRefreshPlacement(AudScriptPlacement& placement) {
  if (placement.mode != AUD_SCRIPT_PLACEMENT_FAST_CORES) return true;
  cpu_set_t now;
  if (sched_getaffinity(0, sizeof(now), &now) != 0) return false;
  bool holds = true;
  for (size_t cpu = 0; cpu < 64 && holds; ++cpu) {
    // A core outside the fast ones breaks the binding; the kernel may leave
    // out fast cores it isolates, which does not.
    if (CPU_ISSET(cpu, &now) && !((placement.cpus >> cpu) & 1)) holds = false;
  }
  if (holds) return true;
  cpu_set_t set;
  CPU_ZERO(&set);
  for (size_t cpu = 0; cpu < 64; ++cpu) {
    if ((placement.cpus >> cpu) & 1) CPU_SET(cpu, &set);
  }
  if (sched_setaffinity(0, sizeof(set), &set) == 0) return true;
  placement.mode = AUD_SCRIPT_PLACEMENT_REFUSED;
  return false;
}

#elif __APPLE__

AudScriptPlacement audScriptPlaceCurrentThread() {
  AudScriptPlacement placement;
  placement.mode = AUD_SCRIPT_PLACEMENT_SYSTEM;
  if (pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0) == 0) {
    placement.mode = AUD_SCRIPT_PLACEMENT_QOS;
  }
  placement.qos = static_cast<uint32_t>(qos_class_self());
  return placement;
}

bool audScriptRefreshPlacement(AudScriptPlacement&) { return true; }

#else

AudScriptPlacement audScriptPlaceCurrentThread() {
  AudScriptPlacement placement;
  placement.mode = AUD_SCRIPT_PLACEMENT_SYSTEM;
  if (SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST)) {
    placement.priority = THREAD_PRIORITY_HIGHEST;
  }
  return placement;
}

bool audScriptRefreshPlacement(AudScriptPlacement&) { return true; }

#endif

}  // namespace aud_script
