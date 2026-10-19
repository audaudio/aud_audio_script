// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'package:aud_audio_script/aud_audio_script.dart';
import 'package:test/test.dart';

void main() {
  group('AudScriptPlacement', () {
    group('AudScriptPlacement.fromId(id)', () {
      test('finds the placement of an id, system for others', () {
        expect(
          [for (var id = 0; id < 6; ++id) AudScriptPlacement.fromId(id)],
          [
            AudScriptPlacement.system,
            AudScriptPlacement.fastCores,
            AudScriptPlacement.qos,
            AudScriptPlacement.refused,
            AudScriptPlacement.anyCore,
            AudScriptPlacement.system,
          ],
        );
      });
    });
  });
}
