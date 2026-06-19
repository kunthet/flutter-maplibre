#pragma once

#include <flutter/texture_registrar.h>

#include <cstdint>
#include <functional>
#include <mutex>

namespace maplibre_windows {

/// Holds the current DXGI shared-surface descriptor for a Flutter
/// [GpuSurfaceTexture]. The descriptor is produced on the map render thread and
/// consumed (via [ObtainDescriptor]) on Flutter's raster thread.
class MapGpuTexture {
public:
    using FrameCallback = std::function<void()>;

    explicit MapGpuTexture(FrameCallback on_frame_requested);

    /// Updates the shared handle/size. Called on the map render thread after a
    /// frame has been blitted into the shared texture.
    void SetSurface(void* shared_handle, int width, int height);

    /// Clears the current descriptor so Flutter does not sample a stale shared
    /// handle while the embedder is resizing/recreating the D3D11 texture.
    void InvalidateSurface();

    void MarkFrameAvailable();

    /// Returns the descriptor for the engine to open. The requested width/height
    /// are only hints; the engine reads the actual size from the descriptor.
    const FlutterDesktopGpuSurfaceDescriptor* ObtainDescriptor(size_t width, size_t height);

private:
    std::mutex mutex_;
    FlutterDesktopGpuSurfaceDescriptor descriptor_{};
    FrameCallback on_frame_requested_;
    bool has_surface_ = false;
};

}  // namespace maplibre_windows
