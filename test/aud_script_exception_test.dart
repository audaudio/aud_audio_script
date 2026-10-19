// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'package:aud_audio_core/aud_audio_core.dart';
import 'package:aud_audio_script/aud_audio_script.dart';
import 'package:test/test.dart';

void main() {
  group('AudScriptException', () {
    group('toString()', () {
      test('names the operation that failed', () {
        expect(
          const AudScriptException(
            code: AUD_ERROR_INVALID_ARGUMENT,
            operation: 'the bench of synthetic.js',
          ).toString(),
          'AudScriptException: the bench of synthetic.js failed with '
          'AUD_ERROR_INVALID_ARGUMENT',
        );
      });

      test('adds the error of the script', () {
        expect(
          const AudScriptException(
            code: AUD_ERROR_FAILED,
            operation: 'the simulation of a.js',
            message: 'a.js: SyntaxError: unexpected token',
          ).toString(),
          'AudScriptException: the simulation of a.js failed with '
          'AUD_ERROR_FAILED: a.js: SyntaxError: unexpected token',
        );
      });
    });
  });
}
