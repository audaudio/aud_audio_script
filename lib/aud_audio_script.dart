// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

/// Script modulation of the Audanika Audio Engine: the node
/// `aud.script.modulator` runs JavaScript on QuickJS-ng in a worker thread
/// of its own and sends controls on its event outputs once per audio
/// block; the registration of the node, its statistics, a simulation
/// without a graph and a bench. Native platforms only; an app that runs
/// scripts adds the package.
library;

export 'src/aud_audio_script_ffi.dart';
export 'src/aud_audio_script_version.dart';
export 'src/aud_script_bench.dart';
export 'src/aud_script_bench_result.dart';
export 'src/aud_script_control_change.dart';
export 'src/aud_script_exception.dart';
export 'src/aud_script_mode.dart';
export 'src/aud_script_modulator.dart';
export 'src/aud_script_placement.dart';
export 'src/aud_script_send.dart';
export 'src/aud_script_simulation.dart';
export 'src/aud_script_stats.dart';
export 'src/aud_script_tick.dart';
