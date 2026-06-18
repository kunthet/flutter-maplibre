#include <maplibre_windows/maplibre_windows_plugin.h>

#include <flutter/plugin_registrar_windows.h>

void MaplibreWindowsPluginRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar) {
    maplibre_windows::MaplibreWindowsPlugin::RegisterWithRegistrar(
        flutter::PluginRegistrarManager::GetInstance()
            ->GetRegistrar<flutter::PluginRegistrarWindows>(registrar));
}
