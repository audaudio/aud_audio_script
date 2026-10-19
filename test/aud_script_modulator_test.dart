// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:io';

import 'package:aud_audio_core/aud_audio_core.dart';
import 'package:aud_audio_graph/aud_audio_graph_ffi.dart';
import 'package:aud_audio_script/aud_audio_script.dart';
import 'package:test/test.dart';

void main() {
  // Renders [source] offline for [blocks] blocks of 256 frames on a graph
  // with a modulator; the load's result.
  AudLoad render(String source, {int blocks = 20, String name = 'test.js'}) {
    final graph = AudGraphFfi(listen: false, maxFrames: 256);
    addTearDown(graph.dispose);
    AudAudioScript.registerWith(graph.hostApi);
    final node = graph.createNode(AudScriptModulator.typeId);
    graph
      ..setParam(node, AudScriptModulator.mode(0), AudScriptMode.step.value)
      ..transaction((tx) {})
      ..start()
      ..setString(node, AudScriptModulator.scriptName, name);
    final load = graph.setString(node, AudScriptModulator.script, source);
    expect(
      graph.waitLoads(node: node, timeout: const Duration(seconds: 20)),
      isTrue,
    );
    AudOfflineRenderer(graph).render(frames: 256 * blocks, blockFrames: 256);
    return graph.loadOf(load)!;
  }

  setUp(AudScriptModulator.resetStats);
  tearDown(AudScriptModulator.resetStats);

  group('AudScriptModulator', () {
    test('names the node, its events, settings and parameters', () {
      expect(AudScriptModulator.typeId, 'aud.script.modulator');
      expect(AudScriptModulator.input, 'in');
      expect(AudScriptModulator.outputs, 16);
      expect(AudScriptModulator.output(15), 'out15');
      expect(AudScriptModulator.maxId, 511);
      expect(AudScriptModulator.maxValues, 1024);
      expect(AudScriptModulator.strings, ['script', 'script_name', 'engine']);
      expect(AudScriptModulator.mode(3), 'mode3');
      expect(AudScriptModulator.params, [
        for (var i = 0; i < 16; ++i) AudScriptModulator.mode(i),
        AudScriptModulator.memoryLimit,
        AudScriptModulator.tickBudget,
        AudScriptModulator.lookahead,
      ]);
    });

    group('paramDescriptors', () {
      test('describes the parameters in the order of their ids', () {
        final descriptors = AudScriptModulator.paramDescriptors;
        expect(descriptors.map((p) => p.id), AudScriptModulator.params);
        expect(
          descriptors.first,
          isA<AudParamDescriptor>()
              .having((p) => p.name, 'name', 'Mode of output 0')
              .having((p) => p.defaultValue, 'default', 0)
              .having((p) => p.steps, 'steps', 2),
        );
        expect(
          descriptors.map((p) => (p.id, p.min, p.max, p.defaultValue)).skip(16),
          [
            ('memory_limit', 1, 1024, 32),
            ('tick_budget', 1, 10000, 100),
            ('lookahead', 1, 4, 2),
          ],
        );
      });
    });

    group('engineAvailable(engine)', () {
      test('tells which engines run here', () {
        expect(
          AudScriptModulator.engineAvailable(AudScriptEngine.quickjs),
          isTrue,
        );
        expect(
          AudScriptModulator.engineAvailable(AudScriptEngine.javascriptCore),
          Platform.isMacOS || Platform.isIOS,
        );
      });
    });

    group('stats()', () {
      test('counts the ticks of the modulators of the process', () {
        final load = render(
          'aud.onTick = function () {\n'
          '  aud.send(0, 300, aud.time.tick / 100);\n'
          '  console.log("tick", aud.time.tick);\n'
          '};\n',
        );
        expect(load.result, 0);
        final stats = AudScriptModulator.stats();
        expect(stats.ticks, greaterThanOrEqualTo(19));
        expect(stats.lateTicks, 0);
        expect(stats.abortedTicks, 0);
        expect(stats.lookahead, 2);
        expect(stats.loads, 1);
        expect(stats.failedLoads, 0);
        expect(stats.engine, AudScriptEngine.quickjs);
        expect(stats.tickTimeSumUs, greaterThan(0));
        expect(stats.tickTimeMeanUs, greaterThan(0));
        expect(stats.tickTimeP50Us, greaterThan(0));
        expect(stats.tickTimeP99Us, greaterThanOrEqualTo(stats.tickTimeP50Us));
        expect(stats.tickTimeMaxUs, greaterThan(0));
        expect(stats.contextCreateUs, greaterThan(0));
        expect(stats.loadUs, greaterThan(0));
        expect(stats.memoryBytes, greaterThan(0));
        expect(stats.console.take(2), ['test.js: tick 1', 'test.js: tick 2']);
        expect(stats.lastError, isEmpty);
        if (Platform.isMacOS) {
          expect(stats.placement, AudScriptPlacement.qos);
          expect(stats.qosClass, isPositive);
        }
      });

      test('reads a long last error', () {
        final load = render(
          'aud.onTick = function () { throw new Error("x".repeat(400)); };',
          blocks: 3,
        );
        expect(load.result, 0);
        final stats = AudScriptModulator.stats();
        expect(stats.abortedTicks, greaterThanOrEqualTo(2));
        expect(stats.lastError, startsWith('test.js: Error: ${'x' * 400}'));
      });

      test('tells a failed load', () {
        final load = render('aud.onTick = function ( {', blocks: 2);
        expect(load.result, isNot(0));
        expect(load.message, startsWith('test.js: SyntaxError'));
        final stats = AudScriptModulator.stats();
        expect(stats.failedLoads, 1);
        expect(stats.lastError, startsWith('test.js: SyntaxError'));
      });
    });

    group('resetStats()', () {
      test('resets the counters, the error and the console', () {
        render(
          'aud.onTick = function () { console.log(1); missing(); };',
          blocks: 3,
        );
        expect(AudScriptModulator.stats().ticks, isPositive);
        AudScriptModulator.resetStats();
        final stats = AudScriptModulator.stats();
        expect(stats.ticks, 0);
        expect(stats.lastError, isEmpty);
        expect(stats.console, isEmpty);
      });
    });
  });
}
