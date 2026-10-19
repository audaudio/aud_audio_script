# aud_audio_script_example

A modulator of `aud_audio_script` runs the synthetic workload of the
package on the engine of `aud_audio` while the transport plays at
120 BPM, on iOS and Android:

- the statistics of the modulator every second: ticks, tick times, late
  ticks, skipped requests, aborted ticks, the placement of the worker
- the bench of the synthetic workload on QuickJS-ng

```bash
flutter run -d <device>
flutter test integration_test/script_test.dart -d <device> \
  --dart-define=AUD_MEASURE_SECONDS=10
```

`script_test.dart` prints `SCRIPT_BENCH` - the synthetic workload and its
tenfold stress variant on the bench, paced at the block of the device and
at 48 kHz, 256 frames - and `SCRIPT_LIVE`:
the statistics of the modulator after `AUD_MEASURE_SECONDS` (default 10)
on the running engine.
