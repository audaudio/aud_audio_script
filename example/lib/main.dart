// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:async';

import 'package:aud_audio/aud_audio_ffi.dart';
import 'package:aud_audio_script/aud_audio_script.dart';
import 'package:flutter/material.dart';

void main() => runApp(const ScriptApp());

// #############################################################################
/// The example of `aud_audio_script` on the engine of `aud_audio`: a
/// modulator runs the synthetic workload of the package while the transport
/// plays; the page shows its statistics and runs the bench.
class ScriptApp extends StatefulWidget {
  /// Creates the app; [config] replaces the engine's configuration.
  const ScriptApp({super.key, this.config = const AudEngineConfig()});

  /// The configuration of the engine.
  final AudEngineConfig config;

  @override
  State<ScriptApp> createState() => ScriptAppState();
}

// #############################################################################
/// The state of [ScriptApp]: the engine with the package registered and the
/// modulator.
class ScriptAppState extends State<ScriptApp> {
  /// The engine.
  late final AudEngine engine = AudEngine(
    config: widget.config,
    packages: [
      AudNativeNodePackage(AudAudioScript.name, AudAudioScript.register),
    ],
  );

  /// The modulator; its outputs lead nowhere, the statistics tell what it
  /// does.
  late final AudNode modulator;

  /// The statistics of the modulators, read every second.
  AudScriptStats stats = const AudScriptStats();

  /// The result of the last bench, or what failed.
  String bench = '';

  late final Timer _refresh;

  @override
  void initState() {
    super.initState();
    engine.prepare();
    final graph = engine.graph;
    modulator = graph.createNode(AudScriptModulator.typeId);
    graph
      ..transaction((tx) {})
      ..setString(modulator, AudScriptModulator.scriptName, 'synthetic.js')
      ..setString(
        modulator,
        AudScriptModulator.script,
        AudScriptBench.syntheticScript,
      )
      ..transport(const AudTransportRequest.setTempo(120))
      ..transport(const AudTransportRequest.start());
    engine.start();
    _refresh = Timer.periodic(
      const Duration(seconds: 1),
      (_) => setState(() => stats = AudScriptModulator.stats()),
    );
  }

  @override
  void dispose() {
    _refresh.cancel();
    engine.dispose();
    super.dispose();
  }

  /// Runs the bench of the synthetic workload on [engine].
  void runBench(AudScriptEngine scriptEngine) {
    try {
      final result = AudScriptBench.measure(
        AudScriptBench.syntheticScript,
        engine: scriptEngine,
        ticks: 500,
      );
      setState(() => bench = '$result');
    } on AudScriptException catch (e) {
      setState(() => bench = '$e');
    }
  }

  @override
  Widget build(BuildContext context) => MaterialApp(
    title: 'aud_audio_script',
    theme: ThemeData(colorSchemeSeed: Colors.indigo, useMaterial3: true),
    home: Scaffold(
      appBar: AppBar(title: const Text('aud_audio_script')),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          Text('$stats', key: const Key('stats')),
          if (stats.lastError.isNotEmpty) Text(stats.lastError),
          const SizedBox(height: 16),
          Wrap(
            spacing: 8,
            children: [
              for (final scriptEngine in AudScriptEngine.values)
                if (AudScriptModulator.engineAvailable(scriptEngine))
                  FilledButton(
                    onPressed: () => runBench(scriptEngine),
                    child: Text('Bench ${scriptEngine.name}'),
                  ),
            ],
          ),
          const SizedBox(height: 16),
          Text(bench, key: const Key('bench')),
        ],
      ),
    ),
  );
}
