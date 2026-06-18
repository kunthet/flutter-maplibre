import 'dart:math';

import 'package:flutter/widgets.dart';
import 'package:maplibre_platform_interface/maplibre_platform_interface.dart';

/// Mercator screen projection using current camera (matches webview math).
final class WindowsMapProjection {
  const WindowsMapProjection._();

  static const double _tileSize = 512;

  static double _wrapDelta(double dx, double worldSize) {
    var wrapped = (dx + worldSize / 2) % worldSize;
    if (wrapped < 0) wrapped += worldSize;
    return wrapped - worldSize / 2;
  }

  static Offset _projectWebMercator(Geographic g) {
    final x = (g.lon + 180) / 360;
    final sinLat = sin(g.lat * pi / 180);
    final y = 0.5 - log((1 + sinLat) / (1 - sinLat)) / (4 * pi);
    return Offset(x, y);
  }

  static Geographic _unprojectWebMercator(Offset p) {
    final lon = p.dx * 360 - 180;
    final n = pi - 2 * pi * p.dy;
    final lat = 180 / pi * atan(0.5 * (exp(n) - exp(-n)));
    return Geographic(lon: lon, lat: lat);
  }

  static Offset toScreenLocation(
    MapCamera camera,
    Size mapSize,
    Geographic lngLat,
  ) {
    final scale = pow(2.0, camera.zoom) as double;
    final centerWorld = _projectWebMercator(camera.center) * scale;
    final worldSizePx = _tileSize * scale;

    var p = _projectWebMercator(lngLat) * scale - centerWorld;
    p = Offset(_wrapDelta(p.dx, worldSizePx), p.dy);

    final bearingRad = -camera.bearing * pi / 180;
    final cosB = cos(bearingRad);
    final sinB = sin(bearingRad);
    p = Offset(p.dx * cosB - p.dy * sinB, p.dx * sinB + p.dy * cosB);

    final pitchRad = camera.pitch * pi / 180;
    p = Offset(p.dx, p.dy * cos(pitchRad));

    return Offset(mapSize.width / 2 + p.dx, mapSize.height / 2 + p.dy);
  }

  static Geographic toLngLat(
    MapCamera camera,
    Size mapSize,
    Offset screenPoint,
  ) {
    final scale = pow(2.0, camera.zoom) as double;
    final centerWorld = _projectWebMercator(camera.center) * scale;
    final worldSizePx = _tileSize * scale;

    var p = Offset(
      screenPoint.dx - mapSize.width / 2,
      screenPoint.dy - mapSize.height / 2,
    );

    final pitchRad = camera.pitch * pi / 180;
    p = Offset(p.dx, p.dy / cos(pitchRad));

    final bearingRad = camera.bearing * pi / 180;
    final cosB = cos(bearingRad);
    final sinB = sin(bearingRad);
    p = Offset(p.dx * cosB - p.dy * sinB, p.dx * sinB + p.dy * cosB);

    final world = centerWorld + Offset(_wrapDelta(p.dx, worldSizePx), p.dy);
    return _unprojectWebMercator(world / scale);
  }
}
