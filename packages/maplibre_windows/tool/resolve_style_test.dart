import 'dart:io';

import 'package:maplibre_windows/src/style_resolver.dart';

Future<void> main(List<String> args) async {
  final url = args.isNotEmpty
      ? args.first
      : 'https://api.maptiler.com/maps/dataviz-light/style.json?key=2EmAnQFfnpeTaK8mQRZT';
  final resolved = await resolveStylePayloadForNative(url);
  final out = File(
    args.length > 1
        ? args[1]
        : r'd:\Dev\mapping\myuramap\myuramap\build\style_tests\dart_resolved_maptiler.json',
  );
  await out.parent.create(recursive: true);
  await out.writeAsString(resolved);
  stdout.writeln('resolved ${resolved.length} bytes -> ${out.path}');
}
