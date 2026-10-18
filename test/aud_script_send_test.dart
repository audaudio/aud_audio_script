// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'package:aud_audio_script/aud_audio_script.dart';
import 'package:test/test.dart';

void main() {
  group('AudScriptSend', () {
    test('compares by its fields', () {
      const send = AudScriptSend(output: 1, id: 300, value: 0.5);
      expect(send, const AudScriptSend(output: 1, id: 300, value: 0.5));
      expect(
        send == const AudScriptSend(output: 2, id: 300, value: 0.5),
        isFalse,
      );
      expect(
        send == const AudScriptSend(output: 1, id: 301, value: 0.5),
        isFalse,
      );
      expect(
        send == const AudScriptSend(output: 1, id: 300, value: 1),
        isFalse,
      );
      expect(
        send.hashCode,
        const AudScriptSend(output: 1, id: 300, value: 0.5).hashCode,
      );
      expect(send.toString(), 'out1 300 = 0.5');
    });
  });
}
