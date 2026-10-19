// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// The entry points of aud_audio_script (ticket 31): the node
// aud.script.modulator, which runs JavaScript on a worker thread of its own
// and sends controls on its event outputs once per audio block; the
// statistics of all modulators of the process; a simulation that runs a
// script on the same host code without a graph; and a bench that times the
// ticks of a script on a worker thread.
//
// The node never runs JavaScript on the audio thread. At the end of the
// process call of block n the audio thread hands the worker the request
// for block n + L - L the lookahead, 2 by default - and the input events
// of block n through a lock-free ring and signals a semaphore without
// blocking; the worker runs the ticks in order and puts the values into a
// second lock-free ring; the process call of block n + L emits the values
// that changed. A late worker holds the values; an offline render waits
// for it.
//
// The tick grid, identical in the node, the simulation and every reference
// of it: for a block that starts at the beat b0 with N frames at the
// sample rate sr and the tempo T while the transport plays,
//
//   beatsPerBlock = N * T / 60 / sr
//   fractions     = ceil(1 / beatsPerBlock)
//   d             = 1 / fractions
//   g             = floor((b0 + beatsPerBlock) / d) * d   (aud.time.beat)
//   k             = floor((g - b0) * 60 / T * sr)         (frame of g)
//   k             = 0 when k >= N or k < 0
//
// and g = b0, k = 0 while the transport stands.

#ifndef AUD_AUDIO_SCRIPT_H
#define AUD_AUDIO_SCRIPT_H

#include <stddef.h>
#include <stdint.h>

#include "aud_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

// ............................................................................
// The node

#define AUD_SCRIPT_MODULATOR_TYPE_ID "aud.script.modulator"

// The event outputs of the node: out0 to out15.
#define AUD_SCRIPT_NUM_OUTPUTS 16

// The control ids a script sends on an output: 0 to 511, the numeric
// controls a sampler maps onto its controllers.
#define AUD_SCRIPT_NUM_IDS 512

// The values one context keeps: the (output, id) pairs a script may have
// sent; a send to a pair beyond them throws a RangeError.
#define AUD_SCRIPT_MAX_VALUES 1024

// Parameters by index; their ids are stable.
enum {
  // "mode0" to "mode15": how the values of an output enter the block,
  // AUD_SCRIPT_MODE_RAMP (default) or AUD_SCRIPT_MODE_STEP.
  AUD_SCRIPT_PARAM_MODE0 = 0,
  // "memory_limit": the heap of a script context in MB, 1 to 1024;
  // default 32. A change applies to the running context at its next tick.
  AUD_SCRIPT_PARAM_MEMORY_LIMIT = 16,
  // "tick_budget": the longest tick in ms, 1 to 10000; default 100. A tick
  // that runs longer is interrupted and counted as aborted; its values are
  // dropped.
  AUD_SCRIPT_PARAM_TICK_BUDGET = 17,
  // "lookahead": how many blocks ahead the worker computes, 1 to 4;
  // default 2. At the end of block n the audio thread requests block
  // n + lookahead, so the worker has lookahead - 1 blocks of slack, e.g.
  // for callbacks that come back to back after a late one. The input events
  // of block n reach the tick of block n + lookahead. The first block
  // requests blocks 1 to lookahead at once; a larger lookahead requests the
  // blocks up to it at once, a smaller one requests nothing until the
  // blocks catch up: no block loses its tick when it changes.
  AUD_SCRIPT_PARAM_LOOKAHEAD = 18,
  // "worker_cores": the cores the worker may run on, AUD_SCRIPT_CORES_ANY
  // (default) or AUD_SCRIPT_CORES_FAST; Android and Linux only, Apple
  // platforms keep the quality of service class user-interactive. The
  // worker places itself anew at its next wake-up.
  AUD_SCRIPT_PARAM_WORKER_CORES = 19,
  AUD_SCRIPT_NUM_PARAMS = 20,
};

