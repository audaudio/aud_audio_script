# Third-party notices of aud_audio_script

`aud_audio_script` is published under the MIT license with Audanika as the
copyright holder (decision license-002); see `LICENSE` in the root of the
package. It vendors no third-party sources. Its native library contains
one third-party component, compiled by the package's build hook (decision
build-001):

- QuickJS-ng 0.17.0, a JavaScript engine written in C, from
  https://github.com/quickjs-ng/quickjs. The package takes its sources
  from the pub.dev package `gg_quickjs` 0.1.0 (MIT,
  https://github.com/ggsuite/gg_quickjs), which carries the library
  subset of QuickJS-ng unchanged in `src/quickjs-ng` with its `LICENSE`
  and builds it as a static library through `package:gg_quickjs/hook.dart`;
  the build hook of this package links that library into its own. The
  standard library (`quickjs-libc`), the command line tools and the tests
  of QuickJS-ng are not part of it. An app that ships this package ships
  the notice below; `gg_quickjs` ships it in its `NOTICE` too.

| Component | Path | License |
| --- | --- | --- |
| QuickJS-ng | built from `gg_quickjs` (pub.dev), `src/quickjs-ng` | MIT, Copyright (c) 2017-2026 Fabrice Bellard, Copyright (c) 2017-2024 Charlie Gordon, Copyright (c) 2023-2026 Ben Noordhuis, Copyright (c) 2023-2026 Saúl Ibarra Corretgé |

## The license of QuickJS-ng

```text
The MIT License (MIT)

Copyright (c) 2017-2026 Fabrice Bellard
Copyright (c) 2017-2024 Charlie Gordon
Copyright (c) 2023-2026 Ben Noordhuis
Copyright (c) 2023-2026 Saúl Ibarra Corretgé

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

## The license audit

Done for ticket 31 (decisions license-001 and license-002):

- QuickJS-ng: QuickJS by Fabrice Bellard and Charlie Gordon, continued as
  QuickJS-ng; every file of the subset is under the MIT license of the
  repository, the C files carry the MIT notice in their headers.
  `libunicode-table.h` is generated from the Unicode Character Database by
  the project's own generator, whose output the Unicode license allows to
  be used without restriction; `dtoa.c` is QuickJS's own implementation by
  Fabrice Bellard (MIT). `gg_quickjs` is MIT as well; the package uses its
  hook helper and its copy of the sources, no other code of it.
- The node, the worker, the engine interface with its QuickJS-ng adapter,
  the simulation, the bench and the synthetic workload are Audanika's own
  code. MIT, as the package.
- No code under the LGPL or the GPL is part of the package.
