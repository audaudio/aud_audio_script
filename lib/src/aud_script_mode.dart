// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// #############################################################################
/// How the values of an output of a modulator enter the block, the value
/// of its parameter `mode<n>`.
enum AudScriptMode {
  /// One event at the last frame of the block with the new value: a
  /// sampler draws a line from the old value over the block, and the value
  /// arrives one block later. The default.
  ramp(0),

  /// The old value one frame before the grid point and the new value at
  /// it, so that a sampler steps at the beat without a ramp. A value that
  /// arrives late steps at the first frame.
  step(1);

  /// Creates a mode with its parameter [value].
  const AudScriptMode(this.value);

  // ...........................................................................
  /// The value of the parameter.
  final double value;
}
