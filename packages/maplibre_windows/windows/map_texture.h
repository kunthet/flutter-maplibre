#pragma once

#include <flutter/texture_registrar.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace maplibre_windows {

/// Pixel buffer holder for Flutter [PixelBufferTexture].
class MapTexture {
public:
    using FrameCallback = std::function<void()>;

    MapTexture(int width, int height, FrameCallback on_frame_requested);
    ~MapTexture();

    void Resize(int width, int height);
    void UpdatePixels(const uint8_t* src, size_t width, size_t height);
    void MarkFrameAvailable();

    const FlutterDesktopPixelBuffer* CopyPixelBuffer(size_t width, size_t height);

private:
    std::mutex mutex_;
    std::unique_ptr<FlutterDesktopPixelBuffer> buffer_;
    std::vector<uint8_t> pixels_;
    FrameCallback on_frame_requested_;
};

}  // namespace maplibre_windows
