#include "map_texture.h"

namespace maplibre_windows {
namespace {

uint8_t UnpremultiplyChannel(uint8_t channel, uint8_t alpha) {
    if (alpha == 0) {
        return 0;
    }
    const float value = channel / (alpha / 255.0f);
    return static_cast<uint8_t>(value > 255.0f ? 255.0f : value);
}

void CopyUnpremultipliedPixel(const uint8_t* src, uint8_t* dst) {
    const uint8_t alpha = src[3];
    dst[0] = UnpremultiplyChannel(src[0], alpha);
    dst[1] = UnpremultiplyChannel(src[1], alpha);
    dst[2] = UnpremultiplyChannel(src[2], alpha);
    dst[3] = alpha;
}

}  // namespace

MapTexture::MapTexture(int width, int height, FrameCallback on_frame_requested)
    : on_frame_requested_(std::move(on_frame_requested)) {
    Resize(width, height);
}

MapTexture::~MapTexture() = default;

void MapTexture::Resize(int width, int height) {
    if (width <= 0 || height <= 0) {
        return;
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    if (!buffer_) {
        buffer_ = std::make_unique<FlutterDesktopPixelBuffer>();
        buffer_->release_context = &mutex_;
        buffer_->release_callback = [](void* opaque) {
            static_cast<std::mutex*>(opaque)->unlock();
        };
    }
    buffer_->width = static_cast<size_t>(width);
    buffer_->height = static_cast<size_t>(height);
    pixels_.assign(static_cast<size_t>(width) * static_cast<size_t>(height) * 4, 0);
    buffer_->buffer = pixels_.data();
}

void MapTexture::UpdatePixels(const uint8_t* src, size_t src_width, size_t src_height) {
    if (!src || src_width == 0 || src_height == 0) {
        return;
    }

    const std::lock_guard<std::mutex> lock(mutex_);
    if (!buffer_) {
        return;
    }

    // Keep the texture buffer at the renderer's physical resolution so the
    // engine uploads crisp pixels (no CPU resample, correct on HiDPI displays).
    const size_t needed = src_width * src_height * 4;
    if (pixels_.size() != needed) {
        pixels_.assign(needed, 0);
    }
    buffer_->width = src_width;
    buffer_->height = src_height;
    buffer_->buffer = pixels_.data();

    uint8_t* dst = pixels_.data();
    for (size_t i = 0; i < needed; i += 4) {
        const uint8_t alpha = src[i + 3];
        if (alpha == 255) {
            dst[i + 0] = src[i + 0];
            dst[i + 1] = src[i + 1];
            dst[i + 2] = src[i + 2];
            dst[i + 3] = 255;
        } else {
            CopyUnpremultipliedPixel(src + i, dst + i);
        }
    }
}

void MapTexture::MarkFrameAvailable() {
    if (on_frame_requested_) {
        on_frame_requested_();
    }
}

const FlutterDesktopPixelBuffer* MapTexture::CopyPixelBuffer(size_t width, size_t height) {
    // The requested width/height are only a hint; the engine reads the actual
    // dimensions from the returned buffer. Returning our buffer regardless keeps
    // rendering robust across device-pixel-ratio changes and resizes.
    (void)width;
    (void)height;
    mutex_.lock();
    if (!buffer_ || !buffer_->buffer || buffer_->width == 0 || buffer_->height == 0) {
        mutex_.unlock();
        return nullptr;
    }
    // Mutex stays locked until Flutter calls release_callback.
    return buffer_.get();
}

}  // namespace maplibre_windows
