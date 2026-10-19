// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:ffi';

import 'package:aud_audio_core/aud_audio_core.dart';
import 'package:ffi/ffi.dart';
import 'package:meta/meta.dart';

import 'aud_audio_script_bindings_generated.dart' as bindings;
import 'aud_script_mode.dart';
import 'aud_script_placement.dart';
import 'aud_script_stats.dart';
import 'aud_script_worker_cores.dart';

// #############################################################################
/// The node `aud.script.modulator`: runs a JavaScript source on a worker
/// thread of its own, once per audio block, and sends the values the script
/// sets as numeric controls (`AUD_EVENT_CONTROL`) on its event outputs, for
/// example to the controllers of a sampler.
///
/// The script sees `aud`: `aud.time` (beat, tempo, playing, sampleRate,
/// frames, tick) before every `aud.onTick()`, the MIDI control changes of the
/// input through `aud.onControlChange(channel, number, value)`, numeric
/// controls through `aud.onControl(id, value)`, and `aud.send(output, id,
/// value)`; `console.log` writes into [stats]. The node emits a value only
/// when it differs from the last one it emitted for the pair. Dart only
/// configures the node: the worker runs the script without Dart, and
/// JavaScript never runs on the audio thread.
///
/// ```dart
/// final node = graph.createNode(AudScriptModulator.typeId);
/// graph.setString(node, AudScriptModulator.script, source);
/// graph.transaction((tx) => tx.connectEvents(node, sampler, fromPort: 0));
/// ```
abstract final class AudScriptModulator {
  // ...........................................................................
  /// The type id of the node.
  static const String typeId = 'aud.script.modulator';

  /// The version of the node type.
  static const int version = 1;

  // ...........................................................................
  // Events

  /// The id of the event input: MIDI control changes and numeric controls.
  static const String input = 'in';

  /// The event outputs, `out0` to `out15`; a script uses as many as it
  /// needs.
  static const int outputs = 16;

  /// The id of the event output [index].
  static String output(int index) => 'out$index';

  /// The control ids a script sends on an output: 0 to [maxId].
  static const int maxId = 511;

  /// The (output, id) pairs one script context keeps values for.
  static const int maxValues = 1024;

  // ...........................................................................
  // String settings

  /// The JavaScript source. A new value loads a new context on the worker
  /// and swaps it in before the next tick; a failed load keeps the old one.
  /// The setting returns the id of the load, whose end tells the error.
  static const String script = 'script';

  /// The name of the script in error messages; default `script`. Applies
  /// to the next load.
  static const String scriptName = 'script_name';

  /// The string ids in the order of their keys.
  static const List<String> strings = [script, scriptName];

  // ...........................................................................
  // Parameters

  /// The id of the parameter that sets how the values of [output] enter
  /// the block, an [AudScriptMode.value]; default [AudScriptMode.ramp].
  static String mode(int output) => 'mode$output';

  /// The heap of a script context in MB, 1 to 1024; default 32. A change
  /// applies to the running context at its next tick.
  static const String memoryLimit = 'memory_limit';

  /// The longest tick in ms, 1 to 10000; default 100. A tick that runs
  /// longer is interrupted and counted as aborted; its values are dropped.
  static const String tickBudget = 'tick_budget';

  /// How many blocks ahead the worker computes, 1 to 4; default 2. At the
  /// end of block n the node requests block n + lookahead, so the worker
  /// has lookahead - 1 blocks of slack, e.g. for audio callbacks that come
  /// back to back after a late one; the input events of block n reach the
  /// tick of block n + lookahead. A change leaves no block without its
  /// tick.
  static const String lookahead = 'lookahead';

  /// The cores the worker may run on, an [AudScriptWorkerCores.value];
  /// default [AudScriptWorkerCores.any]. The worker places itself anew at
  /// its next wake-up; Apple platforms keep their quality of service class.
  static const String workerCores = 'worker_cores';

