# Build maplibre-native embed library for maplibre_windows (Release).
# Run from repo root or this package directory.
$ErrorActionPreference = "Stop"

$vcvars = "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars)) {
  $vcvars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
}
if (-not (Test-Path $vcvars)) {
  throw "Visual Studio vcvars64.bat not found"
}

$root = Resolve-Path (Join-Path $PSScriptRoot "..\..\..\..\maplibre-native")
$build = Join-Path $root "build-windows-opengl"

cmd /c "`"$vcvars`" && cmake --build `"$build`" --target maplibre-embed --config Release && cmake --build `"$build`" --target mbgl-core-deps --config Release"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "maplibre-embed built at $build\platform\windows\maplibre_embed\maplibre-embed.lib"
Write-Host "Build myuramap with: flutter build windows --release"
