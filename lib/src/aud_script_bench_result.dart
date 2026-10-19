// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'aud_script_placement.dart';

// #############################################################################
/// What `AudScriptBench.measure` measured: the tick times against the
/// duration of a block, the ticks that took longer or were aborted, the
/// creation of the context, the load, the memory of the context and where
/// the worker ran.
class AudScriptBenchResult {
  /// Creates a result.
  const AudScriptBenchResult({
    required this.sampleRate,
    required this.frames,
    required this.ticks,
    required this.blockUs,
    required this.meanUs,
    required this.p50Us,
    required this.p99Us,
    required this.maxUs,
    this.overBlock = 0,
    this.aborted = 0,
    this.contextCreateUs = 0,
    this.loadUs = 0,
    this.memoryBytes = 0,
    this.placement = AudScriptPlacement.system,
    this.priority = 0,
    this.cpus = 0,
    this.qosClass = 0,
  });

  // ...........................................................................
  /// The result as JSON, e.g. for the report of an integration test.
  Map<String, Object> toJson() => {
    'sampleRate': sampleRate,
    'frames': frames,
    'ticks': ticks,
    'overBlock': overBlock,
    'aborted': aborted,
    'blockUs': blockUs,
    'meanUs': meanUs,
    'p50Us': p50Us,
    'p99Us': p99Us,
    'maxUs': maxUs,
    'p99Share': p99Share,
    'contextCreateUs': contextCreateUs,
    'loadUs': loadUs,
    'memoryBytes': memoryBytes,
    'placement': placement.name,
    'priority': priority,
    'cpus': cpus,
    'qosClass': qosClass,
  };

  @override
  String toString() =>
      'mean ${_fixed(meanUs)} µs, p50 ${_fixed(p50Us)} µs, '
      'p99 ${_fixed(p99Us)} µs, max ${_fixed(maxUs)} µs per tick of a block of '
      '${_fixed(blockUs)} µs ($frames frames at ${sampleRate.round()} Hz); '
      '$overBlock over the block, $aborted aborted; context '
      '${_fixed(contextCreateUs)} µs, load ${_fixed(loadUs)} µs, '
      '$memoryBytes bytes; ${placement.name}';

  // ...........................................................................
  /// The sample rate in Hz.
  final double sampleRate;

  /// The frames of a block.
  final int frames;

  /// The ticks that were timed.
  final int ticks;

  /// The ticks that took longer than a block.
  final int overBlock;

  /// The ticks that were aborted.
  final int aborted;

  /// The duration of a block in µs.
  final double blockUs;

  /// The mean tick time in µs.
  final double meanUs;

  /// The median tick time in µs.
  final double p50Us;

  /// The 99th percentile of the tick times in µs.
  final double p99Us;

  /// The longest tick in µs.
  final double maxUs;

  /// The creation of the runtime and the context in µs.
  final double contextCreateUs;

  /// The evaluation of the script in µs.
  final double loadUs;

  /// The bytes the context held after the ticks.
  final int memoryBytes;

  /// Where the worker of the bench ran.
  final AudScriptPlacement placement;

  /// The nice value that held on Android and Linux.
  final int priority;

  /// The cores the worker was bound to, a bit per core.
  final int cpus;

  /// The quality of service class on Apple platforms.
  final int qosClass;

  // ...........................................................................
  /// [p99Us] as a share of [blockUs]: 0.1 takes a tenth of the block.
  double get p99Share => p99Us / blockUs;

  static String _fixed(double value) => value.toStringAsFixed(1);
}
