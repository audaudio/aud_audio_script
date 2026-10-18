// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'package:aud_audio_script/aud_audio_script.dart';
import 'package:test/test.dart';

void main() {
  const result = AudScriptBenchResult(
    engine: AudScriptEngine.quickjs,
    sampleRate: 48000,
    frames: 256,
    ticks: 1000,
    blockUs: 5333.3,
    meanUs: 20,
    p50Us: 18,
    p99Us: 53.333,
    maxUs: 90,
    overBlock: 1,
    aborted: 2,
    contextCreateUs: 250,
    loadUs: 700,
    memoryBytes: 300000,
    placement: AudScriptPlacement.qos,
    priority: 0,
    cpus: 0,
    qosClass: 33,
  );

  group('AudScriptBenchResult', () {
    test('tells the 99th percentile as a share of the block', () {
      expect(result.p99Share, closeTo(0.01, 1e-4));
    });

    group('toJson()', () {
      test('writes every field', () {
        expect(result.toJson(), {
          'engine': 'quickjs',
          'sampleRate': 48000.0,
          'frames': 256,
          'ticks': 1000,
          'overBlock': 1,
          'aborted': 2,
          'blockUs': 5333.3,
          'meanUs': 20.0,
          'p50Us': 18.0,
          'p99Us': 53.333,
          'maxUs': 90.0,
          'p99Share': result.p99Share,
          'contextCreateUs': 250.0,
          'loadUs': 700.0,
          'memoryBytes': 300000,
          'placement': 'qos',
          'priority': 0,
          'cpus': 0,
          'qosClass': 33,
        });
      });
    });

    group('toString()', () {
      test('sums up the measurement', () {
        expect(
          result.toString(),
          'quickjs: mean 20.0 µs, p50 18.0 µs, p99 53.3 µs, max 90.0 µs per '
          'tick of a block of 5333.3 µs (256 frames at 48000 Hz); 1 over the '
          'block, 2 aborted; context 250.0 µs, load 700.0 µs, 300000 bytes; '
          'qos',
        );
      });
    });
  });
}
