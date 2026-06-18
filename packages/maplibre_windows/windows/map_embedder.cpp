#include "map_embedder.h"

#include <mbgl/gfx/backend_scope.hpp>
#include <mbgl/gfx/headless_frontend.hpp>
#include <mbgl/map/map.hpp>
#include <mbgl/map/map_observer.hpp>
#include <mbgl/map/map_options.hpp>
#include <mbgl/map/transform_state.hpp>
#include <mbgl/renderer/query.hpp>
#include <mbgl/renderer/renderer.hpp>
#include <mbgl/storage/file_source_manager.hpp>
#include <mbgl/storage/resource_options.hpp>
#include <mbgl/style/conversion/filter.hpp>
#include <mbgl/style/conversion/geojson.hpp>
#include <mbgl/style/conversion/layer.hpp>
#include <mbgl/style/conversion/source.hpp>
#include <mbgl/style/conversion_impl.hpp>
#include <mbgl/style/image.hpp>
#include <mbgl/style/layers/background_layer.hpp>
#include <mbgl/style/layers/circle_layer.hpp>
#include <mbgl/style/layers/fill_extrusion_layer.hpp>
#include <mbgl/style/layers/fill_layer.hpp>
#include <mbgl/style/layers/heatmap_layer.hpp>
#include <mbgl/style/layers/hillshade_layer.hpp>
#include <mbgl/style/layers/line_layer.hpp>
#include <mbgl/style/layers/raster_layer.hpp>
#include <mbgl/style/layers/symbol_layer.hpp>
#include <mbgl/style/sources/geojson_source.hpp>
#include <mbgl/style/sources/raster_dem_source.hpp>
#include <mbgl/style/sources/raster_source.hpp>
#include <mbgl/style/sources/vector_source.hpp>
#include <mbgl/style/style.hpp>
#include <mbgl/util/tile_server_options.hpp>
#include <mbgl/util/image.hpp>
#include <mbgl/util/run_loop.hpp>
#include <mbgl/util/tile_server_options.hpp>

#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <sstream>

namespace maplibre_windows {
namespace {

using namespace mbgl;

class EmbedderObserver : public MapObserver {
public:
    explicit EmbedderObserver(std::function<void(const std::string&, const std::string&)> emit)
        : emit_(std::move(emit)) {}