// The cores of the worker, the values of "worker_cores".
enum {
  // Any core, where the system puts it, with the raised priority. On a
  // big.LITTLE device whose idle big cores the kernel isolates, the audio
  // thread wakes the worker onto its own core (wake-affine); bound to the
  // fast cores the worker then waits there for the realtime audio thread,
  // for several blocks when callbacks come back to back.
  AUD_SCRIPT_CORES_ANY = 0,
  // Bound to the fast cores, with the raised priority.
  AUD_SCRIPT_CORES_FAST = 1,
};

// How the values of an output enter the block.
enum {
  // One event at the last frame of the block with the new value: a
  // sampler draws a line from the old value over the block.
  AUD_SCRIPT_MODE_RAMP = 0,
  // The old value one frame before the grid point k and the new value at
  // k, so that a sampler does not ramp.
  AUD_SCRIPT_MODE_STEP = 1,
};

// String settings by key.
enum {
  // "script": the JavaScript source; loads a new context on the worker and
  // swaps it in at a tick. Returns the id of an engine load (> 0) when the
  // host reports loads, else AUD_OK. A failed load keeps the old context.
  AUD_SCRIPT_KEY_SCRIPT = 0,
  // "script_name": the name of the script in error messages; default
  // "script". Applies to the next load.
  AUD_SCRIPT_KEY_SCRIPT_NAME = 1,
  AUD_SCRIPT_NUM_KEYS = 2,
};

// [control] Registers the node type with the host api of an engine; AUD_OK
// or the first error code. An AudRegisterFunction.
AUD_EXPORT int32_t aud_audio_script_register(const AudHostApi* host);

// ............................................................................
// The statistics of all modulators of the process

// Where the worker threads run.
enum {
  // No worker placed itself yet, or the platform has no placement.
  AUD_SCRIPT_PLACEMENT_SYSTEM = 0,
  // Android and Linux: bound to the fast cores.
  AUD_SCRIPT_PLACEMENT_FAST_CORES = 1,
  // Apple platforms: the quality of service class user-interactive.
  AUD_SCRIPT_PLACEMENT_QOS = 2,
  // The system refused the binding; the worker runs where it is put.
  AUD_SCRIPT_PLACEMENT_REFUSED = 3,
  // Android and Linux: any core, as "worker_cores" asks.
  AUD_SCRIPT_PLACEMENT_ANY_CORE = 4,
};

// The statistics of all modulators of the process since its start or the
// last aud_audio_script_reset_stats; the engine never resets them.
typedef struct AudScriptStats {
  uint32_t struct_size;
  // The lookahead in blocks of the last block a modulator rendered.
  uint32_t lookahead;
  // Ticks the workers ran, aborted ones included.
  uint64_t ticks;
  // Blocks whose values were not there when the block rendered live.
  uint64_t late_ticks;
  // Requests a worker never ran because a newer one was waiting.
  uint64_t skipped_requests;
  // Ticks interrupted by the budget or ended by an exception; their values
  // were dropped.
  uint64_t aborted_ticks;
  // Input events that found no room on the way to a worker.
  uint64_t dropped_events;
  // Script loads that ended, and those of them that failed.
  uint64_t loads;
  uint64_t failed_loads;
  // The time of the ticks in µs: the sum, the longest, the mean and the
  // 50th and 99th percentile from a histogram with 32 bins per octave
  // (2.2 % wide).
  double tick_time_sum_us;
  double tick_time_max_us;
  double tick_time_mean_us;
  double tick_time_p50_us;
  double tick_time_p99_us;
  // The placement of the worker that placed itself last,
  // AUD_SCRIPT_PLACEMENT_*; the cores it is bound to, a bit per core; the
  // nice value that held (Android, Linux: -19, -16, -10 or 0); the quality
  // of service class (Apple platforms, the QOS_CLASS_* value; 0
  // elsewhere); the core a worker ran its last tick on, -1 when the
  // platform does not tell.
  uint32_t placement;
  int32_t priority;
  uint64_t cpus;
  uint32_t qos_class;
  int32_t last_cpu;
  // The last load: the creation of the runtime and the context, and the
  // evaluation of the script, in µs.
  double context_create_us;
  double load_us;
  // The bytes the context of the last load holds, measured on the worker
  // after the load and every second: the count of its allocator.
  uint64_t memory_bytes;
  // The "worker_cores" the worker that placed itself last placed itself
  // for, AUD_SCRIPT_CORES_*.
  uint32_t worker_cores;
  uint32_t reserved;
} AudScriptStats;

