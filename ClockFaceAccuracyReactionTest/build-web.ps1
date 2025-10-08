<#  build-web.ps1
    One-click web build for your raylib + Emscripten game.

    Usage (from project folder):
      powershell -ExecutionPolicy Bypass -File .\build-web.ps1
      # options:
      #   -RebuildRaylib   force rebuild of libraylib.a for web
      #   -NoRun           don't start emrun after build
      #   -NoZip           don't create the zip

      # Example: rebuild raylib and skip launching:
      # .\build-web.ps1 -RebuildRaylib -NoRun
#>

[CmdletBinding()]
param(
  [string]$EmsdkDir = "$env:USERPROFILE\emsdk",
  [string]$RaylibSrc = "C:\Users\htayl\raylib\raylib\src",
  [string]$OutDir    = "dist\web",
  [switch]$RebuildRaylib,
  [switch]$NoRun,
  [switch]$NoZip
)

$ErrorActionPreference = "Stop"

# --- Paths & env -------------------------------------------------------------
$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ProjectRoot

$envScript = Join-Path $EmsdkDir "emsdk_env.ps1"
if (!(Test-Path $envScript)) { throw "emsdk_env.ps1 not found at: $envScript" }
& $envScript | Out-Null

# Ensure em++ is available
Get-Command em++ -ErrorAction Stop | Out-Null

# --- Ensure raylib (web) library exists -------------------------------------
$libray = Join-Path $RaylibSrc "libraylib.a"
if ($RebuildRaylib -or !(Test-Path $libray)) {
  Write-Host "Building raylib for Web (PLATFORM_WEB)..." -ForegroundColor Cyan
  Push-Location $RaylibSrc
  emmake make PLATFORM=PLATFORM_WEB -j
  Pop-Location
}

# --- Build game --------------------------------------------------------------
# Create output dir and compile to dist\web\index.html (+ .js/.wasm)
$null = New-Item -ItemType Directory -Force -Path $OutDir
$OutDir = (Resolve-Path $OutDir).Path
$OutHtml = Join-Path $OutDir "index.html"

$inc = $RaylibSrc
$lib = $RaylibSrc

Write-Host "Compiling to $OutHtml ..." -ForegroundColor Cyan
em++ (Join-Path $ProjectRoot "main.cpp") -o $OutHtml `
 -I"$inc" -L"$lib" -lraylib -DPLATFORM_WEB `
 -sUSE_GLFW=3 -sFETCH=1 -sASYNCIFY -sALLOW_MEMORY_GROWTH=1 -O3

# Copy optional assets (if any)
$assetPatterns = @("*.png","*.jpg","*.jpeg","*.bmp","*.gif","*.wav","*.ogg","*.mp3","*.ttf","*.json","*.data")
foreach ($pattern in $assetPatterns) {
  Get-ChildItem -Path $ProjectRoot -Filter $pattern -File -ErrorAction SilentlyContinue |
    Copy-Item -Destination $OutDir -Force
}

# --- Zip for itch ------------------------------------------------------------
if (-not $NoZip) {
  $zipPath = Join-Path $ProjectRoot "ClockFaceWeb.zip"
  if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
  Compress-Archive -Path (Join-Path $OutDir "*") -DestinationPath $zipPath
  Write-Host "ZIP ready: $zipPath  (Upload this to itch.io as 'Played in the browser')" -ForegroundColor Green
}

# --- Run locally -------------------------------------------------------------
if (-not $NoRun) {
  Write-Host "Launching with emrun..." -ForegroundColor Cyan
  emrun $OutHtml
} else {
  Write-Host "Build complete. Open $OutHtml (via emrun or a local server) or upload the ZIP." -ForegroundColor Green
}
