# aud_audio_script

Skript-Modulation für die Audanika Audio Engine: Der Knoten
`aud.script.modulator` führt JavaScript auf QuickJS-ng in einem eigenen
Worker-Thread aus und sendet einmal pro Audioblock Controls auf seinen
Event-Ausgängen - zum Beispiel taktgebundene Breakpoint-LFOs, die Cutoff,
Resonanz, Lautstärke und Panorama von Samplern steuern. Das Paket ist
optional: Kein anderes Paket der Engine hängt davon ab, eine App, die
Skripte ausführt, fügt es hinzu.

Teil der Audanika Audio Engine; geplant in [aud_audio_pm](https://github.com/audaudio/aud_audio_pm).

## Ziele

- Modulation in JavaScript - Hüllkurven, LFOs, Arithmetik zwischen ihnen,
  Bedingungen auf Controllern - läuft unverändert in der Engine
- JavaScript nie auf dem Audio-Thread und nie in Dart: Ein Worker des
  Knotens führt das Skript aus; der Audio-Thread signalisiert nur eine
  Semaphore, was nie blockiert, und tauscht Anfragen und Werte über
  lock-freie Ringpuffer aus
- Ein Tick pro Audioblock im Takt des Callbacks, Werte auf einem Raster,
  das am Beat hängt, als Rampe oder als Sprung im Block
- Ein später Worker hält das Audio nie auf: Die Ausgänge halten ihren
  Wert, der späte Block wird gezählt; Offline-Renderings warten auf den
  Worker und sind deterministisch
- Ein Speicherlimit und ein Zeitbudget pro Tick stoppen ein
  davonlaufendes Skript; ein fehlgeschlagener Load behält das alte Skript
- Die Geschwindigkeit einer Engine auf jedem Gerät messbar: ein Bench auf
  einem Worker, der wie der des Knotens platziert ist, eine Simulation
  ohne Graph, Statistiken der laufenden Modulatoren
- Eine App ohne Skripte enthält kein QuickJS-ng (interop-001)

## Installation

```yaml
dependencies:
  aud_audio_script:
    git:
      url: git@github.com:audaudio/aud_audio_script.git
      tag_pattern: "{{version}}"
    version: ^0.1.0
```

Eine App hängt von `aud_audio` und den DSP-Paketen ab, die sie nutzt, und
registriert sie bei ihrer Engine. Der Build-Hook kompiliert QuickJS-ng
als C aus dem pub.dev-Paket `gg_quickjs` (über dessen `QuickjsNgBuild`)
und den Knoten als C++17 mit der Toolchain des Ziels gegen den ABI-Header
von `aud_audio_core` und linkt beide zu einer Bibliothek
pro Architektur (iOS, Android, macOS, Windows; Linux im Prinzip). Das
Paket braucht kein Flutter. Die nativen Tests brauchen
Node.js und `clang++` - unter Windows den „C++-Clang-Compiler für
Windows“ von Visual Studio; ihr RealtimeSanitizer-Build braucht einen clang,
der ihn hat, z. B. LLVM aus Homebrew auf macOS (`brew install llvm`), und
wird ohne ihn übersprungen.

```bash
dart test                       # Dart-Tests und native Tests
node scripts/test-native.js     # nur die nativen Tests: ASan+UBSan, RTSan, TSan
node scripts/check-notices.js   # die Lizenzhinweise
```

## Dokumentation

- Die Entscheidungen hinter dem Paket in
  [aud_audio_pm](https://github.com/audaudio/aud_audio_pm/tree/main/doc/2026-Q4/concepts/decisions):
  interop-001 (Dart nie auf dem Audio-Thread; der eigene Worker des
  Knotens ist die festgehaltene Ausnahme), abi-001 (die C-ABI), build-001
  (C von Dritten, vom Hook kompiliert), license-001 (nur permissive
  Quellen)
- Die C-API mit dem Tick-Raster: [`src/aud_audio_script.h`](src/aud_audio_script.h)
- Die JavaScript-API: [`src/aud_script_engine.hpp`](src/aud_script_engine.hpp)
- QuickJS-ng, seine Lizenz und die Prüfung:
  [`src/third_party/NOTICES.md`](src/third_party/NOTICES.md)
- Die Beispiel-App und ihre Messung: [`example`](example)
- Die Guides in [`doc/guides`](doc/guides)

## Code-Beispiele

Ein Modulator, der das Filter eines Samplers auf der Engine von
`aud_audio` steuert (`aud_audio_ffi.dart` auf nativen Plattformen):

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

Dasselbe Skript ohne Graph, Tick für Tick, und seine Geschwindigkeit auf
einem Worker:

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
  print(simulation.ticks.first); // tick 1 at beat 0.0212..., frame 254: ...

  final bench = AudScriptBench.measure(AudScriptBench.syntheticStressScript);
  print(bench); // mean ... µs, p50 ... µs, p99 ... µs, max ... µs per tick of a block of 5333.3 µs ...
}
```

## Funktionsweise

### Das Skript

Ein Skript lädt in einen eigenen Kontext, in den globalen Gültigkeitsbereich
und im Sloppy Mode; `(0, eval)(code)` wertet dort weitere Quellen aus. Es
sieht:

| API | Was sie tut |
| --- | --- |
| `aud.outputs` | 16, die Event-Ausgänge `out0` bis `out15` |
| `aud.time` | `beat`, `tempo`, `playing`, `sampleRate`, `frames`, `tick`; vor jedem `aud.onTick()` gesetzt |
| `aud.onTick = function () {}` | einmal pro Block aufgerufen |
| `aud.onControlChange = function (channel, number, value) {}` | die MIDI-Control-Changes des Eingangs, Wert 0 bis 127 |
| `aud.onControl = function (id, value) {}` | die numerischen Controls des Eingangs |
| `aud.send(output, id, value)` | setzt den Wert von (Ausgang 0..15, Id 0..511) für diesen Tick; eine endliche Zahl |
| `console.log(...args)` | die letzten 64 Zeilen stehen in den Statistiken |

Die Eingangs-Callbacks laufen vor dem Tick, in der Reihenfolge ihres
Eintreffens, für die Events der Blöcke vor dem Block des Ticks. Der Knoten
sendet einen Wert als `AUD_EVENT_CONTROL` nur, wenn er sich vom zuletzt
gesendeten Wert des Paars unterscheidet; ein neuer Kontext beginnt ohne
gesendete Werte.

### Der Tick

Für den Block, der beim Beat `b0` mit `N` Frames bei `sr` Hz und `T` BPM
beginnt, sind der Rasterpunkt des Ticks und sein Frame

```text
beatsPerBlock = N * T / 60 / sr
fractions     = ceil(1 / beatsPerBlock)
d             = 1 / fractions
g             = floor((b0 + beatsPerBlock) / d) * d     // aud.time.beat
k             = floor((g - b0) * 60 / T * sr)           // 0 bei k >= N
```

und `g = b0`, `k = 0`, solange der Transport steht. Der Worker rechnet
`lookahead` Blöcke voraus, L (1 bis 4, Standard 3): Am Ende von Block n
übergibt der Audio-Thread ihm die Anfrage für Block n + L - ihr Beat aus
dem Transport am Ende von Block n plus L - 1 Blöcke gleicher Größe und
gleichen Tempos vorhergesagt - mit den Eingangs-Events von Block n und
signalisiert die Semaphore des Workers. Der erste Block fragt die Blöcke 1
bis L auf einmal an; Block 0 hat keinen Tick. Der Worker führt die
Anfragen der Reihe nach aus, jede einmal - Skripte haben Zustand -, und
legt die Werte in einen Ring von Ergebnissen in der Reihenfolge ihrer
Blöcke; Block n + L sendet sie: pro Ausgang im Rampenmodus (Standard) ein
Event am letzten Frame, das ein Sampler zu einer Linie über den Block
macht; im Sprungmodus der alte Wert bei k - 1 und der neue bei k, mit k
aus dem Transport des Blocks bestimmt, der den Wert sendet, sodass ein
Sprung auch bei wechselnden Blockgrößen auf dem Rasterpunkt liegt. So hat
der Worker L - 1 Blöcke Spielraum: Audio-Callbacks, die nach einem späten
direkt hintereinander kommen, finden ihre Werte trotzdem. Die
Eingangs-Events von Block n erreichen den Tick von Block n + L. Drei
Blöcke sind der Standard, weil auf einem langsamen Android-Gerät mit dem
Core-Control des Herstellers ein Worker auf jedem Kern drei Blöcke voraus
in 10 Minuten dichten Spiels keinen späten Tick hatte.

Fehlt das Ergebnis eines Blocks, zählt der Block als spät; das neueste
ältere Ergebnis, das eingetroffen ist, geht stattdessen hinaus - als
Sprung bei Frame 0 oder als Rampe -, und ohne eines wird nichts gesendet,
und die Sampler halten ihre Werte. Ergebnisse späterer Blöcke warten auf
ihre Blöcke. Nur wenn mehr als L Anfragen warten - der Worker ist in
Rückstand -, führt er die neueste aus und zählt die anderen als
übersprungen. Eine Änderung des Lookaheads lässt keinen Block ohne seinen
Tick: Ein größerer fragt die Blöcke dazwischen auf einmal an, ein
kleinerer fragt nichts an, bis die angefragten Blöcke aufgeholt sind; für
die Blöcke der Änderung erreichen die Eingangs-Events den ersten danach
angefragten Tick. Ein Offline-Block (`AUD_PROCESS_OFFLINE`) wartet auf
sein Ergebnis, mit dem RealtimeSanitizer nur um dieses Warten herum aus,
sodass Offline-Renderings deterministisch, nie zu spät und gleich der
Simulation mit demselben Lookahead sind - ab dem ersten Offline-Block
eines Graphen: Nach Live-Blöcken können die ersten `lookahead`
Offline-Blöcke noch die Ticks verpassen, die der Worker live übersprungen
hat (bekannt, nicht behoben). Ein Load baut einen neuen Kontext auf dem
Worker und tauscht ihn vor dem ersten Tick ein, dessen Anfrage nach dem
Load kam - die L vorher angefragten Blöcke ticken noch das alte Skript;
ein fehlgeschlagener Load behält den alten Kontext und beendet den Load
der Engine mit dem Fehler.

### Die Engine

QuickJS-ng 0.17.0 (MIT), aus den Quellen von `gg_quickjs` in die
Bibliothek des Pakets kompiliert, führt die Skripte auf jeder Plattform
aus: eine Runtime pro Kontext mit einem Speicherlimit
(`memory_limit`, 32 MB) und einem Interrupt-Handler, der einen Tick nach
`tick_budget` (100 ms) stoppt; ein abgebrochener Tick verwirft seine
Werte. Es sitzt hinter einer dünnen Engine-Schnittstelle im Paket, die
nicht öffentlich ist.

Der Worker hat einen Stack von 4 MB und wird auf dem Control-Thread
erzeugt. Auf Android und Linux fragt er nacheinander nach den Nice-Werten
-19, -16 und -10, und der Parameter `worker_cores` sagt, wo er läuft: auf
jedem Kern (0, Standard) oder gebunden an die schnellen Kerne, die Kerne
über dem langsamsten Takt (1). Jeder Kern ist der Standard wegen dessen,
was das Binden auf einem big.LITTLE-Tablet bewirkte: Der Audio-Thread
weckt den Worker, und der Scheduler legt einen geweckten Thread auf den
Kern des Weckenden (wake-affine); da der Core-Control des Herstellers die
ruhenden großen Kerne isoliert, hatte der gebundene Worker keinen anderen
schnellen Kern und wartete dort auf den Echtzeit-Audio-Thread - bei
direkt aufeinanderfolgenden Callbacks mehrere Blöcke lang, sodass Ticks zu
spät kamen, obwohl ein Tick ein Dreißigstel eines Blocks brauchte. Nach
einer Änderung platziert sich der Worker beim nächsten Aufwachen neu.
Unter Windows bindet er sich auf einem Hybridprozessor an die
Performance-Kerne, erkannt an ihrer Effizienzklasse, und läuft auf
anderen dort, wo Windows ihn hinsetzt. Auf Apple-Plattformen läuft er in jedem Fall in der Quality-of-Service-Klasse
user-interactive. Die Statistiken sagen, wo er läuft, welche Priorität
gilt, für welches `worker_cores` er sich platziert hat und auf welchem
Kern er seinen letzten Tick rechnete.

### Grenzen

- Der Graph von `aud_audio_graph` nimmt pro Block höchstens 1024
  gesendete Events an, für alle Knoten zusammen (`max_events_per_block`).
  Ein Modulator sendet ein Event pro geändertem Wert, im Sprungmodus zwei;
  ein Skript, das pro Block mehr Werte ändert, als hineinpassen, verliert
  den Rest; der Graph verwirft und zählt sie.
- Ein Kontext hält höchstens 1024 Paare (Ausgang, Id); ein Send an ein
  weiteres wirft einen RangeError.
- Der Speicher eines QuickJS-ng-Kontexts ist der Zähler seines
  Allokators: die Bytes des Systemallokators, die er hält, nach ihrer
  nutzbaren Größe.
- QuickJS-ng 0.17.0 erholt sich nicht davon, wenn ihm in `JS_NewContext`
  oder beim Parsen der Speicher ausgeht. Der Knoten setzt das
  Speicherlimit erst, wenn der Kontext existiert, und kompiliert eine
  geladene Quelle ohne das Limit, bevor er sie mit dem Limit ausführt;
  `eval` oder `new Function` in einem Skript, das seinen Speicher
  aufgebraucht hat, kann den Prozess trotzdem abstürzen lassen.

### Die Dart-API

- `AudAudioScript`: `name` und `register` für die Paketliste einer Engine,
  `registerWith` für die Host-API eines Graphen.
- `AudScriptModulator`: die Typ-Id, die Event-Ids, die String-Einstellungen
  (`script`, `script_name`), die Parameter (`mode0` bis `mode15`,
  `memory_limit`, `tick_budget`, `lookahead`, `worker_cores` mit
  `AudScriptWorkerCores`) mit ihren Deskriptoren,
  `stats()` und `resetStats()` der prozessweiten
  `AudScriptStats` - Ticks, späte Ticks, übersprungene Anfragen,
  abgebrochene Ticks, Tick-Zeiten mit p50 und p99, die Platzierung des
  Workers, der letzte Load und sein Speicher, der letzte Fehler und die
  Konsolenzeilen; die Engine setzt sie nie zurück.
- `AudScriptSimulation.simulate` führt eine Quelle auf dem Host-Code des
  Workers ohne Graph aus und liefert jeden `AudScriptTick` mit seinem Beat,
  seinem Frame k, seiner Zeit und seinen `AudScriptSend`s; mit `lookahead`
  erreichen die Control-Changes die Ticks wie in einem Modulator mit
  diesem Lookahead.
- `AudScriptBench.measure` misst die Ticks einer Quelle auf einem Worker,
  der wie der des Knotens platziert ist, im Takt der Blockdauer oder direkt
  hintereinander, und liefert ein `AudScriptBenchResult`: Mittelwert, p50,
  p99 und Maximum in µs, das Erzeugen des Kontexts, den Load und seinen
  Speicher. `AudScriptBench.syntheticScript` ist eine öffentliche Last aus
  taktgebundenen Breakpoint-Hüllkurven mit weichen Segmenten und
  Zielfunktionen auf drei Ausgängen; `syntheticStressScript` berechnet sie
  zehnmal pro Tick.
- `AudScriptMode` und `AudScriptWorkerCores` sind die Werte der Parameter
  `mode<n>` und `worker_cores`; `AudScriptPlacement` sagt, wo ein Worker
  läuft; `AudScriptControlChange` ist eine Eingabe einer Simulation und
  `AudScriptTickStatus` sagt, wie einer ihrer Ticks endete.
  `AudScriptException` wird geworfen, wenn eine Engine den Knotentyp
  ablehnt, wenn eine Simulation oder ein Bench nicht laufen kann oder wenn
  ihr Skript nicht lädt.
- Die Member, die die native Bibliothek aufrufen, sind mit `@RecordUse`
  markiert: In einem Profile- oder Release-Build behält der Link-Hook die
  Bibliothek nur, wenn die App einen davon nutzt.

## Mitwirken

Gearbeitet wird Ticket für Ticket mit dem `gg`-Workflow; siehe den
[Develop Guide](doc/guides/develop-guide.md) und den
[Review Guide](doc/guides/for-ai/ai-review-guide.md). Jeder Commit
besteht `dart analyze`, `dart format`, `dart test` mit 100 % Abdeckung von
`lib/`, die nativen Tests und die Prüfung der Lizenzhinweise.
