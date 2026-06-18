part of 'map_state.dart';

/// Windows native [StyleController] backed by the C++ map embedder.
final class StyleControllerWindows extends StyleController {
  StyleControllerWindows._(this._textureId);

  final int _textureId;

  @override
  Future<void> addImage(String id, Uint8List bytes) =>
      NativeMapChannel.addImage(_textureId, id, bytes);

  @override
  Future<void> addLayer(
    StyleLayer layer, {
    String? belowLayerId,
    String? aboveLayerId,
    int? atIndex,
  }) {
    final layerJson = _encodeLayer(layer);
    final before = belowLayerId ?? aboveLayerId;
    return NativeMapChannel.addLayer(_textureId, layerJson, belowLayerId: before);
  }

  @override
  Future<void> addSource(Source source) =>
      NativeMapChannel.addSource(_textureId, source.id, _encodeSource(source));

  @override
  Future<List<String>> getAttributions() =>
      NativeMapChannel.getAttributions(_textureId);

  @override
  List<String> getAttributionsSync() => const [];

  List<String> _cachedLayerIds = const [];

  @override
  List<String> getLayerIds() => _cachedLayerIds;

  Future<void> refreshLayerIds() async {
    _cachedLayerIds = await NativeMapChannel.getLayerIds(_textureId);
  }

  @override
  Future<void> removeImage(String id) => NativeMapChannel.removeImage(_textureId, id);

  @override
  Future<void> removeLayer(String id) => NativeMapChannel.removeLayer(_textureId, id);

  @override
  Future<void> removeSource(String id) => NativeMapChannel.removeSource(_textureId, id);

  @override
  void setProjection(MapProjection projection) {}

  @override
  Future<void> updateGeoJsonSource({required String id, required String data}) =>
      NativeMapChannel.updateGeoJsonSource(_textureId, id, data);

  @override
  Future<void> updateLayerFilter({required String id, Object? filter}) =>
      NativeMapChannel.updateLayerFilter(
        _textureId,
        id,
        filter == null ? null : jsonEncode(filter),
      );

  @override
  Future<void> updateVectorSourceTiles({
    required String id,
    required List<String> tiles,
  }) =>
      NativeMapChannel.updateVectorSourceTiles(_textureId, id, tiles);

  @override
  void dispose() {}

  String _encodeSource(Source source) {
    final map = <String, dynamic>{'id': source.id};
    switch (source) {
      case GeoJsonSource():
        map['type'] = 'geojson';
        map['data'] = source.data;
      case VectorSource():
        map['type'] = 'vector';
        if (source.tiles != null) map['tiles'] = source.tiles;
        if (source.url != null) map['url'] = source.url;
      case RasterSource():
        map['type'] = 'raster';
        if (source.tiles != null) map['tiles'] = source.tiles;
        if (source.url != null) map['url'] = source.url;
        map['tileSize'] = source.tileSize;
        if (source.attribution != null) map['attribution'] = source.attribution;
      case RasterDemSource():
        map['type'] = 'raster-dem';
        if (source.tiles != null) map['tiles'] = source.tiles;
        if (source.url != null) map['url'] = source.url;
        map['tileSize'] = source.tileSize;
      case ImageSource():
        map['type'] = 'image';
        map['url'] = source.url;
        map['coordinates'] = [
          [source.coordinates.topLeft.lon, source.coordinates.topLeft.lat],
          [source.coordinates.topRight.lon, source.coordinates.topRight.lat],
          [source.coordinates.bottomRight.lon, source.coordinates.bottomRight.lat],
          [source.coordinates.bottomLeft.lon, source.coordinates.bottomLeft.lat],
        ];
      default:
        throw UnsupportedError('Source type ${source.runtimeType} not supported on Windows');
    }
    return jsonEncode(map);
  }

  String _encodeLayer(StyleLayer layer) {
    final map = <String, dynamic>{
      'id': layer.id,
      'type': switch (layer) {
        FillStyleLayer() => 'fill',
        LineStyleLayer() => 'line',
        SymbolStyleLayer() => 'symbol',
        CircleStyleLayer() => 'circle',
        HeatmapStyleLayer() => 'heatmap',
        FillExtrusionStyleLayer() => 'fill-extrusion',
        RasterStyleLayer() => 'raster',
        HillshadeStyleLayer() => 'hillshade',
        BackgroundStyleLayer() => 'background',
        _ => throw UnsupportedError('Layer type ${layer.runtimeType}'),
      },
      'paint': layer.paint,
      'layout': layer.layout,
      'minzoom': layer.minZoom,
      'maxzoom': layer.maxZoom,
    };
    if (layer.filter != null) map['filter'] = layer.filter;
    if (layer is StyleLayerWithSource) {
      map['source'] = layer.sourceId;
      if (layer.sourceLayerId != null) map['source-layer'] = layer.sourceLayerId;
    }
    return jsonEncode(map);
  }
}
