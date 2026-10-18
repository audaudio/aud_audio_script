// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// #############################################################################
/// The JavaScript engines a modulator runs scripts on.
enum AudScriptEngine {
  /// QuickJS-ng, bundled, on every platform: a memory limit per context and
  /// a time budget per tick. The default.
  quickjs(0, 'quickjs'),

  /// The system's JavaScriptCore through its public C API, on Apple
  /// platforms only. It has no memory limit and no time budget - a script
  /// that runs away blocks the worker - and does not tell the memory of a
  /// context; for comparisons and as a fallback.
  javascriptCore(1, 'javascriptcore');

  /// Creates an engine with its native [id] and its [settingValue].
  const AudScriptEngine(this.id, this.settingValue);

  /// The engine of the native [id], `AUD_SCRIPT_ENGINE_*`; QuickJS-ng for
  /// an id the package does not know.
  factory AudScriptEngine.fromId(int id) =>
      values.firstWhere((engine) => engine.id == id, orElse: () => quickjs);

  // ...........................................................................
  /// The native id, `AUD_SCRIPT_ENGINE_*`.
  final int id;

  /// The value of the string setting `engine` that selects it.
  final String settingValue;
}
