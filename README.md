# aud_audio_script

English | [Deutsch](README.de.md)

[![CI](https://github.com/audaudio/aud_audio_script/actions/workflows/quick_check.yaml/badge.svg)](https://github.com/audaudio/aud_audio_script/actions/workflows/quick_check.yaml)

Script modulation of the Audanika Audio Engine: the node
aud.script.modulator runs JavaScript on QuickJS-ng in a worker of its own
and sends controls once per audio buffer; optional, apps add it explicitly.

The controls go out on 16 event outputs once per audio block - for example
breakpoint LFOs locked to the beat that drive the cutoff, resonance, gain
and pan of samplers. No other package of the engine depends on it, an app
that runs scripts adds it. It runs on iOS, Android, macOS and Windows,
Linux in principle, and needs no Flutter. Part of the Audanika Audio
Engine; planned in [aud_audio_pm](https://github.com/audaudio/aud_audio_pm).

## Motivation

Modulation written in JavaScript - envelopes, LFOs, arithmetic between
them, conditions on controllers - should run unchanged inside the engine,
without ever touching the audio thread. The package aims for:

- JavaScript never on the audio thread and never in Dart: a worker of the
  node runs the script; the audio thread only posts a semaphore, which
  never blocks, and exchanges requests and values through lock-free rings
- One tick per audio block in step with the callback, values on a grid
  locked to the beat, entering the block as a ramp or as a step
- A late worker never stalls the audio: the outputs hold, the late block is
  counted; offline renders wait for the worker and are deterministic
- A memory limit and a time budget per tick stop a runaway script; a load
  that fails keeps the old script
- The speed of an engine measurable on every device: a bench on a worker
  placed like the node's, a simulation without a graph, statistics of the
  running modulators
- An app that does not run scripts ships without QuickJS-ng (interop-001)

## Installation

The package is not published to pub.dev; add it from git, as the packages
of the engine reference each other:

```yaml
dependencies:
  aud_audio_script:
    git:
      url: git@github.com:audaudio/aud_audio_script.git
      tag_pattern: "{{version}}"
    version: ^0.2.0
```

An app depends on `aud_audio` and on the DSP packages it uses, and
registers them with its engine. The build hook compiles QuickJS-ng as C
from the pub.dev package `gg_quickjs` (through its `QuickjsNgBuild`) and
the node as C++17 with the toolchain of the target against the ABI header
of `aud_audio_core`, and links both into one library per architecture
(iOS, Android, macOS, Windows; Linux in principle).

## Usage

A script runs without a graph, tick by tick, in a simulation. It takes the
public synthetic workload of the package, a control change at frame 4800,
and returns every tick with its beat, its frame and the values it sent:

```dart
import 'package:aud_audio_script/aud_audio_script.dart';

void main() {
  final simulation = AudScriptSimulation.simulate(
    AudScriptBench.syntheticScript,
    sampleRate: 48000,
    frames: 256,
    tempo: 120,
    blocks: 100,
    controlChanges: const [
      AudScriptControlChange(frame: 4800, number: 74, value: 100),
    ],
  );
  print(simulation.ticks.first);
}
```

It prints:

```text
tick 1 at beat 0.02127659574468085, frame 254: out0 300 = 0.1877774311709331, out0 301 = 0.23761165606884235, out0 302 = 0.7790602368890209, out0 303 = 0.10430198277967856, out1 300 = 0.3819502948006305, out1 301 = 0.3724952762345579, out1 302 = 0.5767518104887833, out1 303 = 0.34673351982798417, out2 300 = 0.48380707616960517, out2 301 = 0.3603894835539248, out2 302 = 0.6811539971110301, out2 303 = 0.5616054791065083
```

The same workload timed on a worker placed like the node's; the numbers
depend on the device:

```dart
import 'package:aud_audio_script/aud_audio_script.dart';

void main() {
  final bench = AudScriptBench.measure(AudScriptBench.syntheticStressScript);
  print(bench); // mean ... µs, p50 ... µs, p99 ... µs, max ... µs per tick of a block of 5333.3 µs ...
}
```

A modulator that drives the filter of a sampler on the engine of
`aud_audio` (`aud_audio_ffi.dart` on native platforms), as the
[example](example) does with the synthetic workload:

```dart
import 'package:aud_audio/aud_audio_ffi.dart';
import 'package:aud_audio_script/aud_audio_script.dart';

const source = '''
// A cosine LFO over two beats on the control 300 of output 0.
aud.onTick = function () {
  var phase = (aud.time.beat % 2) / 2;
  aud.send(0, 300, 0.5 - 0.5 * Math.cos(2 * Math.PI * phase));
};
''';

void main() {
  final engine = AudEngine(
    packages: [
      AudNativeNodePackage(AudAudioScript.name, AudAudioScript.register),
    ],
  );
  engine.prepare();
  final graph = engine.graph;
  final modulator = graph.createNode(AudScriptModulator.typeId);
  // Output 0 steps at the grid point instead of ramping over the block.
  graph.setParam(modulator, AudScriptModulator.mode(0), AudScriptMode.step.value);
  graph.transaction((tx) {
    // Connect output 0 to the event input of a sampler here:
    // tx.connectEvents(modulator, sampler, fromPort: 0);
  });
  graph.setString(modulator, AudScriptModulator.script, source);
  graph
    ..transport(const AudTransportRequest.setTempo(120))
    ..transport(const AudTransportRequest.start());
  engine.start();
  print(AudScriptModulator.stats()); // ticks, tick times, late ticks, ...
}
```

## Limitations

- The package is optional and never a dependency of the engine or the
  umbrella; an app that does not run scripts ships without it.
- The graph of `aud_audio_graph` takes at most 1024 emitted events per
  block, for all nodes together (`max_events_per_block`). A modulator
  emits one event per changed value, two in step mode, so a script that
  changes more values per block than fit loses the rest of them; the
  graph drops and counts them.
- A context keeps at most 1024 (output, id) pairs; a send to one more
  throws a RangeError.
- The memory of a QuickJS-ng context is the count of its allocator: the
  bytes of the system's allocator it holds, by their usable size.
- QuickJS-ng 0.17.0 does not recover from running out of memory inside
  `JS_NewContext` or while it parses. The node sets the memory limit only
  after the context exists and compiles a loaded source without the limit
  before it runs it with the limit; `eval` or `new Function` inside a
  script that used up its memory may still crash the process.
- After live blocks, the first `lookahead` offline blocks may still miss
  the ticks the worker skipped live (known, not fixed).

## Documentation

- The decisions behind the package in
  [aud_audio_pm](https://github.com/audaudio/aud_audio_pm/tree/main/doc/2026-Q4/concepts/decisions):
  interop-001 (Dart never on the audio thread; the node's own worker is
  the recorded exception), abi-001 (the C ABI), build-001 (third-party C
  compiled by the hook), license-001 (permissive sources only)
- The C API with the tick grid: [src/aud_audio_script.h](src/aud_audio_script.h)
- The JavaScript API: [src/aud_script_engine.hpp](src/aud_script_engine.hpp)
- QuickJS-ng, its license and the audit:
  [src/third_party/NOTICES.md](src/third_party/NOTICES.md)
- The example app and its measurement: [example](example)
- The guides in [doc/guides](doc/guides) and the blog in [doc/blog](doc/blog)

## How it works

### The script

A script loads into a context of its own, in the global scope and in
sloppy mode; `(0, eval)(code)` evaluates further sources there. It sees:

| API | What it does |
| --- | --- |
| `aud.outputs` | 16, the event outputs `out0` to `out15` |
| `aud.time` | `beat`, `tempo`, `playing`, `sampleRate`, `frames`, `tick`; set before every `aud.onTick()` |
| `aud.onTick = function () {}` | called once per block |
| `aud.onControlChange = function (channel, number, value) {}` | the MIDI control changes of the input, value 0 to 127 |
| `aud.onControl = function (id, value) {}` | the numeric controls of the input |
| `aud.send(output, id, value)` | sets the value of (output 0..15, id 0..511) for this tick; a finite number |
| `console.log(...args)` | the last 64 lines are in the statistics |

The input callbacks run before the tick, in arrival order, for the events
of the blocks before the tick's block. The node emits a value as an
`AUD_EVENT_CONTROL` only when it differs from the last value it emitted
for the pair; a new context starts with no values sent.

### The tick

For the block that starts at the beat `b0` with `N` frames at `sr` Hz and
`T` BPM, the grid point of the tick and its frame are

```text
beatsPerBlock = N * T / 60 / sr
fractions     = ceil(1 / beatsPerBlock)
d             = 1 / fractions
g             = floor((b0 + beatsPerBlock) / d) * d     // aud.time.beat
k             = floor((g - b0) * 60 / T * sr)           // 0 when k >= N
```

and `g = b0`, `k = 0` while the transport stands. The worker computes
`lookahead` blocks ahead, L (1 to 4, default 3): at the end of block n
the audio thread hands it the request for block n + L - its beat predicted
from the transport at the end of block n plus L - 1 blocks of the same
size and tempo - with the input events of block n, and posts the worker's
semaphore. The first block requests blocks 1 to L at once; block 0 has no
tick. The worker runs the requests in order, each once - scripts keep
state -, and puts the values into a ring of results in the order of their
blocks; block n + L emits them: per output in ramp mode (the default) one
event at the last frame, which a sampler turns into a line over the block;
in step mode the old value at k - 1 and the new one at k, with k found
in the transport of the block that emits the value, so a step lands on
the grid point also when the blocks vary in size. So the worker
has L - 1 blocks of slack: audio callbacks that come back to back after a
late one still find their values. The input events of block n reach the
tick of block n + L. Three blocks are the default because on a slow Android
device with the vendor's core control a worker on any core three blocks
ahead had no late tick in 10 minutes of dense playing.

When the result of a block is not there, the block counts as late; the
newest older result that arrived goes out instead - as a step at frame 0
or as a ramp - and without one nothing is emitted and the samplers hold.
Results of later blocks wait for their blocks. Only when more than L
requests wait - the worker fell behind - it runs the newest one and counts
the others as skipped. A change of the lookahead leaves no block without
its tick: a larger one requests the blocks in between at once, a smaller
one requests nothing until the requested blocks catch up; for the blocks
of the change the input events reach the first tick requested after them.
An offline block (`AUD_PROCESS_OFFLINE`) waits for its result, with the
RealtimeSanitizer off around that wait only, so offline renders are
deterministic, never late and equal to the simulation with the same
lookahead - from the first offline block of a graph on. A load builds a
new context on the worker and swaps it in before the first tick whose
request came after the load - the L blocks requested before it still tick
the old script; a failed load keeps the old context and ends the engine's
load with the error.

### The engine

QuickJS-ng 0.17.0 (MIT), compiled into the package's library from the
sources of `gg_quickjs`, runs the scripts on every platform: a runtime per
context with a memory limit (`memory_limit`, 32 MB) and an interrupt
handler that stops a tick after `tick_budget` (100 ms); an aborted tick
drops its values. It sits behind a thin engine interface inside the
package, which is not public.

The worker has a stack of 4 MB and is created on the control thread. On
Android and Linux it asks for the nice values -19, -16 and -10 in turn,
and the parameter `worker_cores` says where it runs: on any core (0, the
default) or bound to the fast cores, the cores above the slowest clock
(1). Any core is the default because of what binding did on a big.LITTLE
tablet: the audio thread wakes the worker, and the scheduler puts a woken
thread on the waker's core (wake-affine); with the idle big cores isolated
by the vendor's core control, the bound worker had no other fast core to
go to and waited there for the realtime audio thread - for several blocks
when callbacks came back to back, so ticks were late although a tick took
a thirtieth of a block. The worker places itself anew at its next wake-up
after a change. On Windows it binds to the performance cores of a hybrid
processor, by their efficiency class, and runs where Windows puts it on
others. On Apple platforms it runs at the quality of service class
user-interactive either way. The statistics tell where it runs, the
priority that held, the `worker_cores` it placed itself for and the core
it ran its last tick on.

### The Dart API

- `AudAudioScript`: `name` and `register` for the package list of an
  engine, `registerWith` for the host api of a graph.
- `AudScriptModulator`: the type id, the event ids, the string settings
  (`script`, `script_name`), the parameters (`mode0` to `mode15`,
  `memory_limit`, `tick_budget`, `lookahead`, `worker_cores` with
  `AudScriptWorkerCores`) with their descriptors,
  `stats()` and `resetStats()` of the process-wide
  `AudScriptStats` - ticks, late ticks, skipped requests, aborted ticks,
  tick times with p50 and p99, the placement of the worker, the last load
  and its memory, the last error and console lines; the engine never
  resets them.
- `AudScriptSimulation.simulate` runs a source on the host code of the
  worker without a graph and returns every `AudScriptTick` with its beat,
  its frame k, its time and its `AudScriptSend`s; with `lookahead` the
  control changes reach the ticks as in a modulator with that lookahead.
- `AudScriptBench.measure` times the ticks of a source on a worker placed
  like the node's, paced at the block duration or back to back, and
  returns an `AudScriptBenchResult`: mean, p50, p99 and maximum in µs, the
  creation of the context, the load and its memory.
  `AudScriptBench.syntheticScript` is a public workload of beat-locked
  breakpoint envelopes with eased segments and target functions on three
  outputs; `syntheticStressScript` computes it ten times per tick.
- `AudScriptMode` and `AudScriptWorkerCores` are the values of the
  parameters `mode<n>` and `worker_cores`; `AudScriptPlacement` tells where
  a worker runs; `AudScriptControlChange` is an input of a simulation and
  `AudScriptTickStatus` how one of its ticks ended. `AudScriptException`
  is thrown when an engine refuses the node type, when a simulation or a
  bench cannot run, or when its script does not load.
- The members that call the native library are recorded with
  `@RecordUse`: in a profile or release build the link hook keeps the
  library only when the app uses one of them.

## Status

Version 0.2.0 on ABI 0.4 of `aud_audio_core`. Before 1.0 - the API may
change between minor versions.

## Contributing

Work is done ticket by ticket with the `gg` workflow; see the
[Develop Guide](doc/guides/develop-guide.md) and the
[Review Guide](doc/guides/for-ai/ai-review-guide.md). Every commit passes
`dart analyze`, `dart format`, `dart test` with 100 % coverage of `lib/`,
the native tests and the notices check.

The native tests need Node.js and `clang++` - on Windows the "C++ Clang
compiler for Windows" of Visual Studio; their RealtimeSanitizer build needs
a clang that has it, e.g. LLVM from Homebrew on macOS
(`brew install llvm`), and is skipped without one.

```bash
dart test                       # Dart tests and native tests
node scripts/test-native.js     # the native tests alone: ASan+UBSan, RTSan, TSan
node scripts/check-notices.js   # the third-party notices
```

## License

MIT — see [LICENSE](LICENSE).
