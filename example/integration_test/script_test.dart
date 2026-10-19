// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// Measures the modulator on a simulator, an emulator or a device (ticket
// 31, scope 3) and prints one line of JSON per measurement:
//
//   flutter test integration_test/script_test.dart -d <device> \
//     --dart-define=AUD_MEASURE_SECONDS=10
//
// - SCRIPT_BENCH: the synthetic workload and its tenfold stress variant on
//   the bench, paced at the block of the engine and at 48 kHz, 256 frames,
//   on QuickJS-ng;
// - SCRIPT_LIVE: the modulator with the synthetic workload on the running
//   engine for AUD_MEASURE_SECONDS (default 10): its statistics.

// ignore_for_file: avoid_print

import 'dart:convert';

import 'package:aud_audio_script/aud_audio_script.dart';
import 'package:aud_audio_script_example/main.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';

const _seconds = int.fromEnvironment('AUD_MEASURE_SECONDS', defaultValue: 10);

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  testWidgets('measures the modulator', (tester) async {
    await tester.pumpWidget(const ScriptApp());
    final state = tester.state<ScriptAppState>(find.byType(ScriptApp));
    final format = state.engine.format;
    final frames = format.bufferFrames > 0 ? format.bufferFrames : 256;
    final sampleRate = format.sampleRate > 0 ? format.sampleRate : 48000.0;
    for (final (name, source) in [
      ('synthetic', AudScriptBench.syntheticScript),
      ('stress', AudScriptBench.syntheticStressScript),
    ]) {
      for (final (rate, block) in [(sampleRate, frames), (48000.0, 256)]) {
        final result = AudScriptBench.measure(
          source,
          name: name,
          sampleRate: rate,
          frames: block,
          ticks: 2000,
        );
        print(
          'SCRIPT_BENCH ${jsonEncode({'script': name, ...result.toJson()})}',
        );
      }
    }
    AudScriptModulator.resetStats();
    await tester.runAsync(
      () => Future<void>.delayed(const Duration(seconds: _seconds)),
    );
    final stats = AudScriptModulator.stats();
    print('SCRIPT_LIVE ${jsonEncode(stats.toJson())}');
    expect(stats.ticks, isPositive);
  });
}
