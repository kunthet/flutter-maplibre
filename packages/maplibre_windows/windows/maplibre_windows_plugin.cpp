#include <maplibre_windows/maplibre_windows_plugin.h>

#include <flutter/event_stream_handler_functions.h>
#include <flutter/standard_method_codec.h>

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "crash_logger.h"
#include "map_embedder.h"
#include "map_gpu_texture.h"
#include "map_texture.h"

namespace {

using maplibre_windows::CameraState;
using maplibre_windows::MapEmbedder;
using maplibre_windows::MapGpuTexture;
using maplibre_windows::MapTexture;

struct MapInstance {
    std::unique_ptr<MapEmbedder> embedder;
    std::unique_ptr<MapTexture> texture;          // CPU pixel-buffer path
    std::unique_ptr<MapGpuTexture> gpu_texture;   // zero-copy GPU surface path
    std::unique_ptr<flutter::TextureVariant> flutter_texture;
    int64_t texture_id = -1;
    std::unique_ptr<flutter::EventSink<flutter::EncodableValue>> event_sink;
};

std::mutex g_maps_mutex;
std::map<int64_t, std::unique_ptr<MapInstance>> g_maps;
int64_t g_next_texture_id = 1;

std::mutex g_pending_events_mutex;
std::map<int64_t, std::vector<std::string>> g_pending_events;

std::optional<double> GetDouble(const flutter::EncodableMap& map, const char* key) {
    const auto it = map.find(flutter::EncodableValue(key));
    if (it == map.end()) return std::nullopt;
    if (const auto* v = std::get_if<double>(&it->second)) return *v;
    if (const auto* i = std::get_if<int32_t>(&it->second)) return static_cast<double>(*i);
    return std::nullopt;
}

std::optional<std::string> GetString(const flutter::EncodableMap& map, const char* key) {
    const auto it = map.find(flutter::EncodableValue(key));
    if (it == map.end()) return std::nullopt;
    if (const auto* v = std::get_if<std::string>(&it->second)) return *v;
    return std::nullopt;
}

std::optional<int64_t> GetTextureIdFromValue(const flutter::EncodableValue& value) {
    if (const auto* v = std::get_if<int32_t>(&value)) {
        return *v;
    }
    if (const auto* l = std::get_if<int64_t>(&value)) {
        return *l;
    }
    return std::nullopt;
}

std::optional<int64_t> GetTextureId(const flutter::EncodableMap& map) {
    const auto it = map.find(flutter::EncodableValue("textureId"));
    if (it == map.end()) return std::nullopt;
    if (const auto* v = std::get_if<int32_t>(&it->second)) return *v;
    if (const auto* l = std::get_if<int64_t>(&it->second)) return *l;
    return std::nullopt;
}

MapInstance* GetMap(int64_t texture_id) {
    const auto it = g_maps.find(texture_id);
    return it == g_maps.end() ? nullptr : it->second.get();
}

void FlushPendingEventsUnsafe(int64_t texture_id, MapInstance* map) {
    if (!map || !map->event_sink) {
        return;
    }
    std::vector<std::string> events;
    {
        std::lock_guard<std::mutex> lock(g_pending_events_mutex);
        const auto it = g_pending_events.find(texture_id);
        if (it == g_pending_events.end()) {
            return;
        }
        events = std::move(it->second);
        it->second.clear();
    }
    for (const auto& type : events) {
        map->event_sink->Success(flutter::EncodableMap{
            {flutter::EncodableValue("type"), type},
        });
    }
}

void FlushPendingEvents(int64_t texture_id) {
    std::lock_guard<std::mutex> lock(g_maps_mutex);
    FlushPendingEventsUnsafe(texture_id, GetMap(texture_id));
}

struct MapHandles {
    MapEmbedder* embedder = nullptr;
    MapTexture* texture = nullptr;
};

std::optional<MapHandles> ResolveMap(int64_t texture_id, bool require_embedder = true) {
    std::lock_guard<std::mutex> lock(g_maps_mutex);
    auto* map = GetMap(texture_id);
    if (!map) {
        return std::nullopt;
    }
    if (require_embedder && !map->embedder) {
        return std::nullopt;
    }
    return MapHandles{map->embedder.get(), map->texture.get()};
}

void DeliverFrame(int64_t texture_id, const uint8_t* data, size_t w, size_t h) {
    MapTexture* texture = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_maps_mutex);
        auto* map = GetMap(texture_id);
        if (!map || !map->texture) {
            return;
        }
        texture = map->texture.get();
    }
    texture->UpdatePixels(data, w, h);
    texture->MarkFrameAvailable();
}

