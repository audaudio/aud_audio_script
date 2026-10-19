// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'package:code_assets/code_assets.dart';
import 'package:hooks/hooks.dart';
import 'package:record_use/record_use.dart';

// Keeps the native library of the package - QuickJS-ng included - in a
// profile or release build only when the app uses it (interop-001): the
// members that call into the library are recorded with @RecordUse. Without
// recordings the library stays, since nothing says it is unused.
void main(List<String> args) async {
  await link(args, (input, output) async {
    // The recordings are a dependency: the hooks runner caches a link by
    // the recordings of the build that ran it first, so a build whose
    // recordings live elsewhere - other defines, another flavor - would
    // take the old answer.
    // ignore: deprecated_member_use
    final recordings = input.recordedUsagesFile;
    if (recordings != null) output.dependencies.add(recordings);
    if (usesScript(input.recordedUses)) {
      output.assets.code.addAll(input.assets.code);
    }
  });
}

/// The libraries of the recorded classes; test/hook/link_test.dart checks
/// them and [scriptMembers] against the sources, so that a rename cannot
/// drop the native library of an app that uses it.
const String registrationLibrary =
    'package:aud_audio_script/src/aud_audio_script_ffi.dart';

/// The library of `AudScriptModulator`.
const String modulatorLibrary =
    'package:aud_audio_script/src/aud_script_modulator.dart';

/// The library of `AudScriptSimulation`.
const String simulationLibrary =
    'package:aud_audio_script/src/aud_script_simulation.dart';

/// The library of `AudScriptBench`.
const String benchLibrary =
    'package:aud_audio_script/src/aud_script_bench.dart';

const _registration = Class('AudAudioScript', Library(registrationLibrary));

const _modulator = Class('AudScriptModulator', Library(modulatorLibrary));

const _simulation = Class('AudScriptSimulation', Library(simulationLibrary));

const _bench = Class('AudScriptBench', Library(benchLibrary));

/// The members of the package that call into its native library.
const List<DefinitionWithStaticCalls> scriptMembers = [
  Getter('register', _registration),
  Method('registerWith', _registration),
  Method('stats', _modulator),
  Method('resetStats', _modulator),
  Method('simulate', _simulation),
  Method('measure', _bench),
];

/// Whether an app with the recorded [uses] needs the native library.
bool usesScript(Recordings? uses) =>
    uses == null ||
    scriptMembers.any((member) => (uses.calls[member] ?? const []).isNotEmpty);
