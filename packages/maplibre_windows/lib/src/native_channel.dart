import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter/services.dart';

/// Method/event channel bridge to the native maplibre_windows C++ plugin.
final class NativeMapChannel {
  NativeMapChannel._();

  static const _method = MethodChannel('maplibre_windows');
  static const _events = EventChannel('maplibre_windows/events');

  static Future<int> createMap({
    required int width,
    required int height,
    required double pixelRatio,
    String initStyle = '',
    double? initZoom,
    double? initLat,
    double? initLon,
    double? initBearing,
    double? initPitch,
  }) async {
    final textureId = await _method.invokeMethod<int>('createMap', {
      'width': width,
      'height': height,
      'pixelRatio': pixelRatio,
      'initStyle': initStyle,
      if (initZoom != null) 'initZoom': initZoom,
      if (initLat != null) 'initLat': initLat,
      if (initLon != null) 'initLon': initLon,
      if (initBearing != null) 'initBearing': initBearing,
      if (initPitch != null) 'initPitch': initPitch,
    });
    if (textureId == null) {
      throw PlatformException(code: 'create_failed', message: 'createMap returned null texture id');
    }
    return textureId;
  }

  static Future<void> disposeMap(int textureId) =>
      _method.invokeMethod<void>('disposeMap', {'textureId': textureId});

  static Future<void> resizeMap(int textureId, int width, int height) =>
      _method.invokeMethod<void>('resizeMap', {
        'textureId': textureId,
        'width': width,
        'height': height,
      });

  static Future<void> setStyle(int textureId, String style) =>
      _method.invokeMethod<void>('setStyle', {'textureId': textureId, 'style': style});

  static Future<void> moveCamera(
    int textureId, {
    double? lat,
    double? lon,
    double? zoom,
    double? bearing,
    double? pitch,
  }) =>
      _method.invokeMethod<void>('moveCamera', {
        'textureId': textureId,
        if (lat != null) 'lat': lat,
        if (lon != null) 'lon': lon,
        if (zoom != null) 'zoom': zoom,
        if (bearing != null) 'bearing': bearing,
        if (pitch != null) 'pitch': pitch,
      });

  static Future<void> animateCamera(
    int textureId, {
    double? lat,
    double? lon,
    double? zoom,
    double? bearing,
    double? pitch,
    int durationMs = 2000,
  }) =>
      _method.invokeMethod<void>('animateCamera', {
        'textureId': textureId,
        if (lat != null) 'lat': lat,
        if (lon != null) 'lon': lon,
        if (zoom != null) 'zoom': zoom,
        if (bearing != null) 'bearing': bearing,
        if (pitch != null) 'pitch': pitch,
        'durationMs': durationMs,
      });

  static Future<Map<String, dynamic>> getCamera(int textureId) async {
    final result = await _method.invokeMethod<Map<Object?, Object?>>(
      'getCamera',
      {'textureId': textureId},
    );
    return Map<String, dynamic>.from(result ?? const {});
  }

  static Future<Map<String, dynamic>> getVisibleRegion(int textureId) async {
    final result = await _method.invokeMethod<Map<Object?, Object?>>(
      'getVisibleRegion',
      {'textureId': textureId},
    );
    return Map<String, dynamic>.from(result ?? const {});
  }

  static Future<List<double>> toLngLat(int textureId, double x, double y) async {
    final result = await _method.invokeListMethod<double>(
      'toLngLat',
      {'textureId': textureId, 'x': x, 'y': y},
    );
    return result ?? [0, 0];
  }

  static Future<List<double>> toScreenLocation(int textureId, double lon, double lat) async {
    final result = await _method.invokeListMethod<double>(
      'toScreenLocation',
      {'textureId': textureId, 'lon': lon, 'lat': lat},
    );
    return result ?? [0, 0];
  }

  static Future<void> setDragPanEnabled(int textureId, bool enabled) =>
      _method.invokeMethod<void>('setDragPanEnabled', {
        'textureId': textureId,
        'enabled': enabled,
      });

