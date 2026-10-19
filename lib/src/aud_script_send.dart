// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// #############################################################################
/// One `aud.send(output, id, value)` call of a script.
class AudScriptSend {
  /// Creates a send.
  const AudScriptSend({
    required this.output,
    required this.id,
    required this.value,
  });

  // ...........................................................................
  @override
  bool operator ==(Object other) =>
      other is AudScriptSend &&
      other.output == output &&
      other.id == id &&
      other.value == value;

  @override
  int get hashCode => Object.hash(output, id, value);

  @override
  String toString() => 'out$output $id = $value';

  // ...........................................................................
  /// The event output, 0 to 15.
  final int output;

  /// The control id, 0 to 511.
  final int id;

  /// The value as the script sent it; the node emits it as a float.
  final double value;
}
