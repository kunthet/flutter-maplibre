import 'package:maplibre_platform_interface/src/map_controller.dart';

/// Optional map-controller capability for toggling drag-pan at runtime.
abstract interface class MapGestureControl {
  /// Enables or disables map drag-pan without changing other gestures.
  void setDragPanEnabled(bool enabled);
}

/// Toggles drag-pan when [controller] supports [MapGestureControl].
void setMapDragPanEnabled(MapController? controller, bool enabled) {
  if (controller case final MapGestureControl gestureControl) {
    gestureControl.setDragPanEnabled(enabled);
  }
}
