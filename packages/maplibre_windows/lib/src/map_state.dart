import 'dart:async';
import 'dart:convert';
import 'dart:math' show cos, log, pi, pow;
import 'dart:typed_data';

import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:maplibre_platform_interface/maplibre_platform_interface.dart';
import 'package:maplibre_windows/src/native_channel.dart';
import 'package:maplibre_windows/src/projection.dart';
import 'package:maplibre_windows/src/style_resolver.dart';

part 'style_controller.dart';

/// MapLibre Native renderer for Windows via texture + method channel.
final class MapLibreMapStateWindows extends MapLibreMapState implements MapGestureControl {
  int? _textureId;
  StreamSubscription<Map<String, dynamic>>? _eventSub;
  bool _dragPanEnabled = true;
  Offset? _lastPanPoint;
  bool _styleLoaded = false;
  Size _mapSize = Size.zero;
  Future<void>? _ensureFuture;

  @override
  StyleControllerWindows? style;

  @override
  Widget buildPlatformWidget(BuildContext context) {
    return LayoutBuilder(
      builder: (context, constraints) {
        final width = constraints.maxWidth.floor();
        final height = constraints.maxHeight.floor();
        if (width <= 0 || height <= 0) {
          return const SizedBox.shrink();
        }
        return _MapTextureHost(
          width: width,
          height: height,
          onReady: (w, h) {
            _mapSize = Size(w.toDouble(), h.toDouble());
            return _ensureMap(w, h);
          },
          textureId: _textureId,
          onPointer: _onPointer,
        );
      },
    );
  }

  Future<void> _ensureMap(int width, int height) async {
    if (_textureId != null) {
      await NativeMapChannel.resizeMap(_textureId!, width, height);
      return;
    }
    if (_ensureFuture != null) {
      await _ensureFuture;
      return;
    }
    _ensureFuture = _createMap(width, height);
    try {
      await _ensureFuture;
    } finally {
      _ensureFuture = null;
    }
  }

  Future<void> _createMap(int width, int height) async {
    final pixelRatio = MediaQuery.devicePixelRatioOf(context);
    final center = options.initCenter;

    String? stylePayload;
    if (options.initStyle.isNotEmpty) {
      try {
        stylePayload = await resolveStylePayloadForNative(options.initStyle);
      } catch (error, stackTrace) {
        debugPrint('maplibre_windows: resolveStyle failed: $error');
        debugPrint('$stackTrace');
      }
    }
    if (!mounted) return;

    final textureId = await NativeMapChannel.createMap(
      width: width,
      height: height,
      pixelRatio: pixelRatio,
      initZoom: options.initZoom,
      initLat: center?.lat,
      initLon: center?.lon,
      initBearing: options.initBearing,
      initPitch: options.initPitch,
    );
    if (!mounted) {
      await NativeMapChannel.disposeMap(textureId);
      return;
    }
    _textureId = textureId;
    style = StyleControllerWindows._(textureId);
    if (mounted) {
      setState(() {});
    }
    _eventSub = NativeMapChannel.eventStream(textureId).listen(_onNativeEvent);
    if (stylePayload != null && stylePayload.isNotEmpty) {
      try {
        await NativeMapChannel.setStyle(textureId, stylePayload);
      } catch (error, stackTrace) {
        debugPrint('maplibre_windows: setStyle failed: $error');
        debugPrint('$stackTrace');
      }
    }
    widget.onEvent?.call(MapEventMapCreated(mapController: this));
    widget.onMapCreated?.call(this);
    await _refreshCamera();
  }

  Future<void> _refreshCamera() async {
    if (_textureId == null) return;
    try {
      final data = await NativeMapChannel.getCamera(_id);
      final next = MapCamera(
        center: Geographic(
          lon: (data['lon'] as num?)?.toDouble() ?? 0,
          lat: (data['lat'] as num?)?.toDouble() ?? 0,
        ),
        zoom: (data['zoom'] as num?)?.toDouble() ?? options.initZoom,
        bearing: (data['bearing'] as num?)?.toDouble() ?? options.initBearing,
        pitch: (data['pitch'] as num?)?.toDouble() ?? options.initPitch,
      );
      if (!mounted) return;
      setState(() {
        camera = next;
        isInitialized = true;
      });
      widget.onEvent?.call(MapEventMoveCamera(camera: next));
    } on PlatformException catch (e) {
      debugPrint('maplibre_windows: getCamera failed: ${e.code} ${e.message}');
    }
  }

  DateTime? _lastPointerMoveSent;