void DeliverGpuFrame(int64_t texture_id, void* shared_handle, int w, int h) {
    MapGpuTexture* gpu_texture = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_maps_mutex);
        auto* map = GetMap(texture_id);
        if (!map || !map->gpu_texture) {
            return;
        }
        gpu_texture = map->gpu_texture.get();
    }
    gpu_texture->SetSurface(shared_handle, w, h);
    gpu_texture->MarkFrameAvailable();
}

void DeliverEvent(int64_t texture_id, const std::string& type) {
    std::lock_guard<std::mutex> lock(g_pending_events_mutex);
    g_pending_events[texture_id].push_back(type);
}

flutter::EncodableMap CameraToMap(const CameraState& camera) {
    return flutter::EncodableMap{
        {flutter::EncodableValue("lat"), camera.lat},
        {flutter::EncodableValue("lon"), camera.lon},
        {flutter::EncodableValue("zoom"), camera.zoom},
        {flutter::EncodableValue("bearing"), camera.bearing},
        {flutter::EncodableValue("pitch"), camera.pitch},
    };
}

CameraState CameraFromMap(const flutter::EncodableMap& map) {
    CameraState camera;
    if (const auto lat = GetDouble(map, "lat")) camera.lat = *lat;
    if (const auto lon = GetDouble(map, "lon")) camera.lon = *lon;
    if (const auto zoom = GetDouble(map, "zoom")) camera.zoom = *zoom;
    if (const auto bearing = GetDouble(map, "bearing")) camera.bearing = *bearing;
    if (const auto pitch = GetDouble(map, "pitch")) camera.pitch = *pitch;
    return camera;
}

}  // namespace

namespace maplibre_windows {

MaplibreWindowsPlugin::~MaplibreWindowsPlugin() {
    std::map<int64_t, std::unique_ptr<MapInstance>> maps;
    {
        std::lock_guard<std::mutex> lock(g_maps_mutex);
        maps.swap(g_maps);
    }
    for (auto& entry : maps) {
        if (!entry.second) {
            continue;
        }
        entry.second->event_sink.reset();
        entry.second->embedder.reset();
        if (texture_registrar_) {
            texture_registrar_->UnregisterTexture(entry.first);
        }
    }
}

void MaplibreWindowsPlugin::RegisterWithRegistrar(flutter::PluginRegistrarWindows* registrar) {
    InstallCrashLogger();
    auto plugin = std::make_unique<MaplibreWindowsPlugin>(registrar);
    registrar->AddPlugin(std::move(plugin));
}

MaplibreWindowsPlugin::MaplibreWindowsPlugin(flutter::PluginRegistrarWindows* registrar)
    : registrar_(registrar), texture_registrar_(registrar->texture_registrar()) {
    method_channel_ = std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
        registrar->messenger(),
        "maplibre_windows",
        &flutter::StandardMethodCodec::GetInstance());

    event_channel_ = std::make_unique<flutter::EventChannel<flutter::EncodableValue>>(
        registrar->messenger(),
        "maplibre_windows/events",
        &flutter::StandardMethodCodec::GetInstance());

    event_channel_->SetStreamHandler(
        std::make_unique<flutter::StreamHandlerFunctions<flutter::EncodableValue>>(
            [](const flutter::EncodableValue* arguments,
               std::unique_ptr<flutter::EventSink<flutter::EncodableValue>>&& events)
                -> std::unique_ptr<flutter::StreamHandlerError<flutter::EncodableValue>> {
                if (!arguments) {
                    return nullptr;
                }
                const auto texture_id = GetTextureIdFromValue(*arguments);
                if (!texture_id) {
                    return nullptr;
                }
                std::lock_guard<std::mutex> lock(g_maps_mutex);
                auto* map = GetMap(*texture_id);
                if (map) {
                    map->event_sink = std::move(events);
                    FlushPendingEventsUnsafe(*texture_id, map);
                }
                return nullptr;
            },
            [](const flutter::EncodableValue* arguments)
                -> std::unique_ptr<flutter::StreamHandlerError<flutter::EncodableValue>> {
                if (!arguments) {
                    return nullptr;
                }
                const auto texture_id = GetTextureIdFromValue(*arguments);
                if (!texture_id) {
                    return nullptr;
                }
                std::lock_guard<std::mutex> lock(g_maps_mutex);
                auto* map = GetMap(*texture_id);
                if (map) {
                    map->event_sink.reset();
                }
                return nullptr;
            }));

