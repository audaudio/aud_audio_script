# Third-party notices of aud_audio_script

`aud_audio_script` is published under the MIT license with Audanika as the
copyright holder (decision license-002); see `LICENSE` in the root of the
package. It vendors one third-party component, compiled by the package's
build hook (decision build-001). It was vendored on 2026-10-18 for ticket
31 of aud_audio_pm:

- `quickjs-ng` holds the library sources of QuickJS-ng, a JavaScript
  engine written in C, from https://github.com/quickjs-ng/quickjs, tag
  `v0.17.0`, commit `6d46d07d04041b40f4f49eaa7fdebe44c314c699`
  (2026-09-18, "Set version to 0.17.0"), with its `LICENSE`: `quickjs.c`,
  `quickjs.h`, `quickjs-atom.h`, `quickjs-opcode.h`,
  `quickjs-c-atomics.h`, `cutils.h`, `list.h`, `libregexp.c`,
  `libregexp.h`, `libregexp-opcode.h`, `libunicode.c`, `libunicode.h`,
  `libunicode-table.h`, `dtoa.c`, `dtoa.h`, `builtin-array-fromasync.h`,
  `builtin-iterator-zip-keyed.h` and `builtin-iterator-zip.h`. The
  standard library (`quickjs-libc`), the command line tools (`qjs`,
  `qjsc`), the tests and the build files are not vendored: the package
  needs the engine only.

The vendored files are unchanged and keep their original headers.

| Component | Path | License |
| --- | --- | --- |
| QuickJS-ng | `quickjs-ng` | MIT, Copyright (c) 2017-2026 Fabrice Bellard, Copyright (c) 2017-2024 Charlie Gordon, Copyright (c) 2023-2026 Ben Noordhuis, Copyright (c) 2023-2026 Saúl Ibarra Corretgé, `quickjs-ng/LICENSE` |

## The license audit of the vendored sources

Done for ticket 31 (decisions license-001 and license-002):

- `quickjs-ng/*`: QuickJS by Fabrice Bellard and Charlie Gordon, continued
  as QuickJS-ng; every vendored file is under the MIT license of the
  repository, the C files carry the MIT notice in their headers. No file
  of the vendored set is under another license: `libunicode-table.h` is
  generated from the Unicode Character Database by the project's own
  generator, whose output the Unicode license allows to be used without
  restriction; `dtoa.c` is QuickJS's own implementation by Fabrice
  Bellard (MIT).
- The node, the worker, the engine interface, the JavaScriptCore adapter,
  the simulation, the bench and the synthetic workload are Audanika's own
  code. MIT, as the package. The JavaScriptCore adapter uses the public C
  API of the system's JavaScriptCore framework on Apple platforms; nothing
  of JavaScriptCore is vendored or linked statically.
- No code under the LGPL or the GPL is part of the package.