  /// The parameter ids in the order of their indices.
  static const List<String> params = [
    'mode0',
    'mode1',
    'mode2',
    'mode3',
    'mode4',
    'mode5',
    'mode6',
    'mode7',
    'mode8',
    'mode9',
    'mode10',
    'mode11',
    'mode12',
    'mode13',
    'mode14',
    'mode15',
    memoryLimit,
    tickBudget,
    lookahead,
    workerCores,
  ];

  /// The parameters as the node declares them, in the order of their
  /// indices.
  static List<AudParamDescriptor> get paramDescriptors => [
    for (var output = 0; output < outputs; ++output)
      AudParamDescriptor(
        id: mode(output),
        name: 'Mode of output $output',
        defaultValue: AudScriptMode.ramp.value,
        stepped: true,
        steps: 2,
      ),
    const AudParamDescriptor(
      id: memoryLimit,
      name: 'Memory limit',
      unit: 'MB',
      min: 1,
      max: 1024,
      defaultValue: 32,
      stepped: true,
      steps: 1024,
    ),
    const AudParamDescriptor(
      id: tickBudget,
      name: 'Tick budget',
      unit: 'ms',
      min: 1,
      max: 10000,
      defaultValue: 100,
      stepped: true,
      steps: 10000,
    ),
    const AudParamDescriptor(
      id: lookahead,
      name: 'Lookahead',
      unit: 'blocks',
      min: 1,
      max: 4,
      defaultValue: 2,
      stepped: true,
      steps: 4,
    ),
    AudParamDescriptor(
      id: workerCores,
      name: 'Worker cores',
      defaultValue: AudScriptWorkerCores.any.value.toDouble(),
      stepped: true,
      steps: 2,
    ),
  ];

  // ...........................................................................
  // Native

  /// The statistics of all modulators of the process, with the last error
  /// and the last console lines; the engine never resets them.
  @RecordUse()
  static AudScriptStats stats() => using((arena) {
    final stats = arena<bindings.AudScriptStats>();
    stats.ref.struct_size = sizeOf<bindings.AudScriptStats>();
    bindings.aud_audio_script_stats(stats);
    final s = stats.ref;
    return AudScriptStats(
      ticks: s.ticks,
      lateTicks: s.late_ticks,
      skippedRequests: s.skipped_requests,
      abortedTicks: s.aborted_ticks,
      droppedEvents: s.dropped_events,
      loads: s.loads,
      failedLoads: s.failed_loads,
      tickTimeSumUs: s.tick_time_sum_us,
      tickTimeMaxUs: s.tick_time_max_us,
      tickTimeMeanUs: s.tick_time_mean_us,
      tickTimeP50Us: s.tick_time_p50_us,
      tickTimeP99Us: s.tick_time_p99_us,
      placement: AudScriptPlacement.fromId(s.placement),
      priority: s.priority,
      cpus: s.cpus,
      qosClass: s.qos_class,
      lastCpu: s.last_cpu,
      workerCores: AudScriptWorkerCores.fromValue(s.worker_cores),
      lookahead: s.lookahead,
      contextCreateUs: s.context_create_us,
      loadUs: s.load_us,
      memoryBytes: s.memory_bytes,
      lastError: _text(bindings.aud_audio_script_last_error, arena),
      console: _text(
        bindings.aud_audio_script_console,
        arena,
      ).split('\n').where((line) => line.isNotEmpty).toList(),
    );
  });

  /// Resets the statistics, the last error and the console lines, e.g.
  /// between two measurements.
  @RecordUse()
  static void resetStats() => bindings.aud_audio_script_reset_stats();

  // The text a native function writes; a buffer that turns out too small
  // grows to the size the function tells.
  static String _text(
    int Function(Pointer<Char>, int, Pointer<Size>) read,
    Arena arena,
  ) {
    final size = arena<Size>();
    var capacity = 256;
    while (true) {
      final buffer = arena<Char>(capacity);
      if (read(buffer, capacity, size) == AUD_OK) {
        return buffer.cast<Utf8>().toDartString(length: size.value);
      }
      capacity = size.value + 1;
    }
  }
}
