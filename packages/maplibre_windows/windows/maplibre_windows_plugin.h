#pragma once

#include <flutter/event_channel.h>
#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/standard_method_codec.h>

#include <memory>

namespace maplibre_windows {

class MaplibreWindowsPlugin : public flutter::Plugin {
public:
    static void RegisterWithRegistrar(flutter::PluginRegistrarWindows* registrar);

    explicit MaplibreWindowsPlugin(flutter::PluginRegistrarWindows* registrar);
    ~MaplibreWindowsPlugin() override;

    MaplibreWindowsPlugin(const MaplibreWindowsPlugin&) = delete;
    MaplibreWindowsPlugin& operator=(const MaplibreWindowsPlugin&) = delete;

private:
    void HandleMethodCall(
        const flutter::MethodCall<flutter::EncodableValue>& method_call,
        std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

    flutter::PluginRegistrarWindows* registrar_;
    flutter::TextureRegistrar* texture_registrar_;
    std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>> method_channel_;
    std::unique_ptr<flutter::EventChannel<flutter::EncodableValue>> event_channel_;
};

}  // namespace maplibre_windows
