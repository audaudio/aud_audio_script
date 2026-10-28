// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:io';
import 'dart:isolate';

import 'package:record_use/record_use.dart';
import 'package:test/test.dart';

import '../../hook/link.dart';

void main() {
  group('hook/link.dart', () {
    test('keeps the library without recordings', () {
      expect(usesScript(null), isTrue);
    });

    test('drops the library of an app that calls no member', () {
      expect(usesScript(Recordings(calls: {}, instances: {})), isFalse);
      final other = Method(
        'speak',
        Class('Other', Library('package:other/other.dart')),
      );
      expect(
        usesScript(
          Recordings(
            calls: {
              other: [_call],
            },
            instances: {},
          ),
        ),
        isFalse,
      );
      // Members the recordings name without a call.
      expect(
        usesScript(
          Recordings(
            calls: {for (final member in scriptMembers) member: []},
            instances: {},
          ),
        ),
        isFalse,
      );
    });

    for (final member in scriptMembers) {
      test('keeps the library of an app that calls ${member.name}', () {
        expect(
          usesScript(
            Recordings(
              calls: {
                member: [_call],
              },
              instances: {},
            ),
          ),
          isTrue,
        );
      });
    }

    test('names every member that the package records', () async {
      final recorded = <DefinitionWithStaticCalls>[];
      for (final (library, className) in [
        (registrationLibrary, 'AudAudioScript'),
        (modulatorLibrary, 'AudScriptModulator'),
        (simulationLibrary, 'AudScriptSimulation'),
        (benchLibrary, 'AudScriptBench'),
      ]) {
        final file = await Isolate.resolvePackageUri(Uri.parse(library));
        final source = await File.fromUri(file!).readAsString();
        final body = RegExp('class $className \\{([\\s\\S]*?)\\n\\}')
            .firstMatch(source)!
            .group(1)!;
        final owner = Class(className, Library(library));
        // The declaration after each @RecordUse() up to its parameters or
        // its body, e.g. `static void registerWith`.
        for (final match in RegExp(
          r'@RecordUse\(\)\s+([^(={;]+)',
        ).allMatches(body)) {
          final words = match.group(1)!.trim().split(RegExp(r'\s+'));
          final isInstanceMember = !words.contains('static');
          recorded.add(switch (words) {
            _ when words.contains('get') => Getter(
              words.last,
              owner,
              isInstanceMember: isInstanceMember,
            ),
            _ when words.contains('set') => Setter(
              words.last,
              owner,
              isInstanceMember: isInstanceMember,
            ),
            _ => Method(words.last, owner, isInstanceMember: isInstanceMember),
          });
        }
      }
      expect(recorded, unorderedEquals(scriptMembers));
    });
  });
}

const _call = CallWithArguments(
  positionalArguments: [],
  namedArguments: {},
  loadingUnit: LoadingUnit('main'),
);