    void onDidFinishLoadingStyle() override { emit_("styleLoaded", "{}"); }
    void onDidBecomeIdle() override { emit_("mapIdle", "{}"); }
    void onCameraDidChange(CameraChangeMode) override { emit_("cameraChanged", "{}"); }
    void onDidFinishRenderingFrame(const RenderFrameStatus& status) override {
        if (!status.needsRepaint) {
            emit_("cameraIdle", "{}");
        }
    }

private:
    std::function<void(const std::string&, const std::string&)> emit_;
};

rapidjson::Document ParseJson(const std::string& text) {
    rapidjson::Document doc;
    doc.Parse(text.c_str());
    return doc;
}

std::string JsonEscape(const std::string& s) {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    writer.String(s.c_str(), static_cast<rapidjson::SizeType>(s.size()));
    return buffer.GetString();
}

}  // namespace

struct MapEmbedder::Impl {
    std::unique_ptr<util::RunLoop> run_loop;
    std::unique_ptr<HeadlessFrontend> frontend;
    std::unique_ptr<Map> map;
    std::unique_ptr<EmbedderObserver> observer;
    int width = 0;
    int height = 0;
    bool drag_pan_enabled = true;
    bool pointer_down = false;
    double last_x = 0;
    double last_y = 0;
    std::atomic<bool> render_requested{false};
};

MapEmbedder::MapEmbedder(int width,
                         int height,
                         float pixel_ratio,
                         std::string init_style,
                         std::optional<CameraState> init_camera,
                         EventCallback on_event,
                         std::function<void(const uint8_t*, size_t, size_t)> on_pixels)
    : pixel_ratio_(pixel_ratio),
      on_event_(std::move(on_event)),
      on_pixels_(std::move(on_pixels)),
      impl_(std::make_unique<Impl>()) {
    impl_->width = width;
    impl_->height = height;

    thread_ = std::thread([this]() { ThreadMain(); });

    std::unique_lock<std::mutex> lock(ready_mutex_);
    ready_cv_.wait(lock, [this] { return thread_ready_; });

    InvokeSync([&] {
        if (!init_style.empty()) {
            const auto normalized = NormalizeStyleUrl(init_style);
            if (!normalized.empty() && (normalized.front() == '{' || normalized.front() == '[')) {
                impl_->map->getStyle().loadJSON(normalized);
            } else {
                impl_->map->getStyle().loadURL(normalized);
            }
        }
        if (init_camera) {
            impl_->map->jumpTo(CameraOptions()
                                   .withCenter(LatLng{init_camera->lat, init_camera->lon})
                                   .withZoom(init_camera->zoom)
                                   .withBearing(init_camera->bearing)
                                   .withPitch(init_camera->pitch));
        }
        RequestRender();
    });
}

MapEmbedder::~MapEmbedder() {
    InvokeSync([&] {
        impl_->map.reset();
        impl_->frontend.reset();
        impl_->observer.reset();
        if (impl_->run_loop) {
            impl_->run_loop->stop();
        }
    });
    if (thread_.joinable()) {
        thread_.join();
    }
}

void MapEmbedder::ThreadMain() {
    impl_->run_loop = std::make_unique<util::RunLoop>();
    impl_->observer = std::make_unique<EmbedderObserver>([this](const std::string& type, const std::string& payload) {
        if (on_event_) {
            on_event_(type, payload);
        }
    });

    const auto cache_path = (std::filesystem::temp_directory_path() / "maplibre_windows_cache.db").string();
    auto map_tiler = TileServerOptions::MapTilerConfiguration();
    ResourceOptions resource_options;
    resource_options.withCachePath(cache_path)
        .withTileServerOptions(map_tiler)
        .withMaximumCacheSize(512 * 1024 * 1024);
    ClientOptions client_options;

    impl_->frontend = std::make_unique<HeadlessFrontend>(
        Size{static_cast<uint32_t>(impl_->width), static_cast<uint32_t>(impl_->height)},
        pixel_ratio_,
        gfx::HeadlessBackend::SwapBehaviour::NoFlush,
        gfx::ContextMode::Unique,
        std::nullopt,
        true);

    impl_->map = std::make_unique<Map>(
        *impl_->frontend,
        *impl_->observer,
        MapOptions()
            .withMapMode(MapMode::Continuous)
            .withSize(impl_->frontend->getSize())
            .withPixelRatio(pixel_ratio_),
        resource_options,
        client_options);

    {
        std::lock_guard<std::mutex> lock(ready_mutex_);
        thread_ready_ = true;
    }
    ready_cv_.notify_all();

    impl_->run_loop->run();
}

void MapEmbedder::InvokeSync(const std::function<void()>& fn) const {
    if (!impl_->run_loop) {
        return;
    }
    if (std::this_thread::get_id() == thread_.get_id()) {
        fn();
        return;
    }
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    impl_->run_loop->invoke([&] {
        fn();
        {
            std::lock_guard<std::mutex> lock(mutex);
            done = true;
        }
        cv.notify_one();
    });
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [&] { return done; });
}

template <typename T>
T MapEmbedder::InvokeSyncValue(const std::function<T()>& fn) const {
    T value{};
    InvokeSync([&] { value = fn(); });
    return value;
}

void MapEmbedder::RequestRender() {
    if (!impl_->map || !impl_->frontend) {
        return;
    }
    gfx::BackendScope guard{*impl_->frontend->getBackend()};
    impl_->frontend->renderOnce(*impl_->map);
    PublishFrame();
}

void MapEmbedder::PublishFrame() {
    if (!on_pixels_ || !impl_->frontend) {
        return;
    }
    const auto image = impl_->frontend->readStillImage();
    if (!image.valid()) {
        return;
    }
    on_pixels_(image.data.get(),
               static_cast<size_t>(image.size.width),
               static_cast<size_t>(image.size.height));
}

void MapEmbedder::Resize(int width, int height) {
    InvokeSync([&] {
        impl_->width = width;
        impl_->height = height;
        impl_->frontend->setSize(Size{static_cast<uint32_t>(width), static_cast<uint32_t>(height)});
        impl_->map->setSize(impl_->frontend->getSize());
        RequestRender();
    });
}

std::string MapEmbedder::NormalizeStyleUrl(std::string style) const {
    if (style.empty()) {
        const auto styles = TileServerOptions::MapTilerConfiguration().defaultStyles();
        if (!styles.empty()) {
            return styles.front().getUrl();
        }
        return style;
    }
    if (style.find("://") == std::string::npos && style.front() != '{' && style.front() != '[') {
        if (style.find("http") == 0) {
            return style;
        }
        return "file://" + style;
    }
    return style;
}

void MapEmbedder::SetStyle(std::string style) {
    InvokeSync([&] {
        const auto normalized = NormalizeStyleUrl(style);
        if (!normalized.empty() && (normalized.front() == '{' || normalized.front() == '[')) {
            impl_->map->getStyle().loadJSON(normalized);
        } else {
            impl_->map->getStyle().loadURL(normalized);
        }
        RequestRender();
    });
}

