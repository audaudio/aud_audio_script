// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:ffi';

import 'package:aud_audio_core/aud_audio_core.dart';
import 'package:aud_audio_script/src/aud_audio_script_bindings_generated.dart'
    as bindings;
import 'package:ffi/ffi.dart';
import 'package:test/test.dart';

void main() {
  group('aud_audio_script_bindings_generated.dart', () {
    test('aud_audio_script_register refuses a null host', () {
      expect(
        bindings.aud_audio_script_register(nullptr),
        AUD_ERROR_INVALID_ARGUMENT,
      );
    });

    test('aud_audio_script_stats refuses a short struct', () {
      final stats = calloc<bindings.AudScriptStats>();
      addTearDown(() => calloc.free(stats));
      stats.ref.struct_size = 4;
      expect(
        bindings.aud_audio_script_stats(stats),
        AUD_ERROR_INVALID_ARGUMENT,
      );
    });

    test('aud_audio_script_simulate refuses a null request', () {
      final result = calloc<Pointer<bindings.AudScriptSimulation>>();
      addTearDown(() => calloc.free(result));
      expect(
        bindings.aud_audio_script_simulate(nullptr, result),
        AUD_ERROR_INVALID_ARGUMENT,
      );
    });

    test('aud_audio_script_bench_run refuses a null request', () {
      final result = calloc<bindings.AudScriptBenchResult>();
      addTearDown(() => calloc.free(result));
      result.ref.struct_size = sizeOf<bindings.AudScriptBenchResult>();
      expect(
        bindings.aud_audio_script_bench_run(nullptr, result),
        AUD_ERROR_INVALID_ARGUMENT,
      );
    });

    test('names the type id of the node', () {
      expect(bindings.AUD_SCRIPT_MODULATOR_TYPE_ID, 'aud.script.modulator');
    });
  });
}
