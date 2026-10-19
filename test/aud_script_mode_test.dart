// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'package:aud_audio_script/aud_audio_script.dart';
import 'package:test/test.dart';

void main() {
  group('AudScriptMode', () {
    test('carries the values of the mode parameters', () {
      expect(AudScriptMode.values.map((m) => m.value), [0, 1]);
      expect(AudScriptMode.ramp.value, 0);
      expect(AudScriptMode.step.value, 1);
    });
  });
}
