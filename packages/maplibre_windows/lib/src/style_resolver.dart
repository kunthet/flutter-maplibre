import 'dart:convert';
import 'dart:io';

/// Prepares a MapLibre style JSON string for the Windows native embedder.
///
/// The headless renderer crashes when [Style::loadJSON] receives vector sources
/// that use a TileJSON `url` reference instead of an inline `tiles` array.
/// This fetches the style (if needed), resolves those sources, and drops invalid
/// attribution-only vector stubs (e.g. MapTiler `maptiler_attribution`).
Future<String> resolveStylePayloadForNative(String style) async {
  if (style.isEmpty) return style;

  final trimmed = style.trimLeft();
  if (trimmed.startsWith('{') || trimmed.startsWith('[')) {
    return resolveStyleJsonForNative(trimmed);
  }

  final uri = Uri.tryParse(style);
  if (uri == null || !uri.hasScheme) {
    return style;
  }

  if (uri.scheme == 'file') {
    final path = uri.toFilePath(windows: Platform.isWindows);
    final jsonText = await File(path).readAsString();
    return resolveStyleJsonForNative(jsonText);
  }

  final client = HttpClient();
  try {
    final jsonText = await _fetchText(client, uri);
    return resolveStyleJsonForNative(jsonText);
  } finally {
    client.close();
  }
}

/// Resolves url-based vector sources inside an already-fetched style JSON body.
Future<String> resolveStyleJsonForNative(String jsonText) async {
  final decoded = jsonDecode(jsonText);
  if (decoded is! Map<String, dynamic>) {
    return jsonText;
  }

  final sources = decoded['sources'];
  if (sources is! Map<String, dynamic>) {
    return jsonText;
  }

  final client = HttpClient();
  try {
    final resolvedSources = <String, dynamic>{};
    for (final entry in sources.entries) {
      final id = entry.key;
      final value = entry.value;
      if (value is! Map<String, dynamic>) {
        resolvedSources[id] = value;
        continue;
      }

      final type = value['type'];
      final url = value['url'];
      final tiles = value['tiles'];

      if (type == 'vector' && url is String && tiles == null) {
        final tileJson = await _fetchJson(client, Uri.parse(url));
        final inline = <String, dynamic>{
          'type': 'vector',
          'tiles': tileJson['tiles'],
        };
        final minzoom = tileJson['minzoom'];
        final maxzoom = tileJson['maxzoom'];
        if (minzoom is num) inline['minzoom'] = minzoom;
        if (maxzoom is num) inline['maxzoom'] = maxzoom;
        resolvedSources[id] = inline;
        continue;
      }

      // MapTiler styles ship a vector stub used only for HTML attribution.
      if (type == 'vector' && tiles == null && url == null) {
        continue;
      }

      resolvedSources[id] = value;
    }

    decoded['sources'] = resolvedSources;
    return jsonEncode(decoded);
  } finally {
    client.close();
  }
}

Future<String> _fetchText(HttpClient client, Uri uri) async {
  final request = await client.getUrl(uri);
  final response = await request.close();
  if (response.statusCode != HttpStatus.ok) {
    throw StateError('Failed to fetch map style: HTTP ${response.statusCode} for $uri');
  }
  return response.transform(utf8.decoder).join();
}

Future<Map<String, dynamic>> _fetchJson(HttpClient client, Uri uri) async {
  final body = await _fetchText(client, uri);
  final decoded = jsonDecode(body);
  if (decoded is! Map<String, dynamic>) {
    throw StateError('Expected JSON object from $uri');
  }
  return decoded;
}
