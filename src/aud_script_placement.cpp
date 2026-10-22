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
#ifndef NOMINMAX
#define NOMINMAX
#endif
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

AudScriptPlacement audScriptPlaceCurrentThread(bool fastCores) {
  AudScriptPlacement placement;
  placement.fastCores = fastCores;
  placement.mode = AUD_SCRIPT_PLACEMENT_SYSTEM;
  const uint64_t fast = fastCores ? audScriptFastCoresOf(maxClocksKhz()) : 0;
  if (!fastCores) {
    // Every core the system has; the kernel keeps the cpuset of the app.
    cpu_set_t set;
    CPU_ZERO(&set);
    const long cpus = sysconf(_SC_NPROCESSORS_CONF);
    for (long cpu = 0; cpu < cpus && cpu < CPU_SETSIZE; ++cpu) {
      CPU_SET(cpu, &set);
    }
    placement.mode = sched_setaffinity(0, sizeof(set), &set) == 0
                         ? AUD_SCRIPT_PLACEMENT_ANY_CORE
                         : AUD_SCRIPT_PLACEMENT_REFUSED;
  } else if (fast != 0) {
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

int32_t audScriptCurrentCpu() { return sched_getcpu(); }

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

AudScriptPlacement audScriptPlaceCurrentThread(bool fastCores) {
  AudScriptPlacement placement;
  placement.fastCores = fastCores;
  placement.mode = AUD_SCRIPT_PLACEMENT_SYSTEM;
  if (pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0) == 0) {
    placement.mode = AUD_SCRIPT_PLACEMENT_QOS;
  }
  placement.qos = static_cast<uint32_t>(qos_class_self());
  return placement;
}

int32_t audScriptCurrentCpu() {
  size_t cpu = 0;
  if (__builtin_available(macOS 11.0, iOS 14.2, *)) {
    if (pthread_cpu_number_np(&cpu) == 0) return static_cast<int32_t>(cpu);
  }
  return -1;
}

bool audScriptRefreshPlacement(AudScriptPlacement&) { return true; }

#else

namespace {

// The efficiency class of every logical processor of group 0, the first 64
// cores; on hybrid processors the performance cores have the higher class,
// elsewhere all are 0 (ticket 38).
std::vector<long> efficiencyClasses() {
  std::vector<long> classes;
  ULONG size = 0;
  GetSystemCpuSetInformation(nullptr, 0, &size, GetCurrentProcess(), 0);
  if (size == 0) return classes;
  std::vector<uint8_t> buffer(size);
  auto* first = reinterpret_cast<SYSTEM_CPU_SET_INFORMATION*>(buffer.data());
  if (!GetSystemCpuSetInformation(first, size, &size, GetCurrentProcess(), 0)) {
    return classes;
  }
  for (ULONG offset = 0; offset < size;) {
    const auto* info =
        reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION*>(buffer.data() + offset);
    if (info->Type == CpuSetInformation && info->CpuSet.Group == 0 &&
        info->CpuSet.LogicalProcessorIndex < 64) {
      const size_t cpu = info->CpuSet.LogicalProcessorIndex;
      if (classes.size() <= cpu) classes.resize(cpu + 1, 0);
      // One more than the class, so that the slowest class is no 0, which
      // audScriptFastCoresOf() takes for a core that tells nothing.
      classes[cpu] = static_cast<long>(info->CpuSet.EfficiencyClass) + 1;
    }
    offset += info->Size;
  }
  return classes;
}

}  // namespace

AudScriptPlacement audScriptPlaceCurrentThread(bool fastCores) {
  AudScriptPlacement placement;
  placement.fastCores = fastCores;
  placement.mode = AUD_SCRIPT_PLACEMENT_SYSTEM;
  DWORD_PTR processMask = 0;
  DWORD_PTR systemMask = 0;
  GetProcessAffinityMask(GetCurrentProcess(), &processMask, &systemMask);
  const uint64_t fast =
      fastCores ? audScriptFastCoresOf(efficiencyClasses()) & processMask : 0;
  if (!fastCores) {
    // Every core of the process.
    placement.mode = SetThreadAffinityMask(GetCurrentThread(), processMask) != 0
                         ? AUD_SCRIPT_PLACEMENT_ANY_CORE
                         : AUD_SCRIPT_PLACEMENT_REFUSED;
  } else if (fast != 0) {
    // The performance cores of a hybrid processor; without them Windows
    // places the thread.
    if (SetThreadAffinityMask(GetCurrentThread(),
                              static_cast<DWORD_PTR>(fast)) != 0) {
      placement.mode = AUD_SCRIPT_PLACEMENT_FAST_CORES;
      placement.cpus = fast;
    } else {
      placement.mode = AUD_SCRIPT_PLACEMENT_REFUSED;
    }
  }
  if (SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST)) {
    placement.priority = THREAD_PRIORITY_HIGHEST;
  }
  return placement;
}

int32_t audScriptCurrentCpu() {
  return static_cast<int32_t>(GetCurrentProcessorNumber());
}

bool audScriptRefreshPlacement(AudScriptPlacement&) { return true; }

#endif

}  // namespace aud_script
