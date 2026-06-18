import 'package:maplibre_platform_interface/maplibre_platform_interface.dart';
import 'package:maplibre_windows/src/map_state.dart';

/// Windows native implementation of the federated MapLibre plugin.
final class MapLibrePlugin extends MapLibrePlatform {
  /// Registers [MapLibrePlugin] as the active platform implementation.
  static void registerWith() => MapLibrePlatform.instance = MapLibrePlugin();

  @override
  MapLibreMapState createWidgetState() => MapLibreMapStateWindows();

  @override
  Future<OfflineManager> createOfflineManager() =>
      throw UnsupportedError('OfflineManager is not supported on maplibre_windows yet.');

  @override
  PermissionManager createPermissionManager() =>
      throw UnsupportedError('PermissionManager is not supported on maplibre_windows yet.');

  @override
  bool get offlineManagerIsSupported => false;

  @override
  bool get permissionManagerIsSupported => false;

  @override
  bool get userLocationIsSupported => false;
}
