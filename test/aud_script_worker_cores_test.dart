// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'package:aud_audio_script/aud_audio_script.dart';
import 'package:test/test.dart';

void main() {
  group('AudScriptWorkerCores', () {
    test('carries the values of the parameter worker_cores', () {
      expect(AudScriptWorkerCores.values.map((c) => c.value), [0, 1]);
    });

    group('AudScriptWorkerCores.fromValue(value)', () {
      test('finds the choice of a value, any for others', () {
        expect(
          [
            for (final value in [0, 1, 0.7, 2, -1])
              AudScriptWorkerCores.fromValue(value),
          ],
          [
            AudScriptWorkerCores.any,
            AudScriptWorkerCores.fast,
            AudScriptWorkerCores.fast,
            AudScriptWorkerCores.any,
            AudScriptWorkerCores.any,
          ],
        );
      });
    });
  });
}
