// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// #############################################################################
/// A MIDI control change at an absolute frame of a simulation: it reaches
/// `aud.onControlChange` before the tick of the block after its own.
class AudScriptControlChange {
  /// Creates a control change.
  const AudScriptControlChange({
    required this.frame,
    this.channel = 0,
    required this.number,
    required this.value,
  });

  // ...........................................................................
  @override
  bool operator ==(Object other) =>
      other is AudScriptControlChange &&
      other.frame == frame &&
      other.channel == channel &&
      other.number == number &&
      other.value == value;

  @override
  int get hashCode => Object.hash(frame, channel, number, value);

  @override
  String toString() => 'CC $number = $value on channel $channel at $frame';

  // ...........................................................................
  /// The frame from the start of the simulation.
  final int frame;

  /// The MIDI channel, 0 to 15.
  final int channel;

  /// The controller, 0 to 127.
  final int number;

  /// The value, 0 to 127.
  final int value;
}
