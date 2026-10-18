// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// #############################################################################
/// Where the worker thread of a modulator or of the bench runs.
enum AudScriptPlacement {
  /// Where the system put it: no worker placed itself yet, or the platform
  /// has no placement, e.g. a device with one cluster of cores.
  system(0),

  /// Android and Linux: bound to the fast cores.
  fastCores(1),

  /// Apple platforms: the quality of service class user-interactive.
  qos(2),

  /// The system refused the binding to the fast cores.
  refused(3);

  /// Creates a placement with its native [id].
  const AudScriptPlacement(this.id);

  /// The placement of the native [id], `AUD_SCRIPT_PLACEMENT_*`; [system]
  /// for an id the package does not know.
  factory AudScriptPlacement.fromId(int id) => values.firstWhere(
    (placement) => placement.id == id,
    orElse: () => system,
  );

  // ...........................................................................
  /// The native id, `AUD_SCRIPT_PLACEMENT_*`.
  final int id;
}
