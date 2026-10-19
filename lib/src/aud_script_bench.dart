// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:convert';
import 'dart:ffi';

import 'package:aud_audio_core/aud_audio_core.dart';
import 'package:ffi/ffi.dart';
import 'package:meta/meta.dart';

import 'aud_audio_script_bindings_generated.dart' as bindings;
import 'aud_script_bench_result.dart';
import 'aud_script_exception.dart';
import 'aud_script_placement.dart';
import 'aud_script_worker_cores.dart';

// #############################################################################
/// The speed of a script (ticket 31, scope 3): loads it into a context on a
/// worker thread placed like a modulator's - on Android and Linux on the
/// cores `workerCores` names with a raised priority, on Apple platforms
/// user-interactive - and times its ticks, by default one per block
/// duration as the audio callback paces a worker. A bench on the main
/// thread would measure another core.
///
/// ```dart
/// final result = AudScriptBench.measure(AudScriptBench.syntheticScript);
/// print(result); // mean 12.3 µs, p99 ... of a block of 5333 µs
/// ```
abstract final class AudScriptBench {
  // ...........................................................................
  /// The synthetic workload of the package: breakpoint envelopes locked to
  /// the beat with eased (raised-cosine) segments, looped with phase
  /// offsets, and target functions per output - sums, products and an `if`
  /// on CC 74 of channel 0 - that send cutoff, resonance, gain and pan-like
  /// values (ids 300 to 303, 0 to 1) on the outputs 0, 1 and 2 every tick.
  static const String syntheticScript = 'var instances = 1;\n$_syntheticBody';

  /// The synthetic workload computed ten times per tick, the results
  /// averaged: the same sends, ten times the work.
  static const String syntheticStressScript =
      'var instances = 10;\n$_syntheticBody';

  // ...........................................................................
  /// Times the ticks of [source] and returns what the bench measured.
  ///
  /// - [name] the name of the script in error messages
  /// - [sampleRate] the sample rate in Hz, 8 to 384 kHz
  /// - [frames] the frames of a block
  /// - [tempo] the tempo of the transport in BPM, 1 to 999
  /// - [ticks] the ticks that are timed
  /// - [warmupTicks] the ticks before them
  /// - [paced] whether a tick starts every block duration; false runs them
  ///   back to back
  /// - [workerCores] the cores of the bench's worker, as a modulator's
  /// - [memoryLimitMb] the heap of the context in MB
  /// - [tickBudgetMs] the longest tick in ms
  ///
  /// Throws an [AudScriptException] with `AUD_ERROR_FAILED` and the error
  /// of the script when it does not load and `AUD_ERROR_INVALID_ARGUMENT`
  /// for arguments out of range.
  @RecordUse()
  static AudScriptBenchResult measure(
    String source, {
    String name = 'script',
    double sampleRate = 48000,
    int frames = 256,
    double tempo = 120,
    int ticks = 1000,
    int warmupTicks = 50,
    bool paced = true,
    AudScriptWorkerCores workerCores = AudScriptWorkerCores.any,
    int memoryLimitMb = 32,
    int tickBudgetMs = 100,
  }) => using((arena) {
    final request = arena<bindings.AudScriptBenchRequest>();
    final result = arena<bindings.AudScriptBenchResult>();
    request.ref
      ..struct_size = sizeOf<bindings.AudScriptBenchRequest>()
      ..source = source.toNativeUtf8(allocator: arena).cast()
      ..name = name.toNativeUtf8(allocator: arena).cast()
      ..sample_rate = sampleRate
      ..tempo = tempo
      ..frames = frames
      ..ticks = ticks
      ..warmup_ticks = warmupTicks
      ..flags =
          (paced ? 0 : bindings.AUD_SCRIPT_BENCH_UNPACED) |
          (workerCores == AudScriptWorkerCores.fast
              ? bindings.AUD_SCRIPT_BENCH_FAST_CORES
              : 0)
      ..memory_limit_mb = memoryLimitMb
      ..tick_budget_ms = tickBudgetMs;
    result.ref.struct_size = sizeOf<bindings.AudScriptBenchResult>();
    final code = bindings.aud_audio_script_bench_run(request, result);
    if (code != AUD_OK) {
      throw AudScriptException(
        code: code,
        operation: 'the bench of $name',
        message: code == AUD_ERROR_FAILED ? _text(result.ref.error) : '',
      );
    }
    final r = result.ref;
    return AudScriptBenchResult(
      sampleRate: sampleRate,
      frames: frames,
      ticks: r.ticks,
      overBlock: r.over_block,
      aborted: r.aborted,
      blockUs: r.block_us,
      meanUs: r.mean_us,
      p50Us: r.p50_us,
      p99Us: r.p99_us,
      maxUs: r.max_us,
      contextCreateUs: r.context_create_us,
      loadUs: r.load_us,
      memoryBytes: r.memory_bytes,
      placement: AudScriptPlacement.fromId(r.placement),
      priority: r.priority,
      cpus: r.cpus,
      qosClass: r.qos_class,
      lastCpu: r.last_cpu,
    );
  });

  // The UTF-8 text of a zero-terminated array of characters.
  static String _text(Array<Char> chars) {
    final bytes = <int>[];
    for (var i = 0; i < bindings.AUD_SCRIPT_BENCH_ERROR_BYTES; ++i) {
      if (chars[i] == 0) break;
      bytes.add(chars[i] & 0xFF);
    }
    return utf8.decode(bytes, allowMalformed: true);
  }
}

