// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'package:aud_audio_core/aud_audio_core.dart';

// #############################################################################
/// Thrown when an engine refuses the node type of the package, when a
/// simulation or a bench cannot run, or when its script does not load.
class AudScriptException implements Exception {
  /// Creates the exception for a result [code] of [operation], with the
  /// text of a script error in [message].
  const AudScriptException({
    required this.code,
    this.operation = 'registration',
    this.message = '',
  });

  // ...........................................................................
  @override
  String toString() =>
      'AudScriptException: $operation failed with '
      '${AudAbi.resultName(code)}${message.isEmpty ? '' : ': $message'}';

  // ...........................................................................
  /// The result code, one of the `AUD_ERROR_*` constants.
  final int code;

  /// What failed, e.g. `registration`.
  final String operation;

  /// The error of the script, e.g. `script: SyntaxError: ...`; empty when
  /// no script failed.
  final String message;
}
