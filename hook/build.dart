// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:io';
import 'dart:isolate';

import 'package:code_assets/code_assets.dart';
import 'package:hooks/hooks.dart';
import 'package:logging/logging.dart';
import 'package:native_toolchain_c/native_toolchain_c.dart';

// Builds the modulator (ticket 31) in two steps: the vendored QuickJS-ng as
// C into a static library, then the package as C++17 against the
// header-only ABI of aud_audio_core, resolved through the package config,
// linked with that library into the one dynamic library of the package; the
// package never links the engine (build-001). A single compiler call
// cannot take C and C++17 at once, and a static library that no asset
// names stays inside the hook's output. The flags follow aud_dsp_effects:
// hidden symbols - only the entry points of aud_audio_script.h are
// visible, so that QuickJS-ng never clashes with another copy in the app
// -, on Android 16 KB pages, the static C++ runtime kept inside the
// library, unused code dropped and no undefined symbol left. On Apple
// platforms the library links the system's JavaScriptCore for the second
// engine.
void main(List<String> args) async {
  await build(args, (input, output) async {
    if (!input.config.buildCodeAssets) return;
    final packageName = input.packageName;
    final targetOS = input.config.code.targetOS;
    final android = targetOS == OS.android;
    final apple = targetOS == OS.iOS || targetOS == OS.macOS;
    final windows = targetOS == OS.windows;
    final logger = Logger('')
      ..level = Level.ALL
      ..onRecord.listen((record) => stdout.writeln(record.message));
    final quickjs = CBuilder.library(
      name: quickjsLibrary,
      sources: quickjsSources,
      includes: [quickjsDirectory],
      std: windows ? null : 'gnu11',
      linkModePreference: LinkModePreference.static,
      optimizationLevel: OptimizationLevel.o2,
      defines: {'_GNU_SOURCE': null},
      flags: [
        if (windows) ...[
          '/std:c11',
          '/experimental:c11atomics',
        ] else ...[
          '-fvisibility=hidden',
          '-ffunction-sections',
          '-fdata-sections',
          '-w',
        ],
      ],
    );
    await quickjs.run(input: input, output: output, logger: logger);
    final library = CBuilder.library(
      name: packageName,
      assetName: 'src/${packageName}_bindings_generated.dart',
      sources: sources,
      includes: [
        'src',
        quickjsDirectory,
        await packageSrcDirectory('aud_audio_core'),
      ],
      language: Language.cpp,
      std: 'c++17',
      cppLinkStdLib: android ? 'c++_static' : null,
      frameworks: [
        if (apple) ...['Foundation', 'JavaScriptCore'],
      ],
      flags: [
        if (!windows) ...['-fvisibility=hidden', '-fvisibility-inlines-hidden'],
        if (android) ...[
          '-Wl,-z,max-page-size=16384',
          // The static C++ runtime and QuickJS-ng stay inside the library.
          '-Wl,--exclude-libs,ALL',
          // Code nothing calls leaves the library.
          '-ffunction-sections',
          '-fdata-sections',
          '-Wl,--gc-sections',
          // A symbol the library leaves undefined fails the link, not the
          // load in the app.
          '-Wl,--no-undefined',
        ],
        if (apple) '-Wl,-dead_strip',
      ],
      libraries: [
        quickjsLibrary,
        if (android) 'm',
        if (targetOS == OS.linux) ...['m', 'pthread'],
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

/// The folder of the vendored QuickJS-ng, relative to the package root.
const String quickjsDirectory = 'src/third_party/quickjs-ng';

/// The C sources of QuickJS-ng, relative to the package root.
const List<String> quickjsSources = [
  '$quickjsDirectory/dtoa.c',
  '$quickjsDirectory/libregexp.c',
  '$quickjsDirectory/libunicode.c',
  '$quickjsDirectory/quickjs.c',
];

/// The C++ sources of the package, relative to the package root.
const List<String> sources = [
  'src/aud_audio_script.cpp',
  'src/aud_script_bench.cpp',
  'src/aud_script_engine.cpp',
  'src/aud_script_jsc.cpp',
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
