part of 'map_options.dart';

/// Configure gestures that are enabled on the map.
///
/// {@category Basic}
@immutable
class MapGestures {
  /// Create a new [MapGestures] object by setting all gestures.
  const MapGestures({
    required this.rotate,
    required this.pan,
    required this.zoom,
    required this.pitch,
    this.invertWheelZoom = false,
  });

  /// Create a new [MapGestures] object by enabling all gestures or disabling
  /// just some gestures.
  const MapGestures.all({
    this.rotate = true,
    this.pan = true,
    this.zoom = true,
    this.pitch = true,
    this.invertWheelZoom = false,
  });

  /// Create a new [MapGestures] object by disable all gestures or enabling just
  /// some gestures.
  const MapGestures.none({
    this.rotate = false,
    this.pan = false,
    this.zoom = false,
    this.pitch = false,
    this.invertWheelZoom = false,
  });

  /// Rotate the map bearing.
  final bool rotate;

  /// Move the center of the map around.
  final bool pan;

  /// Zoom the map in and out.
  final bool zoom;

  /// Tilt (pitch) the map camera.
  final bool pitch;

  /// When `false` (default), mouse-wheel zoom matches MapLibre GLFW / native
  /// desktop (scroll up zooms in). When `true`, uses Flutter's raw scroll
  /// delta sign (useful for natural scrolling or trackpad conventions).
  final bool invertWheelZoom;

  /// Returns true if all gestures are active
  bool get allEnabled => rotate && pan && zoom && pitch;

  @override
  bool operator ==(Object other) =>
      identical(this, other) ||
      other is MapGestures &&
          runtimeType == other.runtimeType &&
          rotate == other.rotate &&
          pan == other.pan &&
          zoom == other.zoom &&
          pitch == other.pitch &&
          invertWheelZoom == other.invertWheelZoom;

  @override
  int get hashCode => Object.hash(rotate, pan, zoom, pitch, invertWheelZoom);
}
