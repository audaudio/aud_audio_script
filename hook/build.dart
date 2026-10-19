// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:io';
import 'dart:isolate';

import 'package:code_assets/code_assets.dart';
import 'package:gg_quickjs/hook.dart';
import 'package:hooks/hooks.dart';
import 'package:logging/logging.dart';
import 'package:native_toolchain_c/native_toolchain_c.dart';

// Builds the modulator (ticket 31) in two steps: QuickJS-ng from the
// package gg_quickjs as C into a static library (QuickjsNgBuild), then the
// package as C++17 against the header-only ABI of aud_audio_core, resolved
// through the package config, linked with that library into the one
// dynamic library of the package; the package never links the engine
// (build-001). A single compiler call cannot take C and C++17 at once, and
// a static library that no asset names stays inside the hook's output.
// The flags of QuickjsNgBuild.libraryFlags - hidden symbols, so that
// QuickJS-ng never clashes with another copy in the app, unused code
// dropped, the static libraries kept inside, on Android 16 KB pages - and
// those of aud_dsp_effects: hidden inline functions, on Android the static
// C++ runtime, a section per function of the package and no undefined
// symbol left. Only the entry points of aud_audio_script.h are visible.
void main(List<String> args) async {
  await build(args, (input, output) async {
    if (!input.config.buildCodeAssets) return;
    final packageName = input.packageName;
    final targetOS = input.config.code.targetOS;
    final android = targetOS == OS.android;
    final windows = targetOS == OS.windows;
    final logger = Logger('')
      ..level = Level.ALL
      ..onRecord.listen((record) => stdout.writeln(record.message));
    const quickjs = QuickjsNgBuild(libraryName: quickjsLibrary);
    await quickjs.run(input: input, output: output, logger: logger);
    final library = CBuilder.library(
      name: packageName,
      assetName: 'src/${packageName}_bindings_generated.dart',
      sources: sources,
      includes: [
        'src',
        await QuickjsNgBuild.includeDirectory(),
        await packageSrcDirectory('aud_audio_core'),
      ],
      language: Language.cpp,
      std: 'c++17',
      cppLinkStdLib: android ? 'c++_static' : null,
      flags: [
        ...QuickjsNgBuild.libraryFlags(targetOS),
        if (!windows) '-fvisibility-inlines-hidden',
        if (android) ...[
          // Code nothing calls leaves the library.
          '-ffunction-sections',
          '-fdata-sections',
          // A symbol the library leaves undefined fails the link, not the
          // load in the app.
          '-Wl,--no-undefined',
        ],
      ],
      libraries: [
        quickjs.libraryName,
        ...QuickjsNgBuild.systemLibraries(targetOS),
      ],
    );
    await library.run(
      input: input,
      output: output,
      // In profile and release builds the link hook decides whether the
      // app uses the modulator (interop-001).
      routing: [
        if (input.config.linkingEnabled)
          ToLinkHook(packageName)
        else
          const ToAppBundle(),
      ],
      logger: logger,
    );
  });
}

/// The name of the static library of QuickJS-ng inside the hook's output.
const String quickjsLibrary = 'aud_quickjs';

/// The C++ sources of the package, relative to the package root.
const List<String> sources = [
  'src/aud_audio_script.cpp',
  'src/aud_script_bench.cpp',
  'src/aud_script_engine.cpp',
  'src/aud_script_node.cpp',
  'src/aud_script_placement.cpp',
  'src/aud_script_quickjs.cpp',
  'src/aud_script_runner.cpp',
  'src/aud_script_simulate.cpp',
  'src/aud_script_stats.cpp',
];

/// The `src` directory of [package], resolved through the package config.
Future<String> packageSrcDirectory(String package) async {
  final lib = await Isolate.resolvePackageUri(Uri.parse('package:$package/'));
  if (lib == null) throw StateError('Package $package is not resolvable');
  return lib.resolve('../src/').toFilePath();
}