void MapEmbedder::MoveCamera(const CameraState& camera) {
    InvokeSync([&] {
        auto options = impl_->map->getCameraOptions();
        options = options.withCenter(LatLng{camera.lat, camera.lon})
                     .withZoom(camera.zoom)
                     .withBearing(camera.bearing)
                     .withPitch(camera.pitch);
        impl_->map->jumpTo(options);
        RequestRender();
    });
}

void MapEmbedder::AnimateCamera(const CameraState& camera, int duration_ms) {
    InvokeSync([&] {
        auto options = impl_->map->getCameraOptions();
        options = options.withCenter(LatLng{camera.lat, camera.lon})
                     .withZoom(camera.zoom)
                     .withBearing(camera.bearing)
                     .withPitch(camera.pitch);
        impl_->map->easeTo(options, AnimationOptions{static_cast<uint32_t>(duration_ms)});
        RequestRender();
    });
}

CameraState MapEmbedder::GetCamera() const {
    return InvokeSyncValue<CameraState>([&] {
        const auto options = impl_->map->getCameraOptions();
        CameraState state;
        if (options.center) {
            state.lat = options.center->latitude();
            state.lon = options.center->longitude();
        }
        if (options.zoom) state.zoom = *options.zoom;
        if (options.bearing) state.bearing = *options.bearing;
        if (options.pitch) state.pitch = *options.pitch;
        return state;
    });
}

std::pair<double, double> MapEmbedder::ToLngLat(double x, double y) const {
    return InvokeSyncValue<std::pair<double, double>>([&] {
        const auto coord = impl_->frontend->latLngForPixel(ScreenCoordinate{x, y});
        return std::pair<double, double>{coord.longitude(), coord.latitude()};
    });
}

std::pair<double, double> MapEmbedder::ToScreenLocation(double lon, double lat) const {
    return InvokeSyncValue<std::pair<double, double>>([&] {
        const auto pixel = impl_->frontend->pixelForLatLng(LatLng{lat, lon});
        return std::pair<double, double>{pixel.x, pixel.y};
    });
}

void MapEmbedder::SetDragPanEnabled(bool enabled) {
    InvokeSync([&] { impl_->drag_pan_enabled = enabled; });
}

void MapEmbedder::OnPointer(const std::string& phase,
                            double x,
                            double y,
                            double scroll_delta,
                            bool shift,
                            bool control) {
    InvokeSync([&] {
        if (phase == "scroll") {
            const double zoom_delta = scroll_delta > 0 ? -0.25 : 0.25;
            const auto options = impl_->map->getCameraOptions();
            const double zoom = options.zoom.value_or(0) + zoom_delta;
            impl_->map->jumpTo(options.withZoom(zoom));
            RequestRender();
            return;
        }

        if (!impl_->drag_pan_enabled) {
            return;
        }

        if (phase == "down") {
            impl_->pointer_down = true;
            impl_->last_x = x;
            impl_->last_y = y;
            return;
        }

        if (phase == "move" && impl_->pointer_down) {
            const double dx = x - impl_->last_x;
            const double dy = y - impl_->last_y;
            impl_->last_x = x;
            impl_->last_y = y;
            if (shift) {
                impl_->map->pitchBy(dy * 0.2);
            } else if (control) {
                impl_->map->rotateBy(ScreenCoordinate{0, 0}, ScreenCoordinate{dx, dy});
            } else {
                impl_->map->moveBy(ScreenCoordinate{dx, dy});
            }
            RequestRender();
            return;
        }

        if (phase == "up") {
            impl_->pointer_down = false;
        }
    });
}

void MapEmbedder::AddSource(const std::string& id, const std::string& source_json) {
    InvokeSync([&] {
        auto doc = ParseJson(source_json);
        style::conversion::Error error;
        auto source = style::conversion::convert<std::unique_ptr<style::Source>>(doc, error, id);
        if (!source) {
            return;
        }
        impl_->map->getStyle().addSource(std::move(*source));
        RequestRender();
    });
}

void MapEmbedder::AddLayer(const std::string& layer_json, const std::optional<std::string>& below_layer_id) {
    InvokeSync([&] {
        auto doc = ParseJson(layer_json);
        style::conversion::Error error;
        auto layer = style::conversion::convert<std::unique_ptr<style::Layer>>(doc, error);
        if (!layer) {
            return;
        }
        impl_->map->getStyle().addLayer(std::move(*layer), below_layer_id);
        RequestRender();
    });
}

void MapEmbedder::RemoveLayer(const std::string& id) {
    InvokeSync([&] {
        impl_->map->getStyle().removeLayer(id);
        RequestRender();
    });
}

