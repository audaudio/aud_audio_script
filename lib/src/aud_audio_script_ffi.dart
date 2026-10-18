// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:ffi';

import 'package:aud_audio_core/aud_audio_core.dart';
import 'package:aud_audio_core/aud_audio_core_bindings.dart' as core;
import 'package:meta/meta.dart';

import 'aud_audio_script_bindings_generated.dart' as bindings;
import 'aud_script_exception.dart';

// #############################################################################
/// The registration of the package's node, `aud.script.modulator`, with an
/// engine. The package is optional: no other package of the engine depends
/// on it, an app that runs scripts adds it.
///
/// [register] and [registerWith] are recorded (`@RecordUse`): in a profile
/// or release build the link hook of the package keeps its native library,
/// QuickJS-ng included, only when an app uses one of the package's native
/// members.
///
/// ```dart
/// final engine = AudEngine(packages: [
///   AudNativeNodePackage(AudAudioScript.name, AudAudioScript.register),
/// ]);
/// ```
abstract final class AudAudioScript {
  // ...........................................................................
  /// The name of the package, e.g. for an engine's package list.
  static const String name = 'aud_audio_script';

  // ...........................................................................
  /// The register function of the package,
  /// `aud_audio_script_register(const AudHostApi*)`, which an engine calls
  /// with its host api.
  @RecordUse()
  static core.AudRegisterFunction get register =>
      Native.addressOf<NativeFunction<core.AudRegisterFunctionFunction>>(
        bindings.aud_audio_script_register,
      );

  /// Registers the node type with the engine behind [host], e.g.
  /// `AudGraphFfi.hostApi`; throws an [AudScriptException] when the engine
  /// refuses it.
  @RecordUse()
  static void registerWith(Pointer<core.AudHostApi> host) {
    final result = bindings.aud_audio_script_register(host);
    if (result != AUD_OK) throw AudScriptException(code: result);
  }
}