  void _onNativeEvent(Map<String, dynamic> event) {
    final type = event['type'] as String?;
    switch (type) {
      case 'styleLoaded':
        if (!_styleLoaded && style != null) {
          _styleLoaded = true;
          unawaited((style as StyleControllerWindows).refreshLayerIds());
          widget.onEvent?.call(MapEventStyleLoaded(style!));
          widget.onStyleLoaded?.call(style!);
          layerManager = LayerManager(style!, widget.layers);
        }
      case 'mapIdle':
        widget.onEvent?.call(const MapEventIdle());
      case 'cameraChanged':
        break;
      case 'cameraIdle':
        unawaited(_refreshCamera());
        widget.onEvent?.call(const MapEventCameraIdle());
      case 'click':
        final lon = (event['lon'] as num?)?.toDouble() ?? 0;
        final lat = (event['lat'] as num?)?.toDouble() ?? 0;
        final x = (event['x'] as num?)?.toDouble() ?? 0;
        final y = (event['y'] as num?)?.toDouble() ?? 0;
        widget.onEvent?.call(
          MapEventClick(
            point: Geographic(lon: lon, lat: lat),
            screenPoint: Offset(x, y),
          ),
        );
    }
  }

  void _onPointer(PointerEvent event) {
    final textureId = _textureId;
    if (textureId == null) return;

    if (event is PointerScrollEvent) {
      unawaited(NativeMapChannel.onPointer(
        textureId,
        phase: 'scroll',
        x: event.localPosition.dx,
        y: event.localPosition.dy,
        scrollDelta: event.scrollDelta.dy,
        shift: HardwareKeyboard.instance.isShiftPressed,
        control: HardwareKeyboard.instance.isControlPressed,
      ));
      return;
    }

    if (!_dragPanEnabled) return;

    if (event is PointerDownEvent) {
      _lastPanPoint = event.localPosition;
      unawaited(NativeMapChannel.onPointer(
        textureId,
        phase: 'down',
        x: event.localPosition.dx,
        y: event.localPosition.dy,
      ));
    } else if (event is PointerMoveEvent && _lastPanPoint != null) {
      final now = DateTime.now();
      if (_lastPointerMoveSent != null &&
          now.difference(_lastPointerMoveSent!) < const Duration(milliseconds: 16)) {
        return;
      }
      _lastPointerMoveSent = now;
      unawaited(NativeMapChannel.onPointer(
        textureId,
        phase: 'move',
        x: event.localPosition.dx,
        y: event.localPosition.dy,
      ));
    } else if (event is PointerUpEvent || event is PointerCancelEvent) {
      _lastPanPoint = null;
      unawaited(NativeMapChannel.onPointer(
        textureId,
        phase: 'up',
        x: event.localPosition.dx,
        y: event.localPosition.dy,
      ));
    }
  }

  int get _id {
    final id = _textureId;
    if (id == null) throw StateError('Map texture not initialized');
    return id;
  }

  @override
  Future<void> animateCamera({
    Geographic? center,
    double? zoom,
    double? bearing,
    double? pitch,
    Duration nativeDuration = const Duration(seconds: 2),
    double webSpeed = 1.2,
    Duration? webMaxDuration,
    EdgeInsets padding = EdgeInsets.zero,
  }) =>
      NativeMapChannel.animateCamera(
        _id,
        lat: center?.lat,
        lon: center?.lon,
        zoom: zoom,
        bearing: bearing,
        pitch: pitch,
        durationMs: nativeDuration.inMilliseconds,
      );

  @override
  Future<void> moveCamera({
    Geographic? center,
    double? zoom,
    double? bearing,
    double? pitch,
    EdgeInsets padding = EdgeInsets.zero,
  }) =>
      NativeMapChannel.moveCamera(
        _id,
        lat: center?.lat,
        lon: center?.lon,
        zoom: zoom,
        bearing: bearing,
        pitch: pitch,
      );

  @override
  Future<void> fitBounds({
    required LngLatBounds bounds,
    double? bearing,
    double? pitch,
    Duration nativeDuration = const Duration(seconds: 2),
    double webSpeed = 1.2,
    Duration? webMaxDuration,
    Offset offset = Offset.zero,
    double webMaxZoom = double.maxFinite,
    bool webLinear = false,
    EdgeInsets padding = EdgeInsets.zero,
  }) async {
    final center = Geographic(
      lon: (bounds.longitudeWest + bounds.longitudeEast) / 2,
      lat: (bounds.latitudeSouth + bounds.latitudeNorth) / 2,
    );
    final latSpan = (bounds.latitudeNorth - bounds.latitudeSouth).abs();
    final lonSpan = (bounds.longitudeEast - bounds.longitudeWest).abs();
    final span = latSpan > lonSpan ? latSpan : lonSpan;
    final zoom = span <= 0
        ? options.initZoom
        : (_log2(360 / span) - 1).clamp(options.minZoom, options.maxZoom);
    await animateCamera(
      center: center,
      zoom: zoom,
      bearing: bearing,
      pitch: pitch,
      nativeDuration: nativeDuration,
    );
  }

  double _log2(double x) => log(x) / log(2);

