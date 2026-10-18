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