    method_channel_->SetMethodCallHandler(
        [this](const auto& call, auto result) { HandleMethodCall(call, std::move(result)); });
}

void MaplibreWindowsPlugin::HandleMethodCall(
    const flutter::MethodCall<flutter::EncodableValue>& method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
    const auto& method = method_call.method_name();
    const auto* args = std::get_if<flutter::EncodableMap>(method_call.arguments());

    if (method == "createMap") {
        if (!args) {
            result->Error("invalid_args", "createMap requires arguments");
            return;
        }
        const int width = static_cast<int>(GetDouble(*args, "width").value_or(256));
        const int height = static_cast<int>(GetDouble(*args, "height").value_or(256));
        const float pixel_ratio = static_cast<float>(GetDouble(*args, "pixelRatio").value_or(1.0));
        const auto init_style = GetString(*args, "initStyle").value_or("");

        std::optional<CameraState> init_camera;
        CameraState camera;
        bool has_camera = false;
        if (const auto lat = GetDouble(*args, "initLat")) {
            camera.lat = *lat;
            has_camera = true;
        }
        if (const auto lon = GetDouble(*args, "initLon")) {
            camera.lon = *lon;
            has_camera = true;
        }
        if (const auto zoom = GetDouble(*args, "initZoom")) {
            camera.zoom = *zoom;
            has_camera = true;
        }
        if (const auto bearing = GetDouble(*args, "initBearing")) {
            camera.bearing = *bearing;
            has_camera = true;
        }
        if (const auto pitch = GetDouble(*args, "initPitch")) {
            camera.pitch = *pitch;
            has_camera = true;
        }
        if (has_camera) init_camera = camera;

        auto instance = std::make_unique<MapInstance>();
        auto* registrar = texture_registrar_;

        // The texture id is only known after registration, but the embedder's
        // frame callbacks (which fire from the map thread) need it. Share it via
        // a holder that stays -1 until the instance is fully discoverable.
        auto texture_id_holder = std::make_shared<int64_t>(-1);

        // Construct the embedder first: it determines whether the zero-copy GPU
        // surface path is available, which decides the texture variant to
        // register. Frames produced before the holder is set are dropped.
        std::unique_ptr<MapEmbedder> embedder;
        try {
            embedder = std::make_unique<MapEmbedder>(
                width,
                height,
                pixel_ratio,
                /*init_style=*/"",
                init_camera,
                [texture_id_holder](const std::string& type, const std::string&) {
                    if (*texture_id_holder >= 0) DeliverEvent(*texture_id_holder, type);
                },
                [texture_id_holder](const uint8_t* data, size_t w, size_t h) {
                    if (*texture_id_holder >= 0) DeliverFrame(*texture_id_holder, data, w, h);
                },
                [texture_id_holder](void* handle, int w, int h) {
                    if (*texture_id_holder >= 0) DeliverGpuFrame(*texture_id_holder, handle, w, h);
                });
        } catch (const std::exception& ex) {
            result->Error("init_failed", ex.what());
            return;
        }

        const auto mark_frame_available = [texture_id_holder, registrar]() {
            if (*texture_id_holder >= 0) {
                registrar->MarkTextureFrameAvailable(*texture_id_holder);
            }
        };

        if (embedder->IsGpuMode()) {
            instance->gpu_texture = std::make_unique<MapGpuTexture>(mark_frame_available);
            instance->flutter_texture = std::make_unique<flutter::TextureVariant>(flutter::GpuSurfaceTexture(
                kFlutterDesktopGpuSurfaceTypeDxgiSharedHandle,
                [tex = instance->gpu_texture.get()](size_t w, size_t h) { return tex->ObtainDescriptor(w, h); }));
        } else {
            instance->texture = std::make_unique<MapTexture>(width, height, mark_frame_available);
            instance->flutter_texture = std::make_unique<flutter::TextureVariant>(flutter::PixelBufferTexture(
                [tex = instance->texture.get()](size_t w, size_t h) { return tex->CopyPixelBuffer(w, h); }));
        }

        const int64_t texture_id = registrar->RegisterTexture(instance->flutter_texture.get());
        instance->texture_id = texture_id;
        instance->embedder = std::move(embedder);

        {
            std::lock_guard<std::mutex> lock(g_maps_mutex);
            g_maps[texture_id] = std::move(instance);
        }

        // Enable frame delivery only now that the instance is registered and
        // discoverable from the map thread's callbacks.
        *texture_id_holder = texture_id;

        result->Success(flutter::EncodableValue(texture_id));
        return;
    }

    if (method == "disposeMap") {
        if (!args) {
            result->Error("invalid_args", "Missing arguments");
            return;
        }
        const auto texture_id = GetTextureId(*args);
        if (!texture_id) {
            result->Error("invalid_args", "Missing textureId");
            return;
        }
        std::unique_ptr<MapInstance> removed;
        {
            std::lock_guard<std::mutex> lock(g_maps_mutex);
            const auto it = g_maps.find(*texture_id);
            if (it != g_maps.end()) {
                removed = std::move(it->second);
                g_maps.erase(it);
            }
        }
        if (removed) {
            removed->event_sink.reset();
            removed->embedder.reset();
            texture_registrar_->UnregisterTexture(removed->texture_id);
        }
        result->Success();
        return;
    }

    if (!args) {
        result->Error("invalid_args", "Missing arguments");
        return;
    }

    const auto texture_id = GetTextureId(*args);
    if (!texture_id) {
        result->Error("invalid_args", "Missing textureId");
        return;
    }

    const auto handles = ResolveMap(*texture_id);
    if (!handles) {
        result->Error("not_found", "Map not found");
        return;
    }
    FlushPendingEvents(*texture_id);
    auto* embedder = handles->embedder;
    auto* texture = handles->texture;

    if (method == "resizeMap") {
        const int width = static_cast<int>(GetDouble(*args, "width").value_or(256));
        const int height = static_cast<int>(GetDouble(*args, "height").value_or(256));
        if (width <= 0 || height <= 0) {
            result->Success();
            return;
        }
        // CPU pixel-buffer path only; in GPU mode texture is null and the shared
        // surface is resized on the next Publish(EnsureSize).
        if (texture) {
            texture->Resize(width, height);
        } else {
            std::lock_guard<std::mutex> lock(g_maps_mutex);
            if (auto* map = GetMap(*texture_id); map && map->gpu_texture) {
                map->gpu_texture->InvalidateSurface();
            }
        }
        embedder->Resize(width, height);
        result->Success();
        return;
    }

    if (method == "setStyle") {
        embedder->SetStyle(GetString(*args, "style").value_or(""));
        result->Success();
        return;
    }

    if (method == "moveCamera" || method == "animateCamera") {
        const auto camera = CameraFromMap(*args);
        if (method == "moveCamera") {
            embedder->MoveCamera(camera);
        } else {
            const int duration = static_cast<int>(GetDouble(*args, "durationMs").value_or(2000));
            embedder->AnimateCamera(camera, duration);
        }
        result->Success();
        return;
    }

    if (method == "getCamera") {
        result->Success(flutter::EncodableValue(CameraToMap(embedder->GetCamera())));
        return;
    }

    if (method == "toLngLat") {
        const double x = GetDouble(*args, "x").value_or(0);
        const double y = GetDouble(*args, "y").value_or(0);
        const auto lng_lat = embedder->ToLngLat(x, y);
        result->Success(flutter::EncodableValue(flutter::EncodableList{
            lng_lat.second,
            lng_lat.first,
        }));
        return;
    }

    if (method == "toScreenLocation") {
        const double lon = GetDouble(*args, "lon").value_or(0);
        const double lat = GetDouble(*args, "lat").value_or(0);
        const auto screen = embedder->ToScreenLocation(lon, lat);
        result->Success(flutter::EncodableValue(flutter::EncodableList{screen.first, screen.second}));
        return;
    }

    if (method == "setDragPanEnabled") {
        const auto it = args->find(flutter::EncodableValue("enabled"));
        const bool enabled = it != args->end() && std::get<bool>(it->second);
        embedder->SetDragPanEnabled(enabled);
        result->Success();
        return;
    }

    if (method == "setInvertWheelZoom") {
        const auto it = args->find(flutter::EncodableValue("invert"));
        const bool invert = it != args->end() && std::get<bool>(it->second);
        embedder->SetInvertWheelZoom(invert);
        result->Success();
        return;
    }

    if (method == "addSource") {
        embedder->AddSource(GetString(*args, "id").value_or(""),
                            GetString(*args, "sourceJson").value_or(""));
        result->Success();
        return;
    }

    if (method == "addLayer") {
        embedder->AddLayer(GetString(*args, "layerJson").value_or(""),
                           GetString(*args, "belowLayerId"));
        result->Success();
        return;
    }

    if (method == "removeLayer") {
        embedder->RemoveLayer(GetString(*args, "id").value_or(""));
        result->Success();
        return;
    }

    if (method == "removeSource") {
        embedder->RemoveSource(GetString(*args, "id").value_or(""));
        result->Success();
        return;
    }

    if (method == "updateGeoJsonSource") {
        embedder->UpdateGeoJsonSource(GetString(*args, "id").value_or(""),
                                      GetString(*args, "data").value_or(""));
        result->Success();
        return;
    }

    if (method == "updateLayerFilter") {
        const auto id = GetString(*args, "id").value_or("");
        const auto filter_it = args->find(flutter::EncodableValue("filter"));
        std::string filter_json = filter_it != args->end() && std::holds_alternative<std::string>(filter_it->second)
                                      ? std::get<std::string>(filter_it->second)
                                      : "null";
        embedder->UpdateLayerFilter(id, filter_json);
        result->Success();
        return;
    }

    if (method == "updateVectorSourceTiles") {
        const auto id = GetString(*args, "id").value_or("");
        std::vector<std::string> tiles;
        const auto tiles_it = args->find(flutter::EncodableValue("tiles"));
        if (tiles_it != args->end()) {
            if (const auto* list = std::get_if<flutter::EncodableList>(&tiles_it->second)) {
                for (const auto& item : *list) {
                    if (const auto* s = std::get_if<std::string>(&item)) {
                        tiles.push_back(*s);
                    }
                }
            }
        }
        embedder->UpdateVectorSourceTiles(id, tiles);
        result->Success();
        return;
    }

    if (method == "addImage") {
        const auto id = GetString(*args, "id").value_or("");
        const auto bytes_it = args->find(flutter::EncodableValue("bytes"));
        std::vector<uint8_t> bytes;
        if (bytes_it != args->end()) {
            if (const auto* data = std::get_if<std::vector<uint8_t>>(&bytes_it->second)) {
                bytes = *data;
            }
        }
        embedder->AddImage(id, bytes);
        result->Success();
        return;
    }

    if (method == "removeImage") {
        embedder->RemoveImage(GetString(*args, "id").value_or(""));
        result->Success();
        return;
    }

    if (method == "getLayerIds") {
        const auto ids = embedder->GetLayerIds();
        flutter::EncodableList list;
        for (const auto& id : ids) {
            list.emplace_back(id);
        }
        result->Success(flutter::EncodableValue(list));
        return;
    }

    if (method == "getAttributions") {
        const auto attrs = embedder->GetAttributions();
        flutter::EncodableList list;
        for (const auto& a : attrs) {
            list.emplace_back(a);
        }
        result->Success(flutter::EncodableValue(list));
        return;
    }

    if (method == "onPointer") {
        embedder->OnPointer(
            GetString(*args, "phase").value_or(""),
            GetDouble(*args, "x").value_or(0),
            GetDouble(*args, "y").value_or(0),
            GetDouble(*args, "scrollDelta").value_or(0),
            [&]() {
                const auto it = args->find(flutter::EncodableValue("shift"));
                return it != args->end() && std::get<bool>(it->second);
            }(),
            [&]() {
                const auto it = args->find(flutter::EncodableValue("control"));
                return it != args->end() && std::get<bool>(it->second);
            }());
        result->Success();
        return;
    }

    if (method == "featuresAtPoint") {
        const double x = GetDouble(*args, "x").value_or(0);
        const double y = GetDouble(*args, "y").value_or(0);
        std::vector<std::string> layer_ids;
        const auto layers_it = args->find(flutter::EncodableValue("layerIds"));
        if (layers_it != args->end()) {
            if (const auto* list = std::get_if<flutter::EncodableList>(&layers_it->second)) {
                for (const auto& item : *list) {
                    if (const auto* s = std::get_if<std::string>(&item)) {
                        layer_ids.push_back(*s);
                    }
                }
            }
        }
        result->Success(flutter::EncodableValue(embedder->FeaturesAtPointJson(x, y, layer_ids)));
        return;
    }

    result->NotImplemented();
}

}  // namespace maplibre_windows