  static Future<void> setInvertWheelZoom(int textureId, bool invert) =>
      _method.invokeMethod<void>('setInvertWheelZoom', {
        'textureId': textureId,
        'invert': invert,
      });

  static Future<void> addSource(int textureId, String id, String sourceJson) =>
      _method.invokeMethod<void>('addSource', {
        'textureId': textureId,
        'id': id,
        'sourceJson': sourceJson,
      });

  static Future<void> addLayer(
    int textureId,
    String layerJson, {
    String? belowLayerId,
  }) =>
      _method.invokeMethod<void>('addLayer', {
        'textureId': textureId,
        'layerJson': layerJson,
        if (belowLayerId != null) 'belowLayerId': belowLayerId,
      });

  static Future<void> removeLayer(int textureId, String id) =>
      _method.invokeMethod<void>('removeLayer', {'textureId': textureId, 'id': id});

  static Future<void> removeSource(int textureId, String id) =>
      _method.invokeMethod<void>('removeSource', {'textureId': textureId, 'id': id});

  static Future<void> updateGeoJsonSource(int textureId, String id, String data) =>
      _method.invokeMethod<void>('updateGeoJsonSource', {
        'textureId': textureId,
        'id': id,
        'data': data,
      });

  static Future<void> updateLayerFilter(int textureId, String id, String? filterJson) =>
      _method.invokeMethod<void>('updateLayerFilter', {
        'textureId': textureId,
        'id': id,
        'filter': filterJson,
      });

  static Future<void> updateVectorSourceTiles(
    int textureId,
    String id,
    List<String> tiles,
  ) =>
      _method.invokeMethod<void>('updateVectorSourceTiles', {
        'textureId': textureId,
        'id': id,
        'tiles': tiles,
      });

  static Future<void> addImage(int textureId, String id, Uint8List bytes) =>
      _method.invokeMethod<void>('addImage', {
        'textureId': textureId,
        'id': id,
        'bytes': bytes,
      });

  static Future<void> removeImage(int textureId, String id) =>
      _method.invokeMethod<void>('removeImage', {'textureId': textureId, 'id': id});

  static Future<List<String>> getLayerIds(int textureId) async {
    final result = await _method.invokeListMethod<Object?>(
      'getLayerIds',
      {'textureId': textureId},
    );
    return result?.cast<String>() ?? const [];
  }

  static Future<List<String>> getAttributions(int textureId) async {
    final result = await _method.invokeListMethod<Object?>(
      'getAttributions',
      {'textureId': textureId},
    );
    return result?.cast<String>() ?? const [];
  }

  static Future<List<Map<String, dynamic>>> featuresAtPoint(
    int textureId,
    double x,
    double y, {
    List<String>? layerIds,
  }) async {
    final result = await _method.invokeMethod<String>(
      'featuresAtPoint',
      {
        'textureId': textureId,
        'x': x,
        'y': y,
        if (layerIds != null) 'layerIds': layerIds,
      },
    );
    if (result == null || result.isEmpty) return const [];
    final decoded = jsonDecode(result);
    if (decoded is! List) return const [];
    return decoded
        .map((e) => Map<String, dynamic>.from(e as Map))
        .toList(growable: false);
  }

  static Future<void> onPointer(
    int textureId, {
    required String phase,
    required double x,
    required double y,
    double scrollDelta = 0,
    bool shift = false,
    bool control = false,
  }) =>
      _method.invokeMethod<void>('onPointer', {
        'textureId': textureId,
        'phase': phase,
        'x': x,
        'y': y,
        'scrollDelta': scrollDelta,
        'shift': shift,
        'control': control,
      });

  static Stream<Map<String, dynamic>> eventStream(int textureId) {
    return _events.receiveBroadcastStream(textureId).map(
      (event) => Map<String, dynamic>.from(event! as Map<Object?, Object?>),
    );
  }
}
