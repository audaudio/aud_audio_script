// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'package:aud_audio_script/aud_audio_script.dart';
import 'package:test/test.dart';

void main() {
  group('AudScriptTick', () {
    test('holds a tick of a simulation', () {
      const tick = AudScriptTick(
        tick: 3,
        beat: 1.5,
        offset: 12,
        sends: [AudScriptSend(output: 0, id: 300, value: 0.25)],
      );
      expect(tick.timeUs, 0);
      expect(tick.status, AudScriptTickStatus.ok);
      expect(tick.toString(), 'tick 3 at beat 1.5, frame 12: out0 300 = 0.25');
    });
  });

  group('AudScriptTickStatus', () {
    test('follows the native order of AUD_SCRIPT_TICK_*', () {
      expect(AudScriptTickStatus.values.map((s) => s.index), [0, 1, 2]);
      expect(AudScriptTickStatus.values.map((s) => s.name), [
        'ok',
        'interrupted',
        'failed',
      ]);
    });
  });
}