void MapEmbedder::RemoveSource(const std::string& id) {
    InvokeSync([&] {
        impl_->map->getStyle().removeSource(id);
        RequestRender();
    });
}

void MapEmbedder::UpdateGeoJsonSource(const std::string& id, const std::string& data) {
    InvokeSync([&] {
        auto* source = impl_->map->getStyle().getSource(id);
        if (!source) return;
        auto* geojson = source->as<style::GeoJSONSource>();
        if (!geojson) return;
        auto doc = ParseJson(data);
        style::conversion::Error error;
        auto geo = style::conversion::convert<GeoJSON>(doc, error);
        if (!geo) return;
        geojson->setGeoJSON(std::make_shared<GeoJSON>(std::move(*geo)));
        RequestRender();
    });
}

void MapEmbedder::UpdateLayerFilter(const std::string& id, const std::string& filter_json) {
    InvokeSync([&] {
        auto* layer = impl_->map->getStyle().getLayer(id);
        if (!layer) return;
        auto doc = ParseJson(filter_json);
        style::conversion::Error error;
        auto filter = style::conversion::convert<Filter>(doc, error);
        if (!filter) return;
        layer->setFilter(*filter);
        RequestRender();
    });
}

void MapEmbedder::UpdateVectorSourceTiles(const std::string& id, const std::vector<std::string>& tiles) {
    InvokeSync([&] {
        auto* source = impl_->map->getStyle().getSource(id);
        if (!source) return;
        auto* vector = source->as<style::VectorSource>();
        if (!vector) return;
        style::Tileset tileset;
        tileset.tiles = tiles;
        vector->setTileset(tileset);
        RequestRender();
    });
}

void MapEmbedder::AddImage(const std::string& id, const std::vector<uint8_t>& png_bytes) {
    InvokeSync([&] {
        const std::string bytes(png_bytes.begin(), png_bytes.end());
        auto image = decodeImage(bytes);
        if (!image.valid()) return;
        impl_->map->getStyle().addImage(
            std::make_unique<style::Image>(id, std::move(image), 1.0f, id.find("myura-shape-") == 0 || id.find("myura-svg-") == 0));
        RequestRender();
    });
}

void MapEmbedder::RemoveImage(const std::string& id) {
    InvokeSync([&] {
        impl_->map->getStyle().removeImage(id);
        RequestRender();
    });
}

std::vector<std::string> MapEmbedder::GetLayerIds() const {
    return InvokeSyncValue<std::vector<std::string>>([&] {
        std::vector<std::string> ids;
        for (const auto* layer : impl_->map->getStyle().getLayers()) {
            ids.push_back(layer->getID());
        }
        return ids;
    });
}

std::vector<std::string> MapEmbedder::GetAttributions() const {
    return InvokeSyncValue<std::vector<std::string>>([&] {
        std::vector<std::string> attributions;
        for (const auto* source : impl_->map->getStyle().getSources()) {
            (void)source;
        }
        return attributions;
    });
}

std::string MapEmbedder::FeaturesAtPointJson(double x, double y, const std::vector<std::string>& layer_ids) const {
    return InvokeSyncValue<std::string>([&] {
        if (!impl_->frontend->getRenderer()) {
            return "[]";
        }
        RenderedQueryOptions options;
        options.layerIDs = layer_ids;
        const auto features = impl_->frontend->getRenderer()->queryRenderedFeatures(ScreenCoordinate{x, y}, options);
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        writer.StartArray();
        for (const auto& feature : features) {
            writer.StartObject();
            writer.Key("layerId");
            writer.String(feature.layer.c_str());
            writer.Key("sourceId");
            writer.String(feature.source.c_str());
            if (!feature.sourceLayer.empty()) {
                writer.Key("sourceLayer");
                writer.String(feature.sourceLayer.c_str());
            }
            writer.Key("properties");
            writer.StartObject();
            feature.properties.match(
                [&](const std::unordered_map<std::string, Value>& map) {
                    for (const auto& [key, value] : map) {
                        writer.Key(key.c_str());
                        value.match(
                            [&](const std::string& s) { writer.String(s.c_str()); },
                            [&](const bool b) { writer.Bool(b); },
                            [&](const uint64_t u) { writer.Uint64(u); },
                            [&](const int64_t i) { writer.Int64(i); },
                            [&](const double d) { writer.Double(d); },
                            [&](const auto&) { writer.Null(); });
                    }
                },
                [&](const auto&) {});
            writer.EndObject();
            writer.EndObject();
        }
        writer.EndArray();
        return buffer.GetString();
    });
}

}  // namespace maplibre_windows
