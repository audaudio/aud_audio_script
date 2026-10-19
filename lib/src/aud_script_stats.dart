// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'aud_script_engine.dart';
import 'aud_script_placement.dart';

// #############################################################################
/// The statistics of all modulators of the process, as
/// `AudScriptModulator.stats()` reads them: the ticks of the workers and
/// their times, the late blocks, the skipped requests and aborted ticks,
/// the placement of the workers, the last load and its memory, the last
/// error and the last console lines.
class AudScriptStats {
  /// Creates statistics.
  const AudScriptStats({
    this.engine = AudScriptEngine.quickjs,
    this.ticks = 0,
    this.lateTicks = 0,
    this.skippedRequests = 0,
    this.abortedTicks = 0,
    this.droppedEvents = 0,
    this.loads = 0,
    this.failedLoads = 0,
    this.tickTimeSumUs = 0,
    this.tickTimeMaxUs = 0,
    this.tickTimeMeanUs = 0,
    this.tickTimeP50Us = 0,
    this.tickTimeP99Us = 0,
    this.placement = AudScriptPlacement.system,
    this.priority = 0,
    this.cpus = 0,
    this.qosClass = 0,
    this.lookahead = 0,
    this.contextCreateUs = 0,
    this.loadUs = 0,
    this.memoryBytes = 0,
    this.lastError = '',
    this.console = const [],
  });

  // ...........................................................................
  /// The statistics as JSON, e.g. for the report of a measurement.
  Map<String, Object> toJson() => {
    'engine': engine.settingValue,
    'ticks': ticks,
    'lateTicks': lateTicks,
    'skippedRequests': skippedRequests,
    'abortedTicks': abortedTicks,
    'droppedEvents': droppedEvents,
    'loads': loads,
    'failedLoads': failedLoads,
    'tickTimeSumUs': tickTimeSumUs,
    'tickTimeMaxUs': tickTimeMaxUs,
    'tickTimeMeanUs': tickTimeMeanUs,
    'tickTimeP50Us': tickTimeP50Us,
    'tickTimeP99Us': tickTimeP99Us,
    'placement': placement.name,
    'priority': priority,
    'cpus': cpus,
    'qosClass': qosClass,
    'lookahead': lookahead,
    'contextCreateUs': contextCreateUs,
    'loadUs': loadUs,
    'memoryBytes': memoryBytes,
    'lastError': lastError,
    'console': console,
  };

  @override
  String toString() =>
      'aud.script.modulator: $ticks ticks, mean '
      '${tickTimeMeanUs.toStringAsFixed(1)} µs, p99 '
      '${tickTimeP99Us.toStringAsFixed(1)} µs, max '
      '${tickTimeMaxUs.toStringAsFixed(1)} µs; $lateTicks late, '
      '$skippedRequests skipped, $abortedTicks aborted; ${engine.name} on '
      '${placement.name}';

  // ...........................................................................
  /// The engine of the last load.
  final AudScriptEngine engine;

  /// The ticks the workers ran, aborted ones included.
  final int ticks;

  /// The blocks whose values were not there when they rendered live; the
  /// samplers held their values.
  final int lateTicks;

  /// The requests a worker never ran because a newer one was waiting.
  final int skippedRequests;

  /// The ticks that ran over their budget or threw; their values were
  /// dropped.
  final int abortedTicks;

  /// The input events that found no room on their way to a worker.
  final int droppedEvents;

  /// The script loads that ended, and those of them that failed.
  final int loads;

  /// The loads that failed.
  final int failedLoads;

  /// The sum of the tick times in µs.
  final double tickTimeSumUs;

  /// The longest tick in µs.
  final double tickTimeMaxUs;

  /// The mean tick time in µs.
  final double tickTimeMeanUs;

  /// The median tick time in µs, from a histogram of 2.2 % wide bins.
  final double tickTimeP50Us;

  /// The 99th percentile of the tick times in µs.
  final double tickTimeP99Us;

  /// Where the last worker that started runs.
  final AudScriptPlacement placement;

  /// The nice value that held for it on Android and Linux: -19, -16, -10,
  /// or 0 when none did.
  final int priority;

  /// The cores it is bound to, a bit per core.
  final int cpus;

  /// Its quality of service class on Apple platforms, `QOS_CLASS_*`.
  final int qosClass;

  /// The lookahead in blocks of the last block a modulator rendered; 0
  /// before the first.
  final int lookahead;

  /// The creation of the runtime and the context of the last load in µs.
  final double contextCreateUs;

  /// The evaluation of the script of the last load in µs.
  final double loadUs;

  /// The bytes the context of the last load holds, measured after the load
  /// and every second; 0 for JavaScriptCore.
  final int memoryBytes;

  /// The last error of a load or a tick, prefixed with the script name.
  final String lastError;

  /// The last console lines, oldest first, each prefixed with the script
  /// name.
  final List<String> console;
}
