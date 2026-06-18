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

    const size_t dst_width = buffer_->width;
    const size_t dst_height = buffer_->height;
    if (dst_width == 0 || dst_height == 0) {
        return;
    }

    pixels_.assign(dst_width * dst_height * 4, 0);
    buffer_->buffer = pixels_.data();

    if (src_width == dst_width && src_height == dst_height) {
        for (size_t i = 0; i < pixels_.size(); i += 4) {
            CopyUnpremultipliedPixel(src + i, pixels_.data() + i);
        }
        return;
    }

    // HeadlessFrontend renders at physical resolution; Flutter texture is logical size.
    for (size_t y = 0; y < dst_height; ++y) {
        const size_t src_y = y * src_height / dst_height;
        for (size_t x = 0; x < dst_width; ++x) {
            const size_t src_x = x * src_width / dst_width;
            const size_t src_index = (src_y * src_width + src_x) * 4;
            const size_t dst_index = (y * dst_width + x) * 4;
            CopyUnpremultipliedPixel(src + src_index, pixels_.data() + dst_index);
        }
    }
}

void MapTexture::MarkFrameAvailable() {
    if (on_frame_requested_) {
        on_frame_requested_();
    }
}

const FlutterDesktopPixelBuffer* MapTexture::CopyPixelBuffer(size_t width, size_t height) {
    mutex_.lock();
    if (!buffer_ || buffer_->width != width || buffer_->height != height) {
        mutex_.unlock();
        return nullptr;
    }
    // Mutex stays locked until Flutter calls release_callback.
    return buffer_.get();
}

}  // namespace maplibre_windows
