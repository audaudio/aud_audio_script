# Changelog

## Unreleased

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
