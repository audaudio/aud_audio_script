// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:io';

import 'package:aud_audio_core/aud_audio_core.dart';
import 'package:aud_audio_script/aud_audio_script.dart';
import 'package:test/test.dart';

void main() {
  group('AudScriptBench', () {
    group('syntheticScript, syntheticStressScript', () {
      test('compose one patch once and ten times', () {
        expect(
          AudScriptBench.syntheticScript,
          startsWith('var instances = 1;\n'),
        );
        expect(
          AudScriptBench.syntheticStressScript,
          'var instances = 10;\n'
          '${AudScriptBench.syntheticScript.substring(19)}',
        );
      });

      for (final (name, source) in [
        ('syntheticScript', AudScriptBench.syntheticScript),
        ('syntheticStressScript', AudScriptBench.syntheticStressScript),
      ]) {
        test('$name sends four controls on three outputs every tick', () {
          final simulation = AudScriptSimulation.simulate(
            source,
            blocks: 400,
            controlChanges: const [
              AudScriptControlChange(frame: 50000, number: 74, value: 100),
            ],
          );
          final values = <double>{};
          for (final tick in simulation.ticks) {
            expect(tick.status, AudScriptTickStatus.ok);
            expect(tick.sends.map((s) => (s.output, s.id)), [
              for (var output = 0; output < 3; ++output)
                for (var id = 300; id <= 303; ++id) (output, id),
            ]);
            for (final send in tick.sends) {
              expect(send.value, inInclusiveRange(0, 1));
              values.add(send.value);
            }
          }
          // The envelopes move.
          expect(values.length, greaterThan(1000));
        });
      }
    });

    group('measure(source)', () {
      test('times the synthetic workload paced like a worker', () {
        final start = DateTime.now();
        final result = AudScriptBench.measure(
          AudScriptBench.syntheticScript,
          frames: 128,
          ticks: 30,
          warmupTicks: 3,
        );
        // 33 blocks of 2.67 ms.
        expect(
          DateTime.now().difference(start).inMilliseconds,
          greaterThanOrEqualTo(80),
        );
        expect(result.ticks, 30);
        expect(result.frames, 128);
        expect(result.sampleRate, 48000);
        expect(result.blockUs, closeTo(128 / 48000 * 1e6, 1e-6));
        expect(result.meanUs, greaterThan(0));
        expect(result.p50Us, lessThanOrEqualTo(result.p99Us));
        expect(result.p99Us, lessThanOrEqualTo(result.maxUs));
        expect(result.contextCreateUs, greaterThan(0));
        expect(result.loadUs, greaterThan(0));
        expect(result.memoryBytes, greaterThan(0));
        expect(result.aborted, 0);
        if (Platform.isMacOS) {
          expect(result.placement, AudScriptPlacement.qos);
          expect(result.lastCpu, greaterThanOrEqualTo(0));
        }
      });

      test('runs the stress variant back to back on the fast cores', () {
        final result = AudScriptBench.measure(
          AudScriptBench.syntheticStressScript,
          ticks: 20,
          warmupTicks: 0,
          paced: false,
          workerCores: AudScriptWorkerCores.fast,
          memoryLimitMb: 16,
          tickBudgetMs: 1000,
        );
        expect(result.ticks, 20);
        expect(result.aborted, 0);
      });

      test('throws for a script that does not load', () {
        expect(
          () => AudScriptBench.measure('this is no script', name: 'bad.js'),
          throwsA(
            isA<AudScriptException>()
                .having((e) => e.code, 'code', AUD_ERROR_FAILED)
                .having((e) => e.operation, 'operation', 'the bench of bad.js')
                .having(
                  (e) => e.message,
                  'message',
                  startsWith('bad.js: SyntaxError'),
                ),
          ),
        );
      });

      test('throws for arguments out of range', () {
        expect(
          () => AudScriptBench.measure('', ticks: 0),
          throwsA(
            isA<AudScriptException>()
                .having((e) => e.code, 'code', AUD_ERROR_INVALID_ARGUMENT)
                .having((e) => e.message, 'message', isEmpty),
          ),
        );
      });
    });
  });
}
