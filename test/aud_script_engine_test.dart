// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'package:aud_audio_script/aud_audio_script.dart';
import 'package:test/test.dart';

void main() {
  group('AudScriptEngine', () {
    test('carries the native ids and the setting values', () {
      expect(AudScriptEngine.values.map((e) => (e.id, e.settingValue)), [
        (0, 'quickjs'),
        (1, 'javascriptcore'),
      ]);
    });

    group('AudScriptEngine.fromId(id)', () {
      test('finds the engine of an id, QuickJS-ng for others', () {
        expect(AudScriptEngine.fromId(1), AudScriptEngine.javascriptCore);
        expect(AudScriptEngine.fromId(0), AudScriptEngine.quickjs);
        expect(AudScriptEngine.fromId(7), AudScriptEngine.quickjs);
      });
    });
  });
}
