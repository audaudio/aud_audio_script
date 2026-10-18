// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'package:aud_audio_script/aud_audio_script.dart';
import 'package:test/test.dart';

void main() {
  group('AudScriptControlChange', () {
    test('compares by its fields', () {
      const change = AudScriptControlChange(
        frame: 480,
        channel: 1,
        number: 74,
        value: 90,
      );
      expect(
        change,
        const AudScriptControlChange(
          frame: 480,
          channel: 1,
          number: 74,
          value: 90,
        ),
      );
      for (final other in const [
        AudScriptControlChange(frame: 481, channel: 1, number: 74, value: 90),
        AudScriptControlChange(frame: 480, number: 74, value: 90),
        AudScriptControlChange(frame: 480, channel: 1, number: 7, value: 90),
        AudScriptControlChange(frame: 480, channel: 1, number: 74, value: 9),
      ]) {
        expect(change == other, isFalse, reason: '$other');
      }
      expect(
        change.hashCode,
        const AudScriptControlChange(
          frame: 480,
          channel: 1,
          number: 74,
          value: 90,
        ).hashCode,
      );
      expect(change.toString(), 'CC 74 = 90 on channel 1 at 480');
    });
  });
}