// The synthetic workload without its first line, which sets `instances`:
// how often the patch is computed per tick, its results averaged. A script
// of the package's own that does what modulation scripts do - breakpoint
// envelopes locked to the beat with eased segments, looped with phase
// offsets, and target functions per output that combine them - so that the
// speed of the engine can be measured in public. scripts/test-native.js
// reads it for the native tests; keep it the only raw string of the file.
const String _syntheticBody = r'''
// A synthetic modulation patch: breakpoint envelopes locked to the beat,
// combined into cutoff, resonance, gain and pan-like controls (ids 300 to
// 303, values from 0 to 1) on the outputs 0, 1 and 2 every tick. CC 74 of
// channel 0 switches the gain function of every output.

var cutoff = 300;
var resonance = 301;
var gain = 302;
var pan = 303;

// The last value of every controller, by channel and number.
var controllers = [];
for (var channel = 0; channel < 16; ++channel) {
  var row = [];
  for (var number = 0; number < 128; ++number) row.push(0);
  controllers.push(row);
}
controllers[0][74] = 32;

aud.onControlChange = function (channel, number, value) {
  controllers[channel][number] = value;
};

// The interpolations of a segment from a to b at t from 0 to 1.
var shapes = {
  eased: function (a, b, t) {
    return a + (b - a) * (1 - Math.cos(Math.PI * t)) / 2;
  },
  linear: function (a, b, t) {
    return a + (b - a) * t;
  },
  hold: function (a, b, t) {
    return a;
  },
  rise: function (a, b, t) {
    return a + (b - a) * (Math.pow(2, 6 * t) - 1) / 63;
  }
};

// An envelope of breakpoints [beat, value] that loops every `length`
// beats, runs at `rate` times the beat and starts `phase` beats late.
function Envelope(points, length, shape, rate, phase) {
  this.points = points;
  this.length = length;
  this.shape = shapes[shape];
  this.rate = rate;
  this.phase = phase;
}

Envelope.prototype.at = function (beat) {
  var position = (beat * this.rate - this.phase) % this.length;
  if (position < 0) position += this.length;
  var points = this.points;
  var last = points.length - 1;
  for (var i = 0; i < last; ++i) {
    if (position < points[i + 1][0]) {
      var t = (position - points[i][0]) / (points[i + 1][0] - points[i][0]);
      return this.shape(points[i][1], points[i + 1][1], t);
    }
  }
  // From the last point back to the first one at the end of the loop.
  var span = this.length - points[last][0];
  var t = span > 0 ? (position - points[last][0]) / span : 0;
  return this.shape(points[last][1], points[0][1], t);
};

function clamp(value) {
  return value < 0 ? 0 : value > 1 ? 1 : value;
}

// The envelopes of one output, by the target they drive.
function Voice(offset) {
  this.sweep = new Envelope(
    [[0, 0.1], [1, 0.9], [1.5, 0.4], [3, 0.7]], 4, 'eased', 1, offset);
  this.wobble = new Envelope(
    [[0, 0], [0.25, 1], [0.5, 0.2], [0.75, 0.8]], 1, 'eased', 2, offset / 2);
  this.steps = new Envelope(
    [[0, 0.2], [0.5, 0.6], [1, 0.4], [1.5, 1]], 2, 'hold', 1, offset);
  this.swell = new Envelope(
    [[0, 0], [6, 1], [7, 0.5]], 8, 'rise', 1, offset * 2);
  this.drift = new Envelope(
    [[0, -1], [2, 1]], 4, 'linear', 0.5, offset);
}

// The targets of a voice at `beat`; `bright` from CC 74.
Voice.prototype.targets = function (beat, bright) {
  var sweep = this.sweep.at(beat);
  var wobble = this.wobble.at(beat);
  var steps = this.steps.at(beat);
  var swell = this.swell.at(beat);
  var drift = this.drift.at(beat);
  var level;
  if (bright > 64) {
    level = 0.4 + 0.6 * steps * swell;
  } else {
    level = 0.8 - 0.3 * wobble;
  }
  return [
    clamp(0.15 + 0.7 * sweep * (0.5 + 0.5 * wobble)),
    clamp(0.2 + 0.25 * (sweep + steps) / 2),
    clamp(level),
    clamp(0.5 + 0.4 * drift * (1 - swell / 2))
  ];
};

// The patch: three outputs, each with its own phase, `instances` times.
var patches = [];
for (var instance = 0; instance < instances; ++instance) {
  patches.push([
    new Voice(0 + instance * 0.125),
    new Voice(0.5 + instance * 0.125),
    new Voice(1.25 + instance * 0.125)
  ]);
}

aud.onTick = function () {
  var beat = aud.time.beat;
  var bright = controllers[0][74];
  for (var output = 0; output < 3; ++output) {
    var sums = [0, 0, 0, 0];
    for (var i = 0; i < patches.length; ++i) {
      var targets = patches[i][output].targets(beat, bright);
      for (var j = 0; j < 4; ++j) sums[j] += targets[j];
    }
    aud.send(output, cutoff, sums[0] / patches.length);
    aud.send(output, resonance, sums[1] / patches.length);
    aud.send(output, gain, sums[2] / patches.length);
    aud.send(output, pan, sums[3] / patches.length);
  }
};
''';
