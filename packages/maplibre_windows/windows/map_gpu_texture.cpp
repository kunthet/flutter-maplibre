#include "map_gpu_texture.h"

#include <cstdlib>

namespace maplibre_windows {

namespace {
// Diagnostic gate: when MAPLIBRE_GPU_NO_CONSUMER=1, never hand a descriptor to
// the engine so Flutter never opens/samples the shared texture. Used to isolate
// producer/mbgl crashes from consumer (cross-device sampling) crashes.
bool ConsumerDisabled() {
    static const bool disabled = [] {
        char* value = nullptr;
        size_t len = 0;
        const bool on = _dupenv_s(&value, &len, "MAPLIBRE_GPU_NO_CONSUMER") == 0 && value != nullptr &&
                        value[0] == '1';
        free(value);
        return on;
    }();
    return disabled;
}
}  // namespace

MapGpuTexture::MapGpuTexture(FrameCallback on_frame_requested)
    : on_frame_requested_(std::move(on_frame_requested)) {
    descriptor_.struct_size = sizeof(FlutterDesktopGpuSurfaceDescriptor);
    descriptor_.format = kFlutterDesktopPixelFormatRGBA8888;
    descriptor_.release_callback = nullptr;
    descriptor_.release_context = nullptr;
}

void MapGpuTexture::SetSurface(void* shared_handle, int width, int height) {
    const std::lock_guard<std::mutex> lock(mutex_);
    descriptor_.handle = shared_handle;
    descriptor_.width = descriptor_.visible_width = static_cast<size_t>(width);
    descriptor_.height = descriptor_.visible_height = static_cast<size_t>(height);
    has_surface_ = shared_handle != nullptr && width > 0 && height > 0;
}

void MapGpuTexture::InvalidateSurface() {
    const std::lock_guard<std::mutex> lock(mutex_);
    descriptor_.handle = nullptr;
    descriptor_.width = descriptor_.visible_width = 0;
    descriptor_.height = descriptor_.visible_height = 0;
    has_surface_ = false;
}

void MapGpuTexture::MarkFrameAvailable() {
    if (on_frame_requested_) {
        on_frame_requested_();
    }
}

const FlutterDesktopGpuSurfaceDescriptor* MapGpuTexture::ObtainDescriptor(size_t width, size_t height) {
    (void)width;
    (void)height;
    if (ConsumerDisabled()) {
        return nullptr;
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    if (!has_surface_) {
        return nullptr;
    }
    return &descriptor_;
}

}  // namespace maplibre_windows
