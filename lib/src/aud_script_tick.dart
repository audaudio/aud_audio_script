// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'aud_script_send.dart';

// #############################################################################
/// How a tick ended.
enum AudScriptTickStatus {
  /// The tick ran.
  ok,

  /// The tick ran over its budget and was interrupted; its values were
  /// dropped.
  interrupted,

  /// The tick threw, an out of memory included; its values were dropped.
  failed,
}

// #############################################################################
/// One tick of a simulation: the block it was computed for, the beat of its
/// grid point and the frame of it inside the block, the time it took and
/// the sends of the script, in order.
class AudScriptTick {
  /// Creates a tick.
  const AudScriptTick({
    required this.tick,
    required this.beat,
    required this.offset,
    this.timeUs = 0,
    this.status = AudScriptTickStatus.ok,
    this.sends = const [],
  });

  // ...........................................................................
  @override
  String toString() =>
      'tick $tick at beat $beat, frame $offset: ${sends.join(', ')}';

  // ...........................................................................
  /// The number of the tick, the block it is computed for: 1, 2, ...
  final int tick;

  /// The beat of the grid point, `aud.time.beat`.
  final double beat;

  /// The frame of the grid point inside the block, k.
  final int offset;

  /// The time of the input callbacks and the tick in µs.
  final double timeUs;

  /// How the tick ended.
  final AudScriptTickStatus status;

  /// The `aud.send` calls of the tick, in order; those of the load come
  /// with the first tick, an aborted tick has none.
  final List<AudScriptSend> sends;
}