// [any thread] Reads the statistics; AUD_OK or AUD_ERROR_INVALID_ARGUMENT
// for a missing or short struct.
AUD_EXPORT int32_t aud_audio_script_stats(AudScriptStats* stats);

// [any thread] The text of the last error of a load or a tick, prefixed
// with the script name; the last console lines, one per line, oldest
// first. Writes the text with its terminating zero into `buffer` and its
// length into `size`; AUD_ERROR_BUFFER_TOO_SMALL with the needed size in
// `size` (the zero excluded) when `capacity` is too small.
AUD_EXPORT int32_t aud_audio_script_last_error(char* buffer, size_t capacity,
                                               size_t* size);
AUD_EXPORT int32_t aud_audio_script_console(char* buffer, size_t capacity,
                                            size_t* size);

// [any thread] Resets the statistics, the last error and the console
// lines, for tests.
AUD_EXPORT void aud_audio_script_reset_stats(void);

// ............................................................................
// The simulation

// A MIDI control change at an absolute frame of the simulation.
typedef struct AudScriptControlChange {
  uint32_t frame;
  uint8_t channel;  // 0 to 15
  uint8_t number;   // 0 to 127
  uint8_t value;    // 0 to 127
  uint8_t reserved;
} AudScriptControlChange;

// What aud_audio_script_simulate runs: `source` as a modulator with a
// transport that plays from beat 0 at `tempo`, `blocks` blocks of `frames`
// frames at `sample_rate`; block n starts at the beat n * beatsPerBlock.
// The control changes reach aud.onControlChange before the tick of the
// block `lookahead` blocks after theirs, in their order, as in a modulator
// with that lookahead; the tick of block n stays the tick of block n.
typedef struct AudScriptSimulationRequest {
  uint32_t struct_size;
  uint32_t lookahead;  // 0: 1, the tick of the next block
  const char* source;
  const char* name;  // NULL: "script"
  double sample_rate;
  double tempo;
  uint32_t frames;
  uint32_t blocks;
  uint32_t memory_limit_mb;  // 0: 32
  uint32_t tick_budget_ms;   // 0: 100
  uint32_t num_events;
  uint32_t reserved;
  const AudScriptControlChange* events;
} AudScriptSimulationRequest;

// Results of a tick.
enum {
  AUD_SCRIPT_TICK_OK = 0,
  // The tick ran over its budget and was interrupted.
  AUD_SCRIPT_TICK_INTERRUPTED = 1,
  // The tick threw an exception, an out of memory included.
  AUD_SCRIPT_TICK_FAILED = 2,
};

// One tick of a simulation: its number (the block, 1 to blocks - 1), the
// beat of its grid point, the frame offset k, the time it took and its
// aud.send calls, sends[first_send] to sends[first_send + num_sends - 1].
typedef struct AudScriptSimulationTick {
  uint32_t tick;
  uint32_t offset;
  uint32_t first_send;
  uint32_t num_sends;
  int32_t status;  // AUD_SCRIPT_TICK_*
  uint32_t reserved;
  double beat;
  double time_us;
} AudScriptSimulationTick;

// One aud.send call.
typedef struct AudScriptSimulationSend {
  uint32_t output;
  uint32_t id;
  double value;
} AudScriptSimulationSend;

// The result of a simulation, owned by the package until
// aud_audio_script_simulation_free.
typedef struct AudScriptSimulation {
  uint32_t struct_size;
  // AUD_OK, or the error of the load (AUD_ERROR_FAILED); then no tick ran.
  int32_t load_result;
  uint32_t num_ticks;
  uint32_t num_sends;
  const AudScriptSimulationTick* ticks;
  const AudScriptSimulationSend* sends;
  // The error of the load or of the last failed tick; "" without one.
  const char* error;
  // The console lines, one per line.
  const char* console;
  // The creation of the context and the evaluation of the script, in µs.
  double context_create_us;
  double load_us;
} AudScriptSimulation;

