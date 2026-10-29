# Changelog

## Unreleased

### Changed

- Use com.example IDs in the example

## 0.3.0 - 2026-10-28

### Changed

- Require Dart 3.13 and upgrade the dependencies

- Require Dart 3.13

- Upgrade dna_audanika to ^0.1.0

- Upgrade helix to ^1.14.0

- Upgrade the git dependencies to their current versions

- Set the identity the DNA layers ask for in dna/_vars.json

- Reformat 2 files with the Dart 3.13 formatter

- Add the blog post of this change, in English and German

- Bring the README to the README template, in English and German

- Upgrade the dependencies published since the first pass

- Lift the lock file to the newest resolvable versions


## 0.2.0 - 2026-10-24

### Changed

- Place the script worker on Windows
- Document the Windows support
- Document the Windows behavior in both languages
- Place the script worker on Windows

## 0.1.1 - 2026-10-22

### Added

- Add the example to a pub workspace of the package
- The example is a member of the workspace of pubspec.yaml, so that upgrading the package upgrades the example too; its own lock goes
- The workspace overrides aud_audio_core from git, ^0.5.3: the engine of aud_audio the example plays on pins core exactly
- test ^1.31.0, within the test_api that flutter_test of the example pins
- Add aud_audio_graph to the overrides of the workspace
- The engine of aud_audio the example plays on pins aud_audio_graph exactly too; the workspace takes ^0.5.4, which the package needs

## 0.1.0 - 2026-10-19

### Added

- Initial boilerplate

- Add the script modulator node on QuickJS-ng

- Add aud.script.modulator: a worker per node runs JavaScript once per audio block, 16 event outputs, ramp or step per output, value-change-only controls

- Add the worker with a non-blocking semaphore signal, lock-free rings, late and skipped counting, offline waits and loads that swap at a tick

- Add QuickJS-ng 0.17.0 (MIT) with a memory limit and a tick budget, and JavaScriptCore through its public C API on Apple platforms

- Add the worker placement: fast cores and nice -19/-16/-10 on Android, user-interactive QoS on Apple platforms

- Add process-wide stats, a simulation without a graph, a paced bench and a public synthetic workload

- Add the Dart API: AudAudioScript, AudScriptModulator, AudScriptStats, AudScriptSimulation, AudScriptBench

- Add native tests under ASan/UBSan, RTSan and TSan, Dart tests, the link hook and a minimal example

- Count the bytes of a QuickJS-ng runtime in its allocator instead of walking its heap

- Add a lookahead of the worker of the modulator

- Add the parameter lookahead (1 to 4 blocks, default 2): block n requests block n + lookahead, so back-to-back callbacks find their values

- Run pending requests in order, each once; skip to the newest only when more than lookahead requests wait

- Keep the results in a FIFO: a block takes its own, a late block the newest older one, later ones wait

- Request blocks 1 to lookahead at the first block; a change of the lookahead leaves no block without its tick

- Add the lookahead to the simulation and to the statistics

- Test back-to-back callbacks (late at 1, on time at 2) and the equality with the simulation at lookahead 1 and 2

- Add QuickJS-ng as the sole engine, drop JavaScriptCore

- Remove the JavaScriptCore adapter and its framework link

- Remove the engine choice: the setting engine, aud_audio_script_engine_available, AUD_SCRIPT_ENGINE_*, the engine fields of the stats, the simulation and the bench

- Remove AudScriptEngine, AudScriptModulator.engine and engineAvailable, the engine of AudScriptStats, AudScriptBenchResult, simulate and measure

- Keep the internal engine interface with QuickJS-ng as its only implementation

- Drop the JavaScriptCore tests and caveats from the tests and the docs

- Add worker_cores: run the worker on any core by default

- Add the parameter worker_cores: 0 any core with the raised priority (default), 1 bound to the fast cores; Apple platforms keep the QoS class

- Place the worker anew at its next wake-up after a change, never on the audio thread

- Report the placement anyCore, the worker_cores it holds and the core of the last tick (lastCpu) in the stats and the bench

- Add AudScriptWorkerCores, AudScriptModulator.workerCores, AudScriptPlacement.anyCore and the bench option workerCores

- Explain the default in the README: wake-affine wake-ups and isolated big cores starved the bound worker

- Add a lookahead of three blocks as the default

- Raise the default of the parameter lookahead from 2 to 3 blocks

- Explain it in the README: on a slow Android device with core control, any core and three blocks ahead gave no late tick in 10 minutes

- Adapt the tests that rely on the default

- Add steps on the real grid point and room for requests

- Find the frame of a step's grid point in the transport of the block that emits it, so steps land on the grid point when blocks vary in size

- Keep room in the request ring for this block's request and the next block's lookahead when input events flood it

- Let the counting allocator free a block on realloc to 0 bytes

- Document that offline renders are deterministic from the first offline block on

- Test blocks of 180, 192 and 204 frames at lookahead 3, an event flood with a lookahead increase and the allocator

- Add the review fixes of the script modulator

- Keep one undo entry per pair a tick touches, so that a loop of sends no longer grows the undo log without bound

- Keep the sends of a load only for a runner with a send log, the simulation's

- Cut a console line at 4096 bytes on a whole UTF-8 character, also inside a single argument

- Share the defaults and limits and the load, budget and timing helpers of the node, the simulation and the bench

- Correct the comments on where the worker runs, on the lookahead of a simulated control change and on the bench output in the READMEs

- List the remaining public types in the Dart API of both READMEs

- Test the console cut, the sends of a load and a second send to a pair in an aborted tick

- Add QuickJS-ng from gg_quickjs instead of vendored sources

- Add gg_quickjs ^0.1.0 and build QuickJS-ng with its QuickjsNgBuild in the build hook; remove src/third_party/quickjs-ng

- Take the QuickJS-ng sources of the native tests from the gg_quickjs package

- Set the memory limit after the context exists and compile sources without it, against two out-of-memory bugs of QuickJS-ng 0.17.0

- Keep the MIT notice of QuickJS-ng in NOTICES.md; document the dependency and the remaining risk
