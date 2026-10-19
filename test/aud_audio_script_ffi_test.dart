// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:ffi';

import 'package:aud_audio_core/aud_audio_core.dart';
import 'package:aud_audio_core/aud_audio_core_bindings.dart' as core;
import 'package:aud_audio_graph/aud_audio_graph_ffi.dart';
import 'package:aud_audio_script/aud_audio_script.dart';
import 'package:test/test.dart';

void main() {
  // A graph of the engine with the package registered, disposed at the end
  // of the test.
  AudGraphFfi scriptGraph() {
    final graph = AudGraphFfi(listen: false, maxFrames: 256);
    addTearDown(graph.dispose);
    AudAudioScript.registerWith(graph.hostApi);
    return graph;
  }

  group('AudAudioScript', () {
    group('registerWith(host)', () {
      test('registers with an engine once', () {
        final graph = scriptGraph();
        expect(AudAudioScript.name, 'aud_audio_script');
        expect(graph.nodeType(AudScriptModulator.typeId), isNotNull);
        expect(
          () => AudAudioScript.registerWith(graph.hostApi),
          throwsA(
            isA<AudScriptException>()
                .having((e) => e.code, 'code', AUD_ERROR_DUPLICATE_TYPE)
                .having((e) => e.operation, 'operation', 'registration')
                .having(
                  (e) => e.toString(),
                  'text',
                  'AudScriptException: registration failed with '
                      'AUD_ERROR_DUPLICATE_TYPE',
                ),
          ),
        );
      });

      test('declares the node the Dart constants name', () {
        final declared = scriptGraph().nodeType(AudScriptModulator.typeId)!;
        expect(declared.version, AudScriptModulator.version);
        expect(declared.vendor, 'Audanika');
        expect(declared.abiMajor, AudAbi.major);
        expect(declared.abiMinor, AudAbi.minor);
        expect(declared.inputBuses, isEmpty);
        expect(declared.outputBuses, isEmpty);
        expect(declared.eventInputs.map((p) => (p.id, p.midi, p.control)), [
          (AudScriptModulator.input, true, true),
        ]);
        expect(declared.eventOutputs.map((p) => (p.id, p.midi, p.control)), [
          for (var i = 0; i < AudScriptModulator.outputs; ++i)
            (AudScriptModulator.output(i), false, true),
        ]);
        expect(declared.stringKeys.map((k) => (k.key, k.id)), [
          for (final (i, id) in AudScriptModulator.strings.indexed) (i, id),
        ]);
        expect(declared.params.map((p) => p.id), AudScriptModulator.params);
        final mirrors = AudScriptModulator.paramDescriptors;
        expect(mirrors.map((p) => p.id), AudScriptModulator.params);
        for (final (index, mirror) in mirrors.indexed) {
          final param = declared.params[index];
          expect(param.name, mirror.name, reason: mirror.id);
          expect(param.unit, mirror.unit, reason: mirror.id);
          expect(param.min, mirror.min, reason: mirror.id);
          expect(param.max, mirror.max, reason: mirror.id);
          expect(param.defaultValue, mirror.defaultValue, reason: mirror.id);
          expect(param.flags, mirror.flags, reason: mirror.id);
          expect(param.steps, mirror.steps, reason: mirror.id);
        }
        final capabilities = declared.capabilities;
        expect(capabilities.variableBlock, isTrue);
        expect(capabilities.events, isTrue);
        expect(capabilities.strings, isTrue);
        expect(capabilities.transport, isTrue);
        expect(capabilities.eventOutput, isTrue);
        expect(capabilities.inPlace, isFalse);
        expect(capabilities.state, isFalse);
      });
    });

    group('register', () {
      test('hands its register function to an engine', () {
        final graph = AudGraphFfi(listen: false, maxFrames: 256);
        addTearDown(graph.dispose);
        final register = AudAudioScript.register
            .asFunction<int Function(Pointer<core.AudHostApi>)>();
        expect(register(graph.hostApi), AUD_OK);
        expect(graph.nodeType(AudScriptModulator.typeId), isNotNull);
        expect(register(nullptr), AUD_ERROR_INVALID_ARGUMENT);
      });
    });
  });
}
