// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// #############################################################################
/// The cores the worker of a modulator may run on, the value of its
/// parameter `worker_cores`; Android and Linux only, Apple platforms keep
/// the quality of service class user-interactive either way.
enum AudScriptWorkerCores {
  /// Any core, where the system puts it, with the raised priority. The
  /// default: on a big.LITTLE device whose idle big cores the kernel
  /// isolates, the audio thread wakes the worker onto its own core
  /// (wake-affine); bound to the fast cores the worker then waits there for
  /// the realtime audio thread, for several blocks when callbacks come back
  /// to back.
  any(0),

  /// Bound to the fast cores - the cores above the slowest clock - with the
  /// raised priority.
  fast(1);

  /// Creates a choice with its parameter [value].
  const AudScriptWorkerCores(this.value);

  /// The choice of the parameter [value]; [any] for a value the package
  /// does not know.
  factory AudScriptWorkerCores.fromValue(num value) =>
      value >= 0.5 && value < 1.5 ? fast : any;

  // ...........................................................................
  /// The value of the parameter.
  final int value;
}
