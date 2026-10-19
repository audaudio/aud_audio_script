// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'package:aud_audio_script/aud_audio_script.dart';
import 'package:test/test.dart';

void main() {
  const stats = AudScriptStats(
    ticks: 1000,
    lateTicks: 2,
    skippedRequests: 1,
    abortedTicks: 3,
    droppedEvents: 4,
    loads: 5,
    failedLoads: 1,
    tickTimeSumUs: 12000,
    tickTimeMaxUs: 80,
    tickTimeMeanUs: 12,
    tickTimeP50Us: 11,
    tickTimeP99Us: 40,
    placement: AudScriptPlacement.fastCores,
    priority: -19,
    cpus: 0xF0,
    qosClass: 0,
    lastCpu: 7,
    workerCores: AudScriptWorkerCores.fast,
    lookahead: 3,
    contextCreateUs: 300,
    loadUs: 900,
    memoryBytes: 1 << 20,
    lastError: 'a.js: Error: no',
    console: ['a.js: hello'],
  );

  group('AudScriptStats', () {
    test('starts at zero', () {
      const empty = AudScriptStats();
      expect(empty.ticks, 0);
      expect(empty.placement, AudScriptPlacement.system);
      expect(empty.console, isEmpty);
      expect(empty.lookahead, 0);
      expect(empty.lastCpu, -1);
      expect(empty.workerCores, AudScriptWorkerCores.any);
    });

    group('toJson()', () {
      test('writes every field', () {
        expect(stats.toJson(), {
          'ticks': 1000,
          'lateTicks': 2,
          'skippedRequests': 1,
          'abortedTicks': 3,
          'droppedEvents': 4,
          'loads': 5,
          'failedLoads': 1,
          'tickTimeSumUs': 12000.0,
          'tickTimeMaxUs': 80.0,
          'tickTimeMeanUs': 12.0,
          'tickTimeP50Us': 11.0,
          'tickTimeP99Us': 40.0,
          'placement': 'fastCores',
          'priority': -19,
          'cpus': 0xF0,
          'qosClass': 0,
          'lastCpu': 7,
          'workerCores': 'fast',
          'lookahead': 3,
          'contextCreateUs': 300.0,
          'loadUs': 900.0,
          'memoryBytes': 1 << 20,
          'lastError': 'a.js: Error: no',
          'console': ['a.js: hello'],
        });
      });
    });

    group('toString()', () {
      test('sums up the ticks', () {
        expect(
          stats.toString(),
          'aud.script.modulator: 1000 ticks, mean 12.0 µs, p99 40.0 µs, max '
          '80.0 µs; 2 late, 1 skipped, 3 aborted; worker on fastCores',
        );
      });
    });
  });
}
