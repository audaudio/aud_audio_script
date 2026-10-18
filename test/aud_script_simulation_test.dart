// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'package:aud_audio_core/aud_audio_core.dart';
import 'package:aud_audio_script/aud_audio_script.dart';
import 'package:test/test.dart';

void main() {
  group('AudScriptSimulation', () {
    group('simulate(source)', () {
      test('runs a tick per block on the grid of the beat', () {
        final simulation = AudScriptSimulation.simulate(
          'aud.send(1, 2, 0.5);\n'
          'var seen = 0;\n'
          'aud.onControlChange = function (c, n, v) { seen = c * 1000 + n + v; };\n'
          'aud.onTick = function () {\n'
          '  aud.send(0, 300, seen);\n'
          '  if (aud.time.tick === 3) { console.log("three"); missing(); }\n'
          '  if (aud.time.tick === 4) { for (;;) {} }\n'
          '};\n',
          name: 'grid.js',
          frames: 256,
          blocks: 6,
          tickBudgetMs: 20,
          controlChanges: const [
            AudScriptControlChange(
              frame: 300,
              channel: 2,
              number: 74,
              value: 9,
            ),
          ],
        );
        expect(simulation.contextCreateUs, greaterThan(0));
        expect(simulation.loadUs, greaterThan(0));
        expect(simulation.ticks.map((t) => t.tick), [1, 2, 3, 4, 5]);
        expect(simulation.ticks.map((t) => t.status), [
          AudScriptTickStatus.ok,
          AudScriptTickStatus.ok,
          AudScriptTickStatus.failed,
          AudScriptTickStatus.interrupted,
          AudScriptTickStatus.ok,
        ]);
        // 256 frames at 120 BPM: 94 grid points per beat, the grid point
        // of block 1 at frame 254.
        expect(simulation.ticks.first.beat, closeTo(2 / 94, 1e-12));
        expect(simulation.ticks.first.offset, 254);
        expect(simulation.ticks.first.sends, const [
          AudScriptSend(output: 1, id: 2, value: 0.5),
          AudScriptSend(output: 0, id: 300, value: 0),
        ]);
        // The change at frame 300 lies in block 1: tick 2 sees it.
        expect(simulation.ticks[1].sends, const [
          AudScriptSend(output: 0, id: 300, value: 2083),
        ]);
        expect(simulation.ticks[2].sends, isEmpty);
        expect(simulation.ticks[3].sends, isEmpty);
        expect(simulation.console, ['grid.js: three']);
        expect(simulation.error, contains('interrupted'));
        expect(simulation.ticks.first.timeUs, greaterThan(0));
      });

      test('throws the error of a script that does not load', () {
        expect(
          () => AudScriptSimulation.simulate('throw 1;', blocks: 4),
          throwsA(
            isA<AudScriptException>()
                .having((e) => e.code, 'code', AUD_ERROR_FAILED)
                .having(
                  (e) => e.operation,
                  'operation',
                  'the simulation of script',
                )
                .having((e) => e.message, 'message', 'script: 1'),
          ),
        );
      });

      test('throws for arguments out of range', () {
        expect(
          () => AudScriptSimulation.simulate('', blocks: 4, frames: 0),
          throwsA(
            isA<AudScriptException>().having(
              (e) => e.code,
              'code',
              AUD_ERROR_INVALID_ARGUMENT,
            ),
          ),
        );
      });
    });
  });
}