  @override
  MapCamera getCamera() {
    return camera ??
        MapCamera(
          center: options.initCenter ?? const Geographic(lon: 0, lat: 0),
          zoom: options.initZoom,
          bearing: options.initBearing,
          pitch: options.initPitch,
        );
  }

  @override
  Future<void> enableLocation({
    Duration fastestInterval = const Duration(milliseconds: 750),
    Duration maxWaitTime = const Duration(seconds: 1),
    bool pulseFade = true,
    bool accuracyAnimation = true,
    bool compassAnimation = true,
    bool pulse = true,
    BearingRenderMode bearingRenderMode = BearingRenderMode.gps,
  }) async {}

  @override
  List<RenderedFeature> featuresAtPoint(
    Offset point, {
    List<String>? layerIds,
    double radius = 0,
  }) {
    throw UnsupportedError('featuresAtPoint is sync-only stub on Windows; use async query via channel');
  }

  @override
  List<RenderedFeature> featuresInRect(
    Rect rect, {
    List<String>? layerIds,
  }) {
    throw UnimplementedError('featuresInRect not yet implemented on Windows');
  }

  @override
  List<QueriedLayer> queryLayers(Offset point) => const [];

  @override
  void setStyle(String style) {
    unawaited(() async {
      final payload = await resolveStylePayloadForNative(style);
      await NativeMapChannel.setStyle(_id, payload);
    }());
  }

  @override
  Future<void> trackLocation({
    bool trackLocation = true,
    BearingTrackMode trackBearing = BearingTrackMode.gps,
  }) async {}

  @override
  Geographic toLngLat(Offset screenLocation) {
    final cam = camera ?? getCamera();
    if (_mapSize == Size.zero) {
      return cam.center;
    }
    return WindowsMapProjection.toLngLat(cam, _mapSize, screenLocation);
  }

  @override
  List<Geographic> toLngLats(List<Offset> screenLocations) =>
      screenLocations.map(toLngLat).toList();

  @override
  Offset toScreenLocation(Geographic lngLat) {
    final cam = camera ?? getCamera();
    if (_mapSize == Size.zero) {
      return Offset.zero;
    }
    return WindowsMapProjection.toScreenLocation(cam, _mapSize, lngLat);
  }

  @override
  List<Offset> toScreenLocations(List<Geographic> lngLats) =>
      lngLats.map(toScreenLocation).toList();

  @override
  double getMetersPerPixelAtLatitude(double latitude) {
    final z = camera?.zoom ?? options.initZoom;
    return 156543.03392 * cos(latitude * pi / 180) / pow(2, z);
  }

  @override
  LngLatBounds getVisibleRegion() {
    final c = camera?.center ?? options.initCenter ?? const Geographic(lon: 0, lat: 0);
    final z = camera?.zoom ?? options.initZoom;
    final latDelta = 360 / pow(2, z) / 2;
    final lonDelta = latDelta;
    return LngLatBounds(
      longitudeWest: c.lon - lonDelta,
      longitudeEast: c.lon + lonDelta,
      latitudeSouth: c.lat - latDelta,
      latitudeNorth: c.lat + latDelta,
    );
  }

  @override
  void setDragPanEnabled(bool enabled) {
    _dragPanEnabled = enabled;
    unawaited(NativeMapChannel.setDragPanEnabled(_id, enabled));
  }

  @override
  void dispose() {
    final textureId = _textureId;
    _eventSub?.cancel();
    if (textureId != null) {
      unawaited(NativeMapChannel.disposeMap(textureId));
    }
    style?.dispose();
    super.dispose();
  }
}

class _MapTextureHost extends StatefulWidget {
  const _MapTextureHost({
    required this.width,
    required this.height,
    required this.onReady,
    required this.textureId,
    required this.onPointer,
  });

  final int width;
  final int height;
  final Future<void> Function(int width, int height) onReady;
  final int? textureId;
  final void Function(PointerEvent event) onPointer;

  @override
  State<_MapTextureHost> createState() => _MapTextureHostState();
}

class _MapTextureHostState extends State<_MapTextureHost> {
  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) {
      widget.onReady(widget.width, widget.height);
    });
  }

  @override
  void didUpdateWidget(covariant _MapTextureHost oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.width != widget.width || oldWidget.height != widget.height) {
      widget.onReady(widget.width, widget.height);
    }
  }

  @override
  Widget build(BuildContext context) {
    final textureId = widget.textureId;
    if (textureId == null) {
      return const ColoredBox(color: Color(0xFFE0E0E0));
    }
    return Listener(
      behavior: HitTestBehavior.opaque,
      onPointerDown: widget.onPointer,
      onPointerMove: widget.onPointer,
      onPointerUp: widget.onPointer,
      onPointerCancel: widget.onPointer,
      onPointerSignal: widget.onPointer,
      child: Texture(textureId: textureId),
    );
  }
}
