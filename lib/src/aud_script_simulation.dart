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
import 'aud_script_control_change.dart';
import 'aud_script_engine.dart';
import 'aud_script_exception.dart';
import 'aud_script_send.dart';
import 'aud_script_tick.dart';

// #############################################################################
/// A script run on the host code of a modulator's worker without a graph:
/// the same tick grid, the same order of the input events, the same
/// budget. Block n starts at the beat n * beatsPerBlock of a transport that
/// plays from beat 0; block 0 has no tick. It reports every tick with the
/// sends of the script, so that an app compares the modulator with a
/// reference of its own outside the engine.
///
/// ```dart
/// final simulation = AudScriptSimulation.simulate(source, blocks: 1000);
/// for (final tick in simulation.ticks) print(tick);
/// ```
class AudScriptSimulation {
  /// Creates a simulation result.
  const AudScriptSimulation({
    required this.ticks,
    this.error = '',
    this.console = const [],
    this.contextCreateUs = 0,
    this.loadUs = 0,
  });

  // ...........................................................................
  /// Runs [source] for [blocks] blocks on a thread with the stack of a
  /// worker and returns its ticks.
  ///
  /// - [name] the name of the script in error messages
  /// - [engine] the JavaScript engine
  /// - [sampleRate] the sample rate in Hz, 8 to 384 kHz
  /// - [frames] the frames of a block
  /// - [tempo] the tempo in BPM, 1 to 999
  /// - [blocks] the blocks, up to 1000000; the ticks run for blocks 1 to
  ///   [blocks] - 1
  /// - [controlChanges] the MIDI control changes, at their frames
  /// - [lookahead] the lookahead of the modulator, 1 to 4: the changes of
  ///   block j reach the tick of block j + [lookahead]
  /// - [memoryLimitMb] the heap of the context in MB
  /// - [tickBudgetMs] the longest tick in ms
  ///
  /// Throws an [AudScriptException] with `AUD_ERROR_FAILED` and the error
  /// of the script when it does not load, `AUD_ERROR_UNSUPPORTED` for an
  /// engine the platform lacks and `AUD_ERROR_INVALID_ARGUMENT` for
  /// arguments out of range.
  @RecordUse()
  static AudScriptSimulation simulate(
    String source, {
    String name = 'script',
    AudScriptEngine engine = AudScriptEngine.quickjs,
    double sampleRate = 48000,
    int frames = 256,
    double tempo = 120,
    required int blocks,
    List<AudScriptControlChange> controlChanges = const [],
    int lookahead = 1,
    int memoryLimitMb = 32,
    int tickBudgetMs = 100,
  }) => using((arena) {
    final request = arena<bindings.AudScriptSimulationRequest>();
    final events = arena<bindings.AudScriptControlChange>(
      controlChanges.isEmpty ? 1 : controlChanges.length,
    );
    for (final (index, change) in controlChanges.indexed) {
      events[index]
        ..frame = change.frame
        ..channel = change.channel
        ..number = change.number
        ..value = change.value;
    }
    request.ref
      ..struct_size = sizeOf<bindings.AudScriptSimulationRequest>()
      ..engine = engine.id
      ..source = source.toNativeUtf8(allocator: arena).cast()
      ..name = name.toNativeUtf8(allocator: arena).cast()
      ..sample_rate = sampleRate
      ..tempo = tempo
      ..frames = frames
      ..blocks = blocks
      ..memory_limit_mb = memoryLimitMb
      ..tick_budget_ms = tickBudgetMs
      ..num_events = controlChanges.length
      ..lookahead = lookahead
      ..events = events;
    final result = arena<Pointer<bindings.AudScriptSimulation>>();
    final operation = 'the simulation of $name';
    final code = bindings.aud_audio_script_simulate(request, result);
    if (code != AUD_OK) {
      throw AudScriptException(code: code, operation: operation);
    }
    try {
      final simulation = result.value.ref;
      final error = simulation.error.cast<Utf8>().toDartString();
      if (simulation.load_result != AUD_OK) {
        throw AudScriptException(
          code: simulation.load_result,
          operation: operation,
          message: error,
        );
      }
      return AudScriptSimulation(
        ticks: [
          for (var t = 0; t < simulation.num_ticks; ++t)
            _tick(simulation.ticks[t], simulation.sends),
        ],
        error: error,
        console: simulation.console
            .cast<Utf8>()
            .toDartString()
            .split('\n')
            .where((line) => line.isNotEmpty)
            .toList(),
        contextCreateUs: simulation.context_create_us,
        loadUs: simulation.load_us,
      );
    } finally {
      bindings.aud_audio_script_simulation_free(result.value);
    }
  });

  // ...........................................................................
  /// The ticks, one per block from block 1.
  final List<AudScriptTick> ticks;

  /// The error of the last aborted tick, or of an input callback that
  /// threw; empty without one.
  final String error;

  /// The console lines of the script, each prefixed with its name.
  final List<String> console;

  /// The creation of the runtime and the context in µs.
  final double contextCreateUs;

  /// The evaluation of the script in µs.
  final double loadUs;

  // ...........................................................................
  static AudScriptTick _tick(
    bindings.AudScriptSimulationTick tick,
    Pointer<bindings.AudScriptSimulationSend> sends,
  ) => AudScriptTick(
    tick: tick.tick,
    beat: tick.beat,
    offset: tick.offset,
    timeUs: tick.time_us,
    status: AudScriptTickStatus.values[tick.status],
    sends: [
      for (var s = tick.first_send; s < tick.first_send + tick.num_sends; ++s)
        AudScriptSend(
          output: sends[s].output,
          id: sends[s].id,
          value: sends[s].value,
        ),
    ],
  );
}