// [offline] Runs the simulation on a thread of its own with the stack of a
// worker and writes its result into `result`; AUD_OK,
// AUD_ERROR_INVALID_ARGUMENT for a missing or short struct, no source, no
// frames, a sample rate outside 8 to 384 kHz, a tempo outside 1 to 999
// BPM, more than 1000000 blocks, a lookahead above 4 or events out of
// range, or AUD_ERROR_OUT_OF_MEMORY.
AUD_EXPORT int32_t aud_audio_script_simulate(
    const AudScriptSimulationRequest* request, AudScriptSimulation** result);

// [any thread] Frees a simulation; NULL is ignored.
AUD_EXPORT void aud_audio_script_simulation_free(AudScriptSimulation* result);

// ............................................................................
// The bench

// Flags of a bench request.
enum {
  // Runs the ticks back to back instead of one per block duration.
  AUD_SCRIPT_BENCH_UNPACED = 1 << 0,
  // Binds the worker to the fast cores, as AUD_SCRIPT_CORES_FAST; without
  // it the worker runs on any core.
  AUD_SCRIPT_BENCH_FAST_CORES = 1 << 1,
};

// What aud_audio_script_bench_run measures: `source` loaded into a context
// on a worker thread placed like the node's, then `warmup_ticks` and
// `ticks` ticks of blocks of `frames` frames at `sample_rate` with a
// transport that plays at `tempo`, each tick timed with a steady clock.
// Unless AUD_SCRIPT_BENCH_UNPACED is set, a tick starts every block
// duration, as on the worker of a node.
typedef struct AudScriptBenchRequest {
  uint32_t struct_size;
  uint32_t flags;  // AUD_SCRIPT_BENCH_*
  const char* source;
  const char* name;  // NULL: "script"
  double sample_rate;
  double tempo;
  uint32_t frames;
  uint32_t ticks;
  uint32_t warmup_ticks;
  uint32_t memory_limit_mb;  // 0: 32
  uint32_t tick_budget_ms;   // 0: 100
} AudScriptBenchRequest;

// The bytes of the error text of a bench result, its zero included.
#define AUD_SCRIPT_BENCH_ERROR_BYTES 512

// What the bench measured.
typedef struct AudScriptBenchResult {
  uint32_t struct_size;
  // The ticks that were timed, those that took longer than a block and
  // those that were aborted.
  uint32_t ticks;
  uint32_t over_block;
  uint32_t aborted;
  // The placement of the bench's worker, as in AudScriptStats.
  uint32_t placement;
  int32_t priority;
  uint64_t cpus;
  uint32_t qos_class;
  int32_t last_cpu;
  // The duration of a block in µs.
  double block_us;
  // The tick times in µs.
  double mean_us;
  double p50_us;
  double p99_us;
  double max_us;
  // The creation of the context and the evaluation of the script in µs.
  double context_create_us;
  double load_us;
  // The bytes the context holds after the timed ticks.
  uint64_t memory_bytes;
  // The error of a load that failed, prefixed with the script name and cut
  // to fit; "" otherwise.
  char error[AUD_SCRIPT_BENCH_ERROR_BYTES];
} AudScriptBenchResult;

// [offline] Runs the bench on a worker thread of its own and waits for it;
// AUD_OK, AUD_ERROR_INVALID_ARGUMENT as for aud_audio_script_simulate or
// for no ticks, AUD_ERROR_FAILED when the script does not load
// (`result->error` tells why), or AUD_ERROR_OUT_OF_MEMORY. The bench counts
// nothing into the statistics of the modulators.
AUD_EXPORT int32_t aud_audio_script_bench_run(
    const AudScriptBenchRequest* request, AudScriptBenchResult* result);

#ifdef __cplusplus
}
#endif

#endif  // AUD_AUDIO_SCRIPT_H
